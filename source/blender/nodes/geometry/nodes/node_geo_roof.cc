/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <mutex>

#include "DNA_mesh_types.h"

#include "BKE_mesh.hh"

#include "GEO_foreach_geometry.hh"
#include "GEO_mesh_roof.hh"
#include "GEO_randomize.hh"

#include "UI_resources.hh"

#include "node_geometry_util.hh"

namespace blender::nodes::node_geo_roof_cc {

using geometry::RoofCovering;
using geometry::RoofMode;
using geometry::RoofShape;

static const EnumPropertyItem mode_items[] = {
    {int(RoofMode::Build),
     "BUILD",
     ICON_NONE,
     N_("Build"),
     N_("Build a roof on each group of faces facing up")},
    {int(RoofMode::Cover),
     "COVER",
     ICON_NONE,
     N_("Cover"),
     N_("The faces are the roof: add overhangs, covering and trim")},
    {0, nullptr, 0, nullptr, nullptr},
};

static const EnumPropertyItem shape_items[] = {
    {int(RoofShape::Gable), "GABLE", ICON_NONE, N_("Gable"), N_("Two slopes meeting at a ridge")},
    {int(RoofShape::Hip), "HIP", ICON_NONE, N_("Hip"), N_("Slopes on all four sides")},
    {int(RoofShape::Shed), "SHED", ICON_NONE, N_("Shed"), N_("One slope")},
    {int(RoofShape::Flat), "FLAT", ICON_NONE, N_("Flat"), N_("Flat, with low walls along the edge")},
    {int(RoofShape::Pyramid), "PYRAMID", ICON_NONE, N_("Pyramid"), N_("Slopes meeting at a point")},
    {int(RoofShape::Gambrel),
     "GAMBREL",
     ICON_NONE,
     N_("Gambrel"),
     N_("Two slopes on each side, the lower one steeper, like a barn")},
    {int(RoofShape::Mansard),
     "MANSARD",
     ICON_NONE,
     N_("Mansard"),
     N_("Steep lower slopes on all four sides, with a low roof on top")},
    {0, nullptr, 0, nullptr, nullptr},
};

static const EnumPropertyItem covering_items[] = {
    {int(RoofCovering::None), "NONE", ICON_NONE, N_("None"), N_("Only the slab")},
    {int(RoofCovering::Tiles), "TILES", ICON_NONE, N_("Tiles"), N_("Overlapping rows of tiles")},
    {int(RoofCovering::Shingles),
     "SHINGLES",
     ICON_NONE,
     N_("Shingles"),
     N_("Thin overlapping rows of mixed widths")},
    {int(RoofCovering::Metal),
     "METAL",
     ICON_NONE,
     N_("Metal"),
     N_("Sheets down the slope with raised seams")},
    {int(RoofCovering::Planks), "PLANKS", ICON_NONE, N_("Planks"), N_("Boards across the slope")},
    {0, nullptr, 0, nullptr, nullptr},
};

static void node_declare(NodeDeclarationBuilder &b)
{
  b.use_custom_socket_order();
  b.allow_any_socket_order();

  const int build = int(RoofMode::Build);
  const int cover = int(RoofMode::Cover);

  b.add_input<decl::Geometry>("Mesh")
      .supported_type(GeometryComponent::Type::Mesh)
      .is_default_link_socket()
      .description("Outlines to build on, or faces to cover");
  b.add_output<decl::Geometry>("Mesh").propagate_all().align_with_previous();
  b.add_output<decl::Int>("Part").field_on_all().description(
      "0 slope, 1 underside, 2 fascia, 3 covering, 4 ridge, 5 valley, 6 gutter, 7 wall, "
      "8 parapet, -1 on the input");
  b.add_output<decl::Float>("Random").field_on_all().description(
      "A random value from 0 to 1 per tile or piece");

  b.add_input<decl::Menu>("Mode")
      .static_items(mode_items)
      .default_value(RoofMode::Build)
      .optional_label()
      .description("Build a new roof, or dress faces that are already a roof");
  b.add_input<decl::Bool>("Selection")
      .default_value(true)
      .hide_value()
      .field_on_all()
      .description("Faces that are used");
  b.add_input<decl::Menu>("Shape")
      .static_items(shape_items)
      .default_value(RoofShape::Gable)
      .usage_by_menu("Mode", build)
      .description("Shape of the roof");
  b.add_input<decl::Float>("Pitch")
      .default_value(0.6108652f)
      .min(0.0f)
      .max(1.5f)
      .subtype(PROP_ANGLE)
      .usage_by_menu("Mode", build)
      .description("Slope of the roof, or of the upper part of Gambrel and Mansard");
  b.add_input<decl::Float>("Lower Pitch")
      .default_value(1.2217305f)
      .min(0.0f)
      .max(1.5f)
      .subtype(PROP_ANGLE)
      .usage_by_menu("Mode", build)
      .description("Slope of the lower part of Gambrel and Mansard");
  b.add_input<decl::Float>("Break")
      .default_value(0.4f)
      .min(0.05f)
      .max(0.95f)
      .subtype(PROP_FACTOR)
      .usage_by_menu("Mode", build)
      .description("How much of the width the lower part of Gambrel and Mansard covers");
  b.add_input<decl::Bool>("Auto Ridge")
      .default_value(true)
      .usage_by_menu("Mode", build)
      .description("Ridge along the longest side of the outline");
  b.add_input<decl::Float>("Ridge Angle")
      .subtype(PROP_ANGLE)
      .usage_by_menu("Mode", build)
      .description("Direction of the ridge from X, without Auto Ridge");
  b.add_input<decl::Float>("Parapet Height")
      .default_value(0.4f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .usage_by_menu("Mode", build)
      .description("Flat roofs: height of the low walls along the edge. 0 for none");
  b.add_input<decl::Float>("Parapet Width")
      .default_value(0.2f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .usage_by_menu("Mode", build)
      .description("Flat roofs: thickness of the low walls");
  b.add_input<decl::Float>("Flat Angle")
      .default_value(0.0872665f)
      .min(0.0f)
      .max(1.5f)
      .subtype(PROP_ANGLE)
      .usage_by_menu("Mode", cover)
      .description("Faces flatter than this are flat roof");
  b.add_input<decl::Bool>("Flat Roofing")
      .default_value(true)
      .usage_by_menu("Mode", cover)
      .description("Cover flat faces with a sheet instead of leaving them out");

  b.add_input<decl::Float>("Eave Overhang")
      .default_value(0.3f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .description("How far the roof reaches past its low edges, measured level");
  b.add_input<decl::Float>("Rake Overhang")
      .default_value(0.2f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .description("How far the roof reaches past its sloped edges");
  b.add_input<decl::Float>("Thickness")
      .default_value(0.12f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .description("Thickness of the roof. In Cover mode only the overhangs get it");

  PanelDeclarationBuilder &covering = b.add_panel("Roofing").default_closed(false);
  covering.add_input<decl::Menu>("Covering")
      .static_items(covering_items)
      .default_value(RoofCovering::Tiles)
      .optional_label()
      .description("What lies on the slopes");
  covering.add_input<decl::Float>("Row Spacing")
      .default_value(0.2f)
      .min(0.001f)
      .subtype(PROP_DISTANCE)
      .description("How much of each row shows, up the slope");
  covering.add_input<decl::Float>("Tile Width")
      .default_value(0.25f)
      .min(0.001f)
      .subtype(PROP_DISTANCE)
      .description("Width of a tile or shingle, or of a metal sheet");
  covering.add_input<decl::Float>("Covering Thickness")
      .default_value(0.02f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .description("Thickness of the tiles");
  covering.add_input<decl::Float>("Overlap")
      .default_value(0.08f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .description("How much each row covers of the row below");
  covering.add_input<decl::Float>("Covering Gap")
      .default_value(0.004f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .description("Space between the tiles of a row");
  covering.add_input<decl::Float>("Random Height")
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .description("Largest random lift of a tile");
  covering.add_input<decl::Float>("Tilt")
      .min(0.0f)
      .max(M_PI_2)
      .subtype(PROP_ANGLE)
      .description("Largest random tilt of a tile");
  covering.add_input<decl::Float>("Missing")
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("Share of tiles left out");
  covering.add_input<decl::Int>("Seed").description("Changes the random values");

  PanelDeclarationBuilder &trim = b.add_panel("Trim").default_closed(true);
  trim.add_input<decl::Bool>("Ridge Caps").default_value(true).description(
      "Caps over the ridges and hips");
  trim.add_input<decl::Float>("Cap Width")
      .default_value(0.25f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .description("Width of the caps");
  trim.add_input<decl::Bool>("Valleys").default_value(true).description(
      "Metal strips in the valleys");
  trim.add_input<decl::Float>("Valley Width")
      .default_value(0.3f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .description("Width of the valley strips");
  trim.add_input<decl::Float>("Fold Angle")
      .default_value(0.1745329f)
      .min(0.0f)
      .max(M_PI)
      .subtype(PROP_ANGLE)
      .description("Edges that fold more than this get caps or valley strips");
  trim.add_input<decl::Float>("Fascia Height")
      .default_value(0.2f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .description("Height of the boards along the outer edges. 0 for none");
  trim.add_input<decl::Float>("Fascia Thickness")
      .default_value(0.03f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .description("Thickness of the boards along the outer edges");
  trim.add_input<decl::Bool>("Gutters").default_value(false).description(
      "Gutters along the low edges");
  trim.add_input<decl::Float>("Gutter Size")
      .default_value(0.12f)
      .min(0.001f)
      .subtype(PROP_DISTANCE)
      .description("Width and depth of the gutters");

  PanelDeclarationBuilder &materials = b.add_panel("Materials").default_closed(true);
  materials.add_input<decl::Int>("Roof Material")
      .default_value(-1)
      .min(-1)
      .description("Material of the slab. -1 keeps the material of the face");
  materials.add_input<decl::Int>("Covering Material")
      .default_value(-1)
      .min(-1)
      .description("Material of the covering. -1 for the first");
  materials.add_input<decl::Int>("Trim Material")
      .default_value(-1)
      .min(-1)
      .description("Material of the caps, valleys, fascia and gutters. -1 for the first");
  materials.add_input<decl::Int>("Wall Material")
      .default_value(-1)
      .min(-1)
      .description("Material of the gable walls and parapets. -1 for the first");
}

static void node_geo_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");
  const Field<bool> selection_field = params.extract_input<Field<bool>>("Selection");

  geometry::MeshRoofParams settings;
  settings.mode = params.extract_input<RoofMode>("Mode");
  settings.shape = params.extract_input<RoofShape>("Shape");
  settings.pitch = params.extract_input<float>("Pitch");
  settings.lower_pitch = params.extract_input<float>("Lower Pitch");
  settings.break_ratio = params.extract_input<float>("Break");
  settings.auto_ridge = params.extract_input<bool>("Auto Ridge");
  settings.ridge_angle = params.extract_input<float>("Ridge Angle");
  settings.parapet_height = params.extract_input<float>("Parapet Height");
  settings.parapet_width = params.extract_input<float>("Parapet Width");
  settings.flat_angle = params.extract_input<float>("Flat Angle");
  settings.flat_roofing = params.extract_input<bool>("Flat Roofing");
  settings.eave_overhang = params.extract_input<float>("Eave Overhang");
  settings.rake_overhang = params.extract_input<float>("Rake Overhang");
  settings.thickness = params.extract_input<float>("Thickness");
  settings.covering = params.extract_input<RoofCovering>("Covering");
  settings.row_spacing = params.extract_input<float>("Row Spacing");
  settings.tile_width = params.extract_input<float>("Tile Width");
  settings.covering_thickness = params.extract_input<float>("Covering Thickness");
  settings.overlap = params.extract_input<float>("Overlap");
  settings.covering_gap = params.extract_input<float>("Covering Gap");
  settings.random_height = params.extract_input<float>("Random Height");
  settings.tilt = params.extract_input<float>("Tilt");
  settings.missing = params.extract_input<float>("Missing");
  settings.seed = params.extract_input<int>("Seed");
  settings.ridge_caps = params.extract_input<bool>("Ridge Caps");
  settings.cap_width = params.extract_input<float>("Cap Width");
  settings.valleys = params.extract_input<bool>("Valleys");
  settings.valley_width = params.extract_input<float>("Valley Width");
  settings.cap_angle = params.extract_input<float>("Fold Angle");
  settings.fascia_height = params.extract_input<float>("Fascia Height");
  settings.fascia_thickness = params.extract_input<float>("Fascia Thickness");
  settings.gutters = params.extract_input<bool>("Gutters");
  settings.gutter_size = params.extract_input<float>("Gutter Size");
  settings.roof_material = params.extract_input<int>("Roof Material");
  settings.covering_material = params.extract_input<int>("Covering Material");
  settings.trim_material = params.extract_input<int>("Trim Material");
  settings.wall_material = params.extract_input<int>("Wall Material");
  settings.part_id = params.get_output_anonymous_attribute_id_if_needed("Part");
  settings.random_id = params.get_output_anonymous_attribute_id_if_needed("Random");

  std::mutex message_mutex;
  std::string message;
  geometry::foreach_real_geometry(geometry_set, [&](GeometrySet &geometry_set) {
    const Mesh *src_mesh = geometry_set.get_mesh();
    if (src_mesh == nullptr || src_mesh->faces_num == 0) {
      return;
    }
    const bke::MeshFieldContext context(*src_mesh, bke::AttrDomain::Face);
    FieldEvaluator evaluator(context, src_mesh->faces_num);
    evaluator.add(selection_field);
    evaluator.evaluate();
    const VArraySpan<bool> selection = evaluator.get_evaluated<bool>(0);
    geometry::MeshRoofParams mesh_settings = settings;
    mesh_settings.selection = selection;
    std::string mesh_message;
    Mesh *result = geometry::mesh_roof(*src_mesh, mesh_settings, mesh_message);
    if (!mesh_message.empty()) {
      std::lock_guard lock{message_mutex};
      message = mesh_message;
    }
    if (result == nullptr) {
      return;
    }
    geometry::debug_randomize_mesh_order(result);
    geometry_set.replace_mesh(result);
  });
  if (!message.empty()) {
    params.error_message_add(NodeWarningType::Warning, message);
  }
  params.set_output("Mesh", std::move(geometry_set));
}

static void node_register()
{
  static blender::bke::bNodeType ntype;

  geo_node_type_base(&ntype, "GeometryNodeRoof");
  ntype.ui_name = "Roof";
  ntype.ui_description =
      "Build a roof on the outlines of faces, or dress faces with covering and trim";
  ntype.nclass = NODE_CLASS_GEOMETRY;
  ntype.declare = node_declare;
  ntype.geometry_node_execute = node_geo_exec;
  blender::bke::node_register_type(ntype);
}
NOD_REGISTER_NODE(node_register)

}  // namespace blender::nodes::node_geo_roof_cc
