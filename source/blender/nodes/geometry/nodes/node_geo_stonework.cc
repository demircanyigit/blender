/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "DNA_mesh_types.h"

#include "BKE_mesh.hh"

#include "GEO_foreach_geometry.hh"
#include "GEO_mesh_stonework.hh"
#include "GEO_randomize.hh"
#include "GEO_realize_instances.hh"

#include "UI_resources.hh"

#include "node_geometry_util.hh"

namespace blender::nodes::node_geo_stonework_cc {

using geometry::StoneworkColorMode;
using geometry::StoneworkFill;
using geometry::StoneworkRemesh;
using geometry::StoneworkShape;

static const EnumPropertyItem fill_items[] = {
    {int(StoneworkFill::Volume), "VOLUME", ICON_NONE, N_("Volume"), N_("The inside of the mesh")},
    {int(StoneworkFill::Box), "BOX", ICON_NONE, N_("Box"), N_("The bounding box of the mesh")},
    {int(StoneworkFill::Surface),
     "SURFACE",
     ICON_NONE,
     N_("Surface"),
     N_("One layer of stones just under the surface")},
    {0, nullptr, 0, nullptr, nullptr},
};

static const EnumPropertyItem shape_items[] = {
    {int(StoneworkShape::Round), "ROUND", ICON_NONE, N_("Round"), N_("Round stones")},
    {int(StoneworkShape::Angular), "ANGULAR", ICON_NONE, N_("Angular"), N_("Broken, angular stones")},
    {int(StoneworkShape::Blocks), "BLOCKS", ICON_NONE, N_("Blocks"), N_("Rough blocks")},
    {int(StoneworkShape::Slabs), "SLABS", ICON_NONE, N_("Slabs"), N_("Long, thin slabs")},
    {int(StoneworkShape::Wedges), "WEDGES", ICON_NONE, N_("Wedges"), N_("Wedge shaped stones")},
    {int(StoneworkShape::Mixed), "MIXED", ICON_NONE, N_("Mixed"), N_("Stones of every shape")},
    {int(StoneworkShape::DryWall),
     "DRY_WALL",
     ICON_NONE,
     N_("Dry Wall"),
     N_("Blocks and slabs laid flat, a few standing stones, wedges in the gaps")},
    {0, nullptr, 0, nullptr, nullptr},
};

static const EnumPropertyItem remesh_items[] = {
    {int(StoneworkRemesh::Isotropic),
     "ISOTROPIC",
     ICON_NONE,
     N_("Isotropic"),
     N_("Even triangles that follow the surface")},
    {int(StoneworkRemesh::Voxel),
     "VOXEL",
     ICON_NONE,
     N_("Voxel"),
     N_("A new closed surface made from voxels")},
    {int(StoneworkRemesh::Divide), "DIVIDE", ICON_NONE, N_("Divide"), N_("The faces cut by a grid")},
    {int(StoneworkRemesh::None), "NONE", ICON_NONE, N_("None"), N_("The faces as they are")},
    {0, nullptr, 0, nullptr, nullptr},
};

static const EnumPropertyItem color_mode_items[] = {
    {int(StoneworkColorMode::Hue), "HUE", ICON_NONE, N_("Hue"), N_("Random hues")},
    {int(StoneworkColorMode::Gray), "GRAY", ICON_NONE, N_("Gray"), N_("Random shades of gray")},
    {0, nullptr, 0, nullptr, nullptr},
};

static void node_declare(NodeDeclarationBuilder &b)
{
  b.use_custom_socket_order();
  b.allow_any_socket_order();

  b.add_input<decl::Geometry>("Mesh")
      .supported_type(GeometryComponent::Type::Mesh)
      .is_default_link_socket()
      .description("Mesh to fill with stones");
  b.add_output<decl::Geometry>("Stones").align_with_previous();
  b.add_output<decl::Int>("Stone ID").field_on_all().description("Which stone each face is");
  b.add_output<decl::Float>("Random")
      .field_on_all()
      .description("A random value from 0 to 1 for each stone");
  b.add_output<decl::Color>("Color")
      .field_on_all()
      .description("A random color for each stone. The mortar is gray");
  b.add_output<decl::Bool>("Mortar").field_on_all().description("True on the mortar's faces");

  b.add_input<decl::Menu>("Fill")
      .static_items(fill_items)
      .default_value(StoneworkFill::Volume)
      .optional_label()
      .description("The space to fill");
  b.add_input<decl::Geometry>("Own Stones")
      .supported_type({GeometryComponent::Type::Mesh, GeometryComponent::Type::Instance})
      .description("Your own stones. Each loose part is one stone. Leave empty for built-in "
                   "stones");
  b.add_input<decl::Menu>("Shape")
      .static_items(shape_items)
      .default_value(StoneworkShape::DryWall)
      .optional_label()
      .description("The shape of the built-in stones");

  PanelDeclarationBuilder &stones = b.add_panel("Stones").default_closed(false);
  stones.add_input<decl::Float>("Stone Size")
      .default_value(0.2f)
      .min(0.001f)
      .subtype(PROP_DISTANCE)
      .description("About half the width of a built-in stone");
  stones.add_input<decl::Float>("Size Variation")
      .default_value(0.35f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("How much the stones differ in size");
  stones.add_input<decl::Float>("Flatness")
      .default_value(0.5f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("Chunky stones at 0, flat ones at 1");
  stones.add_input<decl::Float>("Roughness")
      .default_value(0.3f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("Bumps on the built-in stones");
  stones.add_input<decl::Int>("Detail")
      .default_value(2)
      .min(1)
      .max(4)
      .description("How many faces each built-in stone has");
  stones.add_input<decl::Float>("Scale Min")
      .default_value(1.0f)
      .min(0.0f)
      .description("Smallest random size of a finished stone");
  stones.add_input<decl::Float>("Scale Max")
      .default_value(1.0f)
      .min(0.0f)
      .description("Largest random size of a finished stone");

  PanelDeclarationBuilder &fit = b.add_panel("Fit").default_closed(false);
  fit.add_input<decl::Float>("Gap")
      .default_value(0.005f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .description("Space between stones");
  fit.add_input<decl::Float>("Close Gaps")
      .default_value(1.0f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("Grow the stones into the empty space around them");
  fit.add_input<decl::Float>("Poke Out")
      .default_value(0.0f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .description("How far stones may stick out past the surface. Zero keeps them inside");
  fit.add_input<decl::Bool>("Filler Stones")
      .default_value(true)
      .description("Fill the holes left at the end with smaller stones");
  fit.add_input<decl::Int>("Max Stones")
      .default_value(2000)
      .min(1)
      .max(100000)
      .description("Stop after this many stones");
  fit.add_input<decl::Int>("Seed").description("Random seed");

  PanelDeclarationBuilder &mortar = b.add_panel("Mortar").default_closed(true);
  mortar.add_input<decl::Bool>("Mortar")
      .default_value(false)
      .panel_toggle()
      .description("Add mortar: the filled shape, pulled in by the depth");
  mortar.add_input<decl::Float>("Depth")
      .default_value(0.05f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .description("How far the mortar sits below the surface");
  mortar.add_input<decl::Menu>("Remesh")
      .static_items(remesh_items)
      .default_value(StoneworkRemesh::Isotropic)
      .optional_label()
      .description("How the mortar is made dense");
  mortar.add_input<decl::Float>("Resolution")
      .default_value(0.05f)
      .min(0.001f)
      .subtype(PROP_DISTANCE)
      .description("Edge length, voxel size or grid size of the mortar");
  mortar.add_input<decl::Bool>("Merge")
      .default_value(false)
      .description("Join the stones and the mortar into one closed surface, like a boolean");

  PanelDeclarationBuilder &color = b.add_panel("Color").default_closed(true);
  color.add_input<decl::Menu>("Color Mode")
      .static_items(color_mode_items)
      .default_value(StoneworkColorMode::Hue)
      .optional_label()
      .description("What kind of random colors the Color output has");
  auto range = [&](const char *name, const float value, const char *description) {
    color.add_input<decl::Float>(name)
        .default_value(value)
        .min(0.0f)
        .max(1.0f)
        .subtype(PROP_FACTOR)
        .description(description);
  };
  range("Hue Min", 0.0f, "Lowest hue");
  range("Hue Max", 1.0f, "Highest hue");
  range("Saturation Min", 0.45f, "Lowest saturation");
  range("Saturation Max", 0.85f, "Highest saturation");
  range("Value Min", 0.65f, "Darkest value");
  range("Value Max", 0.95f, "Brightest value");
}

static void node_geo_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");
  GeometrySet own = params.extract_input<GeometrySet>("Own Stones");

  geometry::StoneworkParams settings;
  settings.fill = params.extract_input<StoneworkFill>("Fill");
  settings.shape = params.extract_input<StoneworkShape>("Shape");
  settings.stone_size = params.extract_input<float>("Stone Size");
  settings.size_variation = params.extract_input<float>("Size Variation");
  settings.flatness = params.extract_input<float>("Flatness");
  settings.roughness = params.extract_input<float>("Roughness");
  settings.detail = params.extract_input<int>("Detail");
  settings.gap = params.extract_input<float>("Gap");
  settings.poke_out = params.extract_input<float>("Poke Out");
  settings.fillers = params.extract_input<bool>("Filler Stones");
  settings.max_stones = params.extract_input<int>("Max Stones");
  settings.seed = params.extract_input<int>("Seed");
  settings.close_gaps = params.extract_input<float>("Close Gaps");
  settings.scale_min = params.extract_input<float>("Scale Min");
  settings.scale_max = params.extract_input<float>("Scale Max");
  settings.color_mode = params.extract_input<StoneworkColorMode>("Color Mode");
  settings.hue_min = params.extract_input<float>("Hue Min");
  settings.hue_max = params.extract_input<float>("Hue Max");
  settings.saturation_min = params.extract_input<float>("Saturation Min");
  settings.saturation_max = params.extract_input<float>("Saturation Max");
  settings.value_min = params.extract_input<float>("Value Min");
  settings.value_max = params.extract_input<float>("Value Max");
  settings.mortar = params.extract_input<bool>("Mortar");
  settings.mortar_depth = params.extract_input<float>("Depth");
  settings.mortar_remesh = params.extract_input<StoneworkRemesh>("Remesh");
  settings.mortar_resolution = params.extract_input<float>("Resolution");
  settings.merge = params.extract_input<bool>("Merge");
  settings.color_id = params.get_output_anonymous_attribute_id_if_needed("Color");
  settings.mortar_id = params.get_output_anonymous_attribute_id_if_needed("Mortar");
  settings.stone_id_id = params.get_output_anonymous_attribute_id_if_needed("Stone ID");
  settings.random_id = params.get_output_anonymous_attribute_id_if_needed("Random");

  Vector<const Mesh *> custom;
  if (own.has_instances()) {
    own = geometry::realize_instances(std::move(own), geometry::RealizeInstancesOptions())
              .geometry;
  }
  if (const Mesh *own_mesh = own.get_mesh()) {
    custom.append(own_mesh);
  }
  settings.custom_stones = custom;

  geometry::foreach_real_geometry(geometry_set, [&](GeometrySet &geometry_set) {
    const Mesh *src_mesh = geometry_set.get_mesh();
    if (src_mesh == nullptr) {
      return;
    }
    std::string message;
    Mesh *result = geometry::mesh_stonework(*src_mesh, settings, nullptr, &message);
    if (!message.empty()) {
      params.error_message_add(NodeWarningType::Warning, message);
    }
    if (result == nullptr) {
      geometry_set.remove<MeshComponent>();
      return;
    }
    geometry::debug_randomize_mesh_order(result);
    geometry_set.replace_mesh(result);
  });
  params.set_output("Stones", std::move(geometry_set));
}

static void node_register()
{
  static blender::bke::bNodeType ntype;

  geo_node_type_base(&ntype, "GeometryNodeStonework");
  ntype.ui_name = "Stonework";
  ntype.ui_description = "Fill a mesh with whole stones, stacked one at a time from the bottom up";
  ntype.nclass = NODE_CLASS_GEOMETRY;
  ntype.declare = node_declare;
  ntype.geometry_node_execute = node_geo_exec;
  blender::bke::node_register_type(ntype);
}
NOD_REGISTER_NODE(node_register)

}  // namespace blender::nodes::node_geo_stonework_cc
