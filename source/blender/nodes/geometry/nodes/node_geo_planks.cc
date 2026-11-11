/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <mutex>

#include "DNA_mesh_types.h"

#include "BKE_mesh.hh"

#include "GEO_foreach_geometry.hh"
#include "GEO_mesh_planks.hh"
#include "GEO_randomize.hh"

#include "UI_resources.hh"

#include "node_geometry_util.hh"

namespace blender::nodes::node_geo_planks_cc {

using geometry::PlanksBarrelEnds;
using geometry::PlanksBrace;
using geometry::PlanksJoints;
using geometry::PlanksLid;
using geometry::PlanksPattern;
using geometry::PlanksType;
using geometry::PlanksWallStyle;

static const EnumPropertyItem type_items[] = {
    {int(PlanksType::Floor), "FLOOR", ICON_NONE, N_("Floor"), N_("Boards laid on the faces")},
    {int(PlanksType::Wall), "WALL", ICON_NONE, N_("Wall"), N_("Boards on the faces, as siding")},
    {int(PlanksType::Crate), "CRATE", ICON_NONE, N_("Crate"), N_("A box made of boards")},
    {int(PlanksType::Barrel), "BARREL", ICON_NONE, N_("Barrel"), N_("A barrel made of staves")},
    {0, nullptr, 0, nullptr, nullptr},
};

static const EnumPropertyItem pattern_items[] = {
    {int(PlanksPattern::Straight), "STRAIGHT", ICON_NONE, N_("Straight"), N_("Boards in rows")},
    {int(PlanksPattern::Herringbone),
     "HERRINGBONE",
     ICON_NONE,
     N_("Herringbone"),
     N_("Boards in a zigzag, each at a right angle to the next")},
    {int(PlanksPattern::Chevron),
     "CHEVRON",
     ICON_NONE,
     N_("Chevron"),
     N_("Slanted boards meeting in V shapes")},
    {int(PlanksPattern::Basket),
     "BASKET",
     ICON_NONE,
     N_("Basket"),
     N_("Squares of boards laid across and along in turns")},
    {0, nullptr, 0, nullptr, nullptr},
};

static const EnumPropertyItem joints_items[] = {
    {int(PlanksJoints::Random), "RANDOM", ICON_NONE, N_("Random"), N_("Joints anywhere")},
    {int(PlanksJoints::Half), "HALF", ICON_NONE, N_("Half"), N_("Each row moved by half a board")},
    {int(PlanksJoints::Third), "THIRD", ICON_NONE, N_("Third"), N_("Each row moved by a third")},
    {0, nullptr, 0, nullptr, nullptr},
};

static const EnumPropertyItem wall_style_items[] = {
    {int(PlanksWallStyle::Horizontal), "HORIZONTAL", ICON_NONE, N_("Horizontal"), N_("Level boards")},
    {int(PlanksWallStyle::Vertical), "VERTICAL", ICON_NONE, N_("Vertical"), N_("Upright boards")},
    {int(PlanksWallStyle::Lap),
     "LAP",
     ICON_NONE,
     N_("Lap"),
     N_("Level boards, each lying over the top of the one below")},
    {int(PlanksWallStyle::BoardAndBatten),
     "BOARD_AND_BATTEN",
     ICON_NONE,
     N_("Board and Batten"),
     N_("Upright boards with thin strips over the joints")},
    {0, nullptr, 0, nullptr, nullptr},
};

static const EnumPropertyItem brace_items[] = {
    {int(PlanksBrace::None), "NONE", ICON_NONE, N_("None"), N_("No brace")},
    {int(PlanksBrace::Diagonal), "DIAGONAL", ICON_NONE, N_("Diagonal"), N_("One diagonal board")},
    {int(PlanksBrace::Z), "Z", ICON_NONE, N_("Z"), N_("Boards along the top and bottom with a diagonal between them")},
    {0, nullptr, 0, nullptr, nullptr},
};

static const EnumPropertyItem lid_items[] = {
    {int(PlanksLid::Closed), "CLOSED", ICON_NONE, N_("Closed"), N_("A lid on top")},
    {int(PlanksLid::Open), "OPEN", ICON_NONE, N_("Open"), N_("A lid turned open")},
    {int(PlanksLid::None), "NONE", ICON_NONE, N_("None"), N_("No lid")},
    {0, nullptr, 0, nullptr, nullptr},
};

static const EnumPropertyItem ends_items[] = {
    {int(PlanksBarrelEnds::Both), "BOTH", ICON_NONE, N_("Both"), N_("Boards at the top and bottom")},
    {int(PlanksBarrelEnds::Bottom), "BOTTOM", ICON_NONE, N_("Bottom"), N_("Open at the top")},
    {int(PlanksBarrelEnds::None), "NONE", ICON_NONE, N_("None"), N_("Open at both ends")},
    {0, nullptr, 0, nullptr, nullptr},
};

static void node_declare(NodeDeclarationBuilder &b)
{
  b.use_custom_socket_order();
  b.allow_any_socket_order();

  const int floor = int(PlanksType::Floor);
  const int wall = int(PlanksType::Wall);
  const int crate = int(PlanksType::Crate);
  const int barrel = int(PlanksType::Barrel);

  b.add_input<decl::Geometry>("Mesh")
      .supported_type(GeometryComponent::Type::Mesh)
      .is_default_link_socket()
      .description("Faces to cover, for floors and walls");
  b.add_output<decl::Geometry>("Mesh").propagate_all().align_with_previous();
  b.add_output<decl::Int>("Board Index")
      .field_on_all()
      .description("Number of the board of each point, -1 on the faces kept from the input");
  b.add_output<decl::Float>("Random").field_on_all().description(
      "A random value from 0 to 1 per board");
  b.add_output<decl::Int>("Part").field_on_all().description(
      "0 on boards, 1 on joists, studs and frames, 2 on hoops, -1 on the input");

  b.add_input<decl::Menu>("Type")
      .static_items(type_items)
      .default_value(PlanksType::Floor)
      .optional_label()
      .description("What to make");
  b.add_input<decl::Bool>("Selection")
      .default_value(true)
      .hide_value()
      .field_on_all()
      .usage_by_menu("Type", {floor, wall})
      .description("Faces that get boards");
  b.add_input<decl::Float>("Width")
      .default_value(0.12f)
      .min(0.0001f)
      .subtype(PROP_DISTANCE)
      .description("Width of a board");
  b.add_input<decl::Float>("Thickness")
      .default_value(0.02f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .description("Thickness of a board");
  b.add_input<decl::Float>("Gap")
      .default_value(0.004f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .description("Space between the boards");

  b.add_input<decl::Menu>("Pattern")
      .static_items(pattern_items)
      .default_value(PlanksPattern::Straight)
      .usage_by_menu("Type", floor)
      .description("How the floor boards are laid");
  b.add_input<decl::Menu>("Style")
      .static_items(wall_style_items)
      .default_value(PlanksWallStyle::Horizontal)
      .usage_by_menu("Type", wall)
      .description("How the wall boards are put up");
  b.add_input<decl::Float>("Length")
      .default_value(1.2f)
      .min(0.0001f)
      .subtype(PROP_DISTANCE)
      .usage_by_menu("Type", {floor, wall})
      .description("Length of a board");
  b.add_input<decl::Float>("Random Length")
      .default_value(0.3f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .usage_by_menu("Type", {floor, wall})
      .description("Change the length of each board");
  b.add_input<decl::Float>("Shortest Piece")
      .default_value(0.2f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .usage_by_menu("Type", {floor, wall})
      .description("No piece at the end of a row is shorter than this");
  b.add_input<decl::Menu>("Joints")
      .static_items(joints_items)
      .default_value(PlanksJoints::Random)
      .usage_by_menu("Type", {floor, wall})
      .description("Where the joints of each row are");
  b.add_input<decl::Float>("Lap")
      .default_value(0.03f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .usage_by_menu("Type", wall)
      .description("Lap style: how much each board covers the one below");
  b.add_input<decl::Float>("Batten Width")
      .default_value(0.05f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .usage_by_menu("Type", wall)
      .description("Board and batten style: width of the strips over the joints");
  b.add_input<decl::Float>("Angle")
      .subtype(PROP_ANGLE)
      .usage_by_menu("Type", {floor, wall})
      .description("Turn the rows on the faces");
  b.add_input<decl::Bool>("Border")
      .default_value(false)
      .usage_by_menu("Type", floor)
      .description("A ring of boards along the outline. Only on faces with one convex outline");
  b.add_input<decl::Bool>("Keep Surface")
      .default_value(true)
      .usage_by_menu("Type", {floor, wall})
      .description("Keep the faces under the boards");
  b.add_input<decl::Bool>("Supports")
      .default_value(false)
      .usage_by_menu("Type", {floor, wall})
      .description("Joists under floors, studs behind walls");
  b.add_input<decl::Float>("Support Spacing")
      .default_value(0.4f)
      .min(0.001f)
      .subtype(PROP_DISTANCE)
      .usage_by_menu("Type", {floor, wall})
      .description("Distance between the joists or studs");
  b.add_input<decl::Float>("Support Width")
      .default_value(0.05f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .usage_by_menu("Type", {floor, wall})
      .description("Width of the joists or studs");
  b.add_input<decl::Float>("Support Depth")
      .default_value(0.15f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .usage_by_menu("Type", {floor, wall})
      .description("Depth of the joists or studs");

  b.add_input<decl::Vector>("Size")
      .default_value(float3(0.8f, 0.6f, 0.6f))
      .subtype(PROP_XYZ)
      .usage_by_menu("Type", crate)
      .description("Size of the crate. Its bottom is at 0");
  b.add_input<decl::Bool>("Slatted")
      .default_value(false)
      .usage_by_menu("Type", crate)
      .description("Wide gaps between the boards");
  b.add_input<decl::Float>("Slat Gap")
      .default_value(0.03f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .usage_by_menu("Type", crate)
      .description("Gap between the boards when slatted");
  b.add_input<decl::Bool>("Frame")
      .default_value(true)
      .usage_by_menu("Type", crate)
      .description("Boards along the edges of each side");
  b.add_input<decl::Menu>("Brace")
      .static_items(brace_items)
      .default_value(PlanksBrace::Diagonal)
      .usage_by_menu("Type", crate)
      .description("Boards across each side");
  b.add_input<decl::Bool>("Corner Posts")
      .default_value(false)
      .usage_by_menu("Type", crate)
      .description("Square posts at the corners");
  b.add_input<decl::Float>("Post Width")
      .default_value(0.06f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .usage_by_menu("Type", crate)
      .description("Width of the corner posts");
  b.add_input<decl::Menu>("Lid")
      .static_items(lid_items)
      .default_value(PlanksLid::Closed)
      .usage_by_menu("Type", crate)
      .description("The top of the crate");
  b.add_input<decl::Float>("Lid Angle")
      .default_value(1.2f)
      .subtype(PROP_ANGLE)
      .usage_by_menu("Type", crate)
      .description("How far an open lid is turned");

  b.add_input<decl::Float>("Height")
      .default_value(0.9f)
      .min(0.001f)
      .subtype(PROP_DISTANCE)
      .usage_by_menu("Type", barrel)
      .description("Height of the barrel. Its bottom is at 0");
  b.add_input<decl::Float>("Radius")
      .default_value(0.28f)
      .min(0.001f)
      .subtype(PROP_DISTANCE)
      .usage_by_menu("Type", barrel)
      .description("Radius at the ends");
  b.add_input<decl::Float>("Bulge")
      .default_value(0.06f)
      .subtype(PROP_DISTANCE)
      .usage_by_menu("Type", barrel)
      .description("How much wider the middle is");
  b.add_input<decl::Int>("Staves")
      .default_value(20)
      .min(3)
      .max(1000)
      .usage_by_menu("Type", barrel)
      .description("Number of boards around");
  b.add_input<decl::Menu>("Ends")
      .static_items(ends_items)
      .default_value(PlanksBarrelEnds::Both)
      .usage_by_menu("Type", barrel)
      .description("Which ends are closed with boards");
  b.add_input<decl::Float>("Chime")
      .default_value(0.03f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .usage_by_menu("Type", barrel)
      .description("How far the staves reach past the end boards");
  b.add_input<decl::Int>("Hoops")
      .default_value(4)
      .min(0)
      .max(100)
      .usage_by_menu("Type", barrel)
      .description("Number of metal bands");
  b.add_input<decl::Float>("Hoop Width")
      .default_value(0.04f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .usage_by_menu("Type", barrel)
      .description("Width of a band");
  b.add_input<decl::Float>("Hoop Thickness")
      .default_value(0.006f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .usage_by_menu("Type", barrel)
      .description("Thickness of a band");
  b.add_input<decl::Float>("Hoop Offset")
      .default_value(0.12f)
      .min(0.0f)
      .max(0.5f)
      .subtype(PROP_FACTOR)
      .usage_by_menu("Type", barrel)
      .description("How far the outer bands are from the ends, as a share of the height");

  PanelDeclarationBuilder &variation = b.add_panel("Variation").default_closed(true);
  variation.add_input<decl::Float>("Random Height")
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .description("Largest random move of a board out of the surface");
  variation.add_input<decl::Float>("Tilt")
      .min(0.0f)
      .max(M_PI_2)
      .subtype(PROP_ANGLE)
      .description("Largest random tilt of a board");
  variation.add_input<decl::Float>("Cup")
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .description("How much the long edges of each board rise over its middle");
  variation.add_input<decl::Float>("Bow")
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .description("How much the middle of each board rises over its ends");
  variation.add_input<decl::Float>("Missing")
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("Share of boards left out");
  variation.add_input<decl::Int>("Seed").description("Changes the random values");

  PanelDeclarationBuilder &materials = b.add_panel("Materials").default_closed(true);
  materials.add_input<decl::Int>("Material Index")
      .default_value(-1)
      .min(-1)
      .description("First material of the boards. -1 keeps the material of the face");
  materials.add_input<decl::Int>("Material Count")
      .default_value(1)
      .min(1)
      .description("Boards pick a material at random from this many");
  materials.add_input<decl::Int>("Metal Material Index")
      .default_value(-1)
      .min(-1)
      .description("Material of the hoops. -1 for the boards' material");
}

static void node_geo_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");
  const Field<bool> selection_field = params.extract_input<Field<bool>>("Selection");

  geometry::MeshPlanksParams settings;
  settings.type = params.extract_input<PlanksType>("Type");
  settings.width = params.extract_input<float>("Width");
  settings.thickness = params.extract_input<float>("Thickness");
  settings.gap = params.extract_input<float>("Gap");
  settings.pattern = params.extract_input<PlanksPattern>("Pattern");
  settings.wall_style = params.extract_input<PlanksWallStyle>("Style");
  settings.length = params.extract_input<float>("Length");
  settings.random_length = params.extract_input<float>("Random Length");
  settings.min_length = params.extract_input<float>("Shortest Piece");
  settings.joints = params.extract_input<PlanksJoints>("Joints");
  settings.lap = params.extract_input<float>("Lap");
  settings.batten_width = params.extract_input<float>("Batten Width");
  settings.angle = params.extract_input<float>("Angle");
  settings.border = params.extract_input<bool>("Border");
  settings.keep_surface = params.extract_input<bool>("Keep Surface");
  settings.supports = params.extract_input<bool>("Supports");
  settings.support_spacing = params.extract_input<float>("Support Spacing");
  settings.support_width = params.extract_input<float>("Support Width");
  settings.support_depth = params.extract_input<float>("Support Depth");
  settings.crate_size = params.extract_input<float3>("Size");
  settings.slatted = params.extract_input<bool>("Slatted");
  settings.slat_gap = params.extract_input<float>("Slat Gap");
  settings.frame = params.extract_input<bool>("Frame");
  settings.brace = params.extract_input<PlanksBrace>("Brace");
  settings.corner_posts = params.extract_input<bool>("Corner Posts");
  settings.post_width = params.extract_input<float>("Post Width");
  settings.lid = params.extract_input<PlanksLid>("Lid");
  settings.lid_angle = params.extract_input<float>("Lid Angle");
  settings.barrel_height = params.extract_input<float>("Height");
  settings.barrel_radius = params.extract_input<float>("Radius");
  settings.bulge = params.extract_input<float>("Bulge");
  settings.staves = params.extract_input<int>("Staves");
  settings.barrel_ends = params.extract_input<PlanksBarrelEnds>("Ends");
  settings.chime = params.extract_input<float>("Chime");
  settings.hoops = params.extract_input<int>("Hoops");
  settings.hoop_width = params.extract_input<float>("Hoop Width");
  settings.hoop_thickness = params.extract_input<float>("Hoop Thickness");
  settings.hoop_offset = params.extract_input<float>("Hoop Offset");
  settings.random_height = params.extract_input<float>("Random Height");
  settings.tilt = params.extract_input<float>("Tilt");
  settings.cup = params.extract_input<float>("Cup");
  settings.bow = params.extract_input<float>("Bow");
  settings.missing = params.extract_input<float>("Missing");
  settings.seed = params.extract_input<int>("Seed");
  settings.material = params.extract_input<int>("Material Index");
  settings.material_count = params.extract_input<int>("Material Count");
  settings.metal_material = params.extract_input<int>("Metal Material Index");
  settings.board_index_id = params.get_output_anonymous_attribute_id_if_needed("Board Index");
  settings.random_id = params.get_output_anonymous_attribute_id_if_needed("Random");
  settings.part_id = params.get_output_anonymous_attribute_id_if_needed("Part");

  std::string message;
  if (ELEM(settings.type, PlanksType::Crate, PlanksType::Barrel)) {
    Mesh *mesh = geometry::mesh_planks(nullptr, settings, message);
    if (mesh != nullptr) {
      geometry::debug_randomize_mesh_order(mesh);
    }
    if (!message.empty()) {
      params.error_message_add(NodeWarningType::Warning, message);
    }
    params.set_output("Mesh", GeometrySet::from_mesh(mesh));
    return;
  }

  std::mutex message_mutex;
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
    geometry::MeshPlanksParams mesh_settings = settings;
    mesh_settings.selection = selection;
    std::string mesh_message;
    Mesh *result = geometry::mesh_planks(src_mesh, mesh_settings, mesh_message);
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

  geo_node_type_base(&ntype, "GeometryNodePlanks");
  ntype.ui_name = "Planks";
  ntype.ui_description = "Floors and walls of boards on faces, and crates and barrels of boards";
  ntype.nclass = NODE_CLASS_GEOMETRY;
  ntype.declare = node_declare;
  ntype.geometry_node_execute = node_geo_exec;
  blender::bke::node_register_type(ntype);
}
NOD_REGISTER_NODE(node_register)

}  // namespace blender::nodes::node_geo_planks_cc
