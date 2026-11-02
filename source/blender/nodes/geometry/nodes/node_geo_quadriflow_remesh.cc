/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "DNA_mesh_types.h"

#include "BLI_math_geom.h"
#include "BLI_math_vector.hh"

#include "BKE_mesh.hh"
#include "BKE_mesh_remesh_voxel.hh"

#include "GEO_foreach_geometry.hh"
#include "GEO_randomize.hh"

#include "UI_resources.hh"

#include "node_geometry_util.hh"

namespace blender::nodes::node_geo_quadriflow_remesh_cc {

enum class Target {
  FaceCount = 0,
  Ratio = 1,
  EdgeLength = 2,
};

static const EnumPropertyItem target_items[] = {
    {int(Target::FaceCount),
     "FACE_COUNT",
     ICON_NONE,
     N_("Face Count"),
     N_("Aim for a number of quads")},
    {int(Target::Ratio),
     "RATIO",
     ICON_NONE,
     N_("Ratio"),
     N_("Aim for a number of quads relative to the number of faces in the input")},
    {int(Target::EdgeLength),
     "EDGE_LENGTH",
     ICON_NONE,
     N_("Edge Length"),
     N_("Aim for quads with edges of this length")},
    {0, nullptr, 0, nullptr, nullptr},
};

static void node_declare(NodeDeclarationBuilder &b)
{
  b.use_custom_socket_order();
  b.allow_any_socket_order();

  b.add_input<decl::Geometry>("Mesh")
      .supported_type(GeometryComponent::Type::Mesh)
      .is_default_link_socket()
      .description("Mesh to rebuild out of quads. It must be manifold");
  b.add_output<decl::Geometry>("Mesh").propagate_all().align_with_previous();

  b.add_input<decl::Menu>("Target")
      .static_items(target_items)
      .default_value(Target::FaceCount)
      .optional_label();
  b.add_input<decl::Int>("Face Count")
      .default_value(1000)
      .min(1)
      .usage_by_menu("Target", int(Target::FaceCount))
      .description("Desired number of quads. The result is only close to this");
  b.add_input<decl::Float>("Ratio")
      .default_value(1.0f)
      .min(0.0f)
      .usage_by_menu("Target", int(Target::Ratio))
      .description("Desired number of quads as a fraction of the number of input faces");
  b.add_input<decl::Float>("Edge Length")
      .default_value(0.1f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .usage_by_menu("Target", int(Target::EdgeLength))
      .description("Desired length of the edges of the quads");
  b.add_input<decl::Int>("Seed").default_value(0).description(
      "Number that changes the layout of the quads");
  b.add_input<decl::Bool>("Preserve Sharp")
      .default_value(false)
      .description("Try to follow the sharp features of the surface");
  b.add_input<decl::Bool>("Preserve Boundary")
      .default_value(false)
      .description("Try to follow the open boundaries of the mesh");
}

/** QuadriFlow can crash on anything but a consistent manifold, so it's checked first. */
static bool mesh_is_supported(const Mesh &mesh)
{
  const Span<float3> positions = mesh.vert_positions();
  const Span<int2> edges = mesh.edges();
  const Span<int> corner_verts = mesh.corner_verts();
  const Span<int> corner_edges = mesh.corner_edges();

  Array<int8_t> edge_face_counts(mesh.edges_num, 0);
  Array<int> edge_first_verts(mesh.edges_num, -1);
  for (const int corner : corner_verts.index_range()) {
    const int edge = corner_edges[corner];
    edge_face_counts[edge]++;
    if (edge_face_counts[edge] > 2) {
      return false;
    }
    /* Neighboring faces have to go around in the same direction. */
    if (edge_first_verts[edge] == corner_verts[corner]) {
      return false;
    }
    edge_first_verts[edge] = corner_verts[corner];
  }
  for (const int edge : edges.index_range()) {
    if (edge_face_counts[edge] == 0) {
      return false;
    }
    if (math::distance_squared(positions[edges[edge][0]], positions[edges[edge][1]]) < 1e-8f) {
      return false;
    }
  }
  return true;
}

static float mesh_area(const Mesh &mesh)
{
  const Span<float3> positions = mesh.vert_positions();
  const Span<int> corner_verts = mesh.corner_verts();
  float area = 0.0f;
  for (const int3 &tri : mesh.corner_tris()) {
    area += area_tri_v3(positions[corner_verts[tri[0]]],
                        positions[corner_verts[tri[1]]],
                        positions[corner_verts[tri[2]]]);
  }
  return area;
}

/** There's no way to stop the evaluation of a node, but the remesher requires a callback. */
static void update_callback(void * /*user_data*/, float /*progress*/, int *cancel)
{
  *cancel = 0;
}

static void node_geo_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");
  const Target target = params.extract_input<Target>("Target");
  const int face_count = params.extract_input<int>("Face Count");
  const float ratio = params.extract_input<float>("Ratio");
  const float edge_length = params.extract_input<float>("Edge Length");
  const int seed = params.extract_input<int>("Seed");
  const bool preserve_sharp = params.extract_input<bool>("Preserve Sharp");
  const bool preserve_boundary = params.extract_input<bool>("Preserve Boundary");

#ifdef WITH_QUADRIFLOW
  std::atomic<bool> has_unsupported_mesh = false;
  std::atomic<bool> has_failure = false;
  geometry::foreach_real_geometry(geometry_set, [&](GeometrySet &geometry_set) {
    const Mesh *src_mesh = geometry_set.get_mesh();
    if (src_mesh == nullptr || src_mesh->faces_num == 0) {
      return;
    }
    if (!mesh_is_supported(*src_mesh)) {
      has_unsupported_mesh = true;
      return;
    }
    int target_faces = face_count;
    if (target == Target::Ratio) {
      target_faces = int(float(src_mesh->faces_num) * ratio);
    }
    else if (target == Target::EdgeLength) {
      if (edge_length <= 0.0f) {
        return;
      }
      target_faces = int(std::min(mesh_area(*src_mesh) / (edge_length * edge_length), 1e8f));
    }
    /* The remesher can't make a closed mesh with very few quads. */
    target_faces = std::max(target_faces, 6);

    Mesh *result = BKE_mesh_remesh_quadriflow(
        src_mesh, target_faces, seed, preserve_sharp, preserve_boundary, false, update_callback, nullptr);
    if (result == nullptr) {
      has_failure = true;
      return;
    }
    BKE_mesh_copy_parameters_for_eval(result, src_mesh);
    bke::mesh_remesh_reproject_attributes(*src_mesh, *result);
    geometry::debug_randomize_mesh_order(result);
    geometry_set.replace_mesh(result);
  });

  if (has_unsupported_mesh) {
    params.error_message_add(
        NodeWarningType::Error,
        TIP_("The mesh must be manifold, with faces pointing the same way and no zero-length edges"));
  }
  if (has_failure) {
    params.error_message_add(NodeWarningType::Error, TIP_("The mesh could not be remeshed"));
  }
#else
  UNUSED_VARS(target, face_count, ratio, edge_length, seed, preserve_sharp, preserve_boundary);
  params.error_message_add(NodeWarningType::Error, TIP_("Disabled, built without QuadriFlow"));
#endif
  params.set_output("Mesh", std::move(geometry_set));
}

static void node_register()
{
  static blender::bke::bNodeType ntype;

  geo_node_type_base(&ntype, "GeometryNodeQuadriFlowRemesh");
  ntype.ui_name = "QuadriFlow Remesh";
  ntype.ui_description = "Rebuild a manifold mesh out of quads that follow the flow of its surface";
  ntype.nclass = NODE_CLASS_GEOMETRY;
  ntype.declare = node_declare;
  ntype.geometry_node_execute = node_geo_exec;
  blender::bke::node_register_type(ntype);
}
NOD_REGISTER_NODE(node_register)

}  // namespace blender::nodes::node_geo_quadriflow_remesh_cc
