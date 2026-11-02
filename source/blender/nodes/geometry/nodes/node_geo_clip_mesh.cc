/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "DNA_mesh_types.h"
#include "DNA_object_types.h"

#include "BLI_math_matrix.hh"
#include "BLI_math_vector.hh"

#include "BKE_customdata.hh"
#include "BKE_mesh.hh"

#include "GEO_foreach_geometry.hh"
#include "GEO_mesh_clip.hh"
#include "GEO_randomize.hh"

#include "bmesh.hh"

#include "UI_resources.hh"

#include "node_geometry_util.hh"

namespace blender::nodes::node_geo_clip_mesh_cc {

using geometry::BMeshClipKeep;

enum class Space {
  Object = 0,
  World = 1,
};

static const EnumPropertyItem keep_items[] = {
    {int(BMeshClipKeep::Above),
     "ABOVE",
     ICON_NONE,
     N_("Above"),
     N_("Keep the part on the side the direction points to")},
    {int(BMeshClipKeep::Below), "BELOW", ICON_NONE, N_("Below"), N_("Keep the other part")},
    {int(BMeshClipKeep::Both),
     "BOTH",
     ICON_NONE,
     N_("Both"),
     N_("Keep everything, and only add the cut to the surface")},
    {0, nullptr, 0, nullptr, nullptr},
};

static const EnumPropertyItem space_items[] = {
    {int(Space::Object),
     "OBJECT",
     ICON_NONE,
     N_("Object"),
     N_("The plane moves and rotates with the object")},
    {int(Space::World),
     "WORLD",
     ICON_NONE,
     N_("World"),
     N_("The plane stays in place when the object moves")},
    {0, nullptr, 0, nullptr, nullptr},
};

static void node_declare(NodeDeclarationBuilder &b)
{
  b.use_custom_socket_order();
  b.allow_any_socket_order();

  b.add_input<decl::Geometry>("Mesh")
      .supported_type(GeometryComponent::Type::Mesh)
      .is_default_link_socket()
      .description("Mesh to cut");
  b.add_output<decl::Geometry>("Mesh").propagate_all().align_with_previous();
  b.add_output<decl::Bool>("Cut Edges").field_on_all().description("The edges along the cut");
  b.add_output<decl::Bool>("Interior")
      .field_on_all()
      .description("The faces that fill the cut");

  b.add_input<decl::Menu>("Keep")
      .static_items(keep_items)
      .default_value(BMeshClipKeep::Above)
      .optional_label();
  b.add_input<decl::Menu>("Space")
      .static_items(space_items)
      .default_value(Space::Object)
      .description("What the origin and direction are relative to");
  b.add_input<decl::Vector>("Origin")
      .subtype(PROP_TRANSLATION)
      .description("A point on the cutting plane");
  b.add_input<decl::Vector>("Direction")
      .default_value({0.0f, 0.0f, 1.0f})
      .subtype(PROP_XYZ)
      .description("The direction the cutting plane faces");
  b.add_input<decl::Float>("Distance")
      .default_value(0.0f)
      .subtype(PROP_DISTANCE)
      .description("Move the plane from the origin along the direction");
  b.add_input<decl::Bool>("Fill Cut")
      .default_value(true)
      .description("Close the holes left by the cut. Not used when keeping both sides");
}

static Mesh *clip_mesh(const Mesh &mesh,
                       const float3 &plane_co,
                       const float3 &plane_no,
                       const BMeshClipKeep keep,
                       const bool fill,
                       const std::optional<std::string> &cut_edges_id,
                       const std::optional<std::string> &interior_id)
{
  BMeshCreateParams create_params{};
  create_params.use_toolflags = true;
  BMeshFromMeshParams convert_params{};
  convert_params.calc_face_normal = true;
  convert_params.calc_vert_normal = true;
  BMesh *bm = BKE_mesh_to_bmesh_ex(&mesh, &create_params, &convert_params);

  geometry::BMeshClipParams clip_params;
  clip_params.plane_co = plane_co;
  clip_params.plane_no = plane_no;
  clip_params.keep = keep;
  clip_params.fill = fill;
  if (const std::optional<Bounds<float3>> bounds = mesh.bounds_min_max()) {
    clip_params.threshold = std::max(math::distance(bounds->min, bounds->max) * 1e-5f, 1e-7f);
  }
  if (cut_edges_id) {
    BM_data_layer_add_named(bm, &bm->edata, CD_PROP_BOOL, cut_edges_id->c_str());
    clip_params.cut_edge_offset = CustomData_get_offset_named(
        &bm->edata, CD_PROP_BOOL, cut_edges_id->c_str());
  }
  if (interior_id) {
    BM_data_layer_add_named(bm, &bm->pdata, CD_PROP_BOOL, interior_id->c_str());
    clip_params.interior_face_offset = CustomData_get_offset_named(
        &bm->pdata, CD_PROP_BOOL, interior_id->c_str());
  }
  geometry::bmesh_clip(bm, clip_params);

  Mesh *result = BKE_mesh_from_bmesh_for_eval_nomain(bm, nullptr, &mesh);
  BM_mesh_free(bm);
  return result;
}

static void node_geo_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");
  const BMeshClipKeep keep = params.extract_input<BMeshClipKeep>("Keep");
  const Space space = params.extract_input<Space>("Space");
  const float3 origin = params.extract_input<float3>("Origin");
  const float3 direction = params.extract_input<float3>("Direction");
  const float distance = params.extract_input<float>("Distance");
  const bool fill = params.extract_input<bool>("Fill Cut");

  if (math::is_zero(direction)) {
    params.error_message_add(NodeWarningType::Info, TIP_("The direction is zero"));
    params.set_output("Mesh", std::move(geometry_set));
    return;
  }
  float3 plane_no = math::normalize(direction);
  float3 plane_co = origin + plane_no * distance;
  if (space == Space::World) {
    const Object *self_object = params.self_object();
    plane_co = math::transform_point(self_object->world_to_object(), plane_co);
    /* The transpose keeps the plane correct on objects with a non-uniform scale. */
    plane_no = math::normalize(math::transpose(float3x3(self_object->object_to_world())) *
                               plane_no);
  }

  const std::optional<std::string> cut_edges_id =
      params.get_output_anonymous_attribute_id_if_needed("Cut Edges");
  const std::optional<std::string> interior_id =
      params.get_output_anonymous_attribute_id_if_needed("Interior");

  geometry::foreach_real_geometry(geometry_set, [&](GeometrySet &geometry_set) {
    const Mesh *src_mesh = geometry_set.get_mesh();
    if (src_mesh == nullptr || src_mesh->verts_num == 0) {
      return;
    }
    Mesh *result = clip_mesh(
        *src_mesh, plane_co, plane_no, keep, fill, cut_edges_id, interior_id);
    geometry::debug_randomize_mesh_order(result);
    geometry_set.replace_mesh(result);
  });
  params.set_output("Mesh", std::move(geometry_set));
}

static void node_register()
{
  static blender::bke::bNodeType ntype;

  geo_node_type_base(&ntype, "GeometryNodeClipMesh");
  ntype.ui_name = "Clip";
  ntype.ui_description = "Cut a mesh with a plane and remove one side, optionally filling the cut";
  ntype.nclass = NODE_CLASS_GEOMETRY;
  ntype.declare = node_declare;
  ntype.geometry_node_execute = node_geo_exec;
  blender::bke::node_register_type(ntype);
}
NOD_REGISTER_NODE(node_register)

}  // namespace blender::nodes::node_geo_clip_mesh_cc
