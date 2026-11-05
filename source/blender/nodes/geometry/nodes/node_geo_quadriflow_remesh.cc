/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "DNA_mesh_types.h"

#include "GEO_foreach_geometry.hh"
#include "GEO_quadriflow_remesh.hh"

#include "UI_resources.hh"

#include "node_geometry_util.hh"

namespace blender::nodes::node_geo_quadriflow_remesh_cc {

/** Same values as #geometry::QuadriflowTarget. */
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

  geometry::QuadriflowRemeshParams remesh_params;
  remesh_params.target = geometry::QuadriflowTarget(int(target));
  remesh_params.face_count = face_count;
  remesh_params.ratio = ratio;
  remesh_params.edge_length = edge_length;
  remesh_params.seed = seed;
  remesh_params.preserve_sharp = preserve_sharp;
  remesh_params.preserve_boundary = preserve_boundary;

  std::atomic<bool> has_unsupported_mesh = false;
  std::atomic<bool> has_failure = false;
  std::atomic<bool> is_disabled = false;
  geometry::foreach_real_geometry(geometry_set, [&](GeometrySet &geometry_set) {
    const Mesh *src_mesh = geometry_set.get_mesh();
    if (src_mesh == nullptr) {
      return;
    }
    geometry::QuadriflowRemeshResult result_kind;
    Mesh *result = geometry::quadriflow_remesh(*src_mesh, remesh_params, result_kind);
    switch (result_kind) {
      case geometry::QuadriflowRemeshResult::Success:
        geometry_set.replace_mesh(result);
        break;
      case geometry::QuadriflowRemeshResult::Unsupported:
        has_unsupported_mesh = true;
        break;
      case geometry::QuadriflowRemeshResult::Failed:
        has_failure = true;
        break;
      case geometry::QuadriflowRemeshResult::Disabled:
        is_disabled = true;
        break;
      case geometry::QuadriflowRemeshResult::Skipped:
        break;
    }
  });

  if (has_unsupported_mesh) {
    params.error_message_add(
        NodeWarningType::Error,
        TIP_("The mesh must be manifold, with faces pointing the same way and no zero-length edges"));
  }
  if (has_failure) {
    params.error_message_add(NodeWarningType::Error, TIP_("The mesh could not be remeshed"));
  }
  if (is_disabled) {
    params.error_message_add(NodeWarningType::Error, TIP_("Disabled, built without QuadriFlow"));
  }
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
