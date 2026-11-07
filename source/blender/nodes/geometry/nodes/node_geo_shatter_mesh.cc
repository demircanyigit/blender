/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <mutex>

#include "DNA_mesh_types.h"

#include "GEO_foreach_geometry.hh"
#include "GEO_mesh_shatter.hh"
#include "GEO_randomize.hh"

#include "UI_resources.hh"

#include "node_geometry_util.hh"

namespace blender::nodes::node_geo_shatter_mesh_cc {

using geometry::ShatterMaterial;

enum class GrainAxis {
  Auto = 0,
  X = 1,
  Y = 2,
  Z = 3,
};

static const EnumPropertyItem material_items[] = {
    {int(ShatterMaterial::Stone),
     "STONE",
     ICON_NONE,
     N_("Stone"),
     N_("Chunky pieces of mixed sizes, with small chips")},
    {int(ShatterMaterial::Glass),
     "GLASS",
     ICON_NONE,
     N_("Glass"),
     N_("Shards between cracks running out from the impact and rings around it")},
    {int(ShatterMaterial::Wood), "WOOD", ICON_NONE, N_("Wood"), N_("Long splinters along the grain")},
    {int(ShatterMaterial::Ceramic), "CERAMIC", ICON_NONE, N_("Ceramic"), N_("A few big pieces")},
    {int(ShatterMaterial::Ice),
     "ICE",
     ICON_NONE,
     N_("Ice"),
     N_("Cracks from the impact like glass, with chunks like stone")},
    {0, nullptr, 0, nullptr, nullptr},
};

static const EnumPropertyItem grain_axis_items[] = {
    {int(GrainAxis::Auto), "AUTO", ICON_NONE, N_("Auto"), N_("The longest side of the mesh")},
    {int(GrainAxis::X), "X", ICON_NONE, N_("X"), ""},
    {int(GrainAxis::Y), "Y", ICON_NONE, N_("Y"), ""},
    {int(GrainAxis::Z), "Z", ICON_NONE, N_("Z"), ""},
    {0, nullptr, 0, nullptr, nullptr},
};

static void node_declare(NodeDeclarationBuilder &b)
{
  b.use_custom_socket_order();
  b.allow_any_socket_order();

  b.add_input<decl::Geometry>("Mesh")
      .supported_type(GeometryComponent::Type::Mesh)
      .is_default_link_socket()
      .description("Closed mesh to break");
  b.add_output<decl::Geometry>("Mesh").propagate_all().align_with_previous();
  b.add_output<decl::Int>("Piece Index")
      .field_on_all()
      .description("Number of the piece, which can be used to split the mesh into the pieces");
  b.add_output<decl::Bool>("Interior").field_on_all().description("The broken faces");

  b.add_input<decl::Menu>("Material")
      .static_items(material_items)
      .default_value(ShatterMaterial::Stone)
      .optional_label()
      .description("How it breaks");
  b.add_input<decl::Int>("Pieces").default_value(20).min(1).max(100000).description(
      "About how many pieces to break into");
  b.add_input<decl::Int>("Seed").default_value(0).description("Changes the pattern");
  b.add_input<decl::Vector>("Impact")
      .subtype(PROP_TRANSLATION)
      .description("Where it was hit. The pieces are smaller there, and glass cracks out from it");
  b.add_input<decl::Float>("Focus")
      .default_value(0.5f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("How much smaller the pieces get near the impact");
  b.add_input<decl::Menu>("Grain Axis")
      .static_items(grain_axis_items)
      .default_value(GrainAxis::Auto)
      .usage_by_menu("Material", int(ShatterMaterial::Wood))
      .description("Direction of the wood grain");
  b.add_input<decl::Float>("Grain")
      .default_value(4.0f)
      .min(1.0f)
      .max(50.0f)
      .usage_by_menu("Material", int(ShatterMaterial::Wood))
      .description("How many times longer than wide the splinters are");
  b.add_input<decl::Float>("Roughness")
      .default_value(0.3f)
      .min(0.0f)
      .max(5.0f)
      .description("How jagged the broken faces are. Glass and ceramic look best at 0");
  b.add_input<decl::Int>("Detail")
      .default_value(2)
      .min(0)
      .max(6)
      .description("Points on the broken faces for the roughness to move");
  b.add_input<decl::Float>("Cut Gap")
      .default_value(0.0f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .description("Distance between neighboring pieces");
}

static void node_geo_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");

  geometry::MeshShatterParams settings;
  settings.material = params.extract_input<ShatterMaterial>("Material");
  settings.pieces = params.extract_input<int>("Pieces");
  settings.seed = params.extract_input<int>("Seed");
  settings.impact = params.extract_input<float3>("Impact");
  settings.focus = params.extract_input<float>("Focus");
  settings.grain_axis = int(params.extract_input<GrainAxis>("Grain Axis")) - 1;
  settings.grain = params.extract_input<float>("Grain");
  settings.roughness = params.extract_input<float>("Roughness");
  settings.detail = params.extract_input<int>("Detail");
  settings.cut_gap = std::max(params.extract_input<float>("Cut Gap"), 0.0f);
  settings.piece_index_id = params.get_output_anonymous_attribute_id_if_needed("Piece Index");
  settings.interior_id = params.get_output_anonymous_attribute_id_if_needed("Interior");

  std::mutex error_mutex;
  const char *error = nullptr;
  geometry::foreach_real_geometry(geometry_set, [&](GeometrySet &geometry_set) {
    const Mesh *src_mesh = geometry_set.get_mesh();
    if (src_mesh == nullptr || src_mesh->faces_num == 0) {
      return;
    }
    const char *mesh_error = nullptr;
    Mesh *result = geometry::mesh_shatter(*src_mesh, settings, &mesh_error);
    if (result == nullptr) {
      if (mesh_error != nullptr) {
        std::lock_guard lock{error_mutex};
        error = mesh_error;
      }
      return;
    }
    geometry::debug_randomize_mesh_order(result);
    geometry_set.replace_mesh(result);
  });

  if (error != nullptr) {
    params.error_message_add(NodeWarningType::Error, TIP_(error));
  }
  params.set_output("Mesh", std::move(geometry_set));
}

static void node_register()
{
  static blender::bke::bNodeType ntype;

  geo_node_type_base(&ntype, "GeometryNodeShatterMesh");
  ntype.ui_name = "Shatter";
  ntype.ui_description =
      "Break a closed mesh into solid pieces the way glass, wood, stone, ceramic or ice breaks";
  ntype.nclass = NODE_CLASS_GEOMETRY;
  ntype.declare = node_declare;
  ntype.geometry_node_execute = node_geo_exec;
  blender::bke::node_register_type(ntype);
}
NOD_REGISTER_NODE(node_register)

}  // namespace blender::nodes::node_geo_shatter_mesh_cc
