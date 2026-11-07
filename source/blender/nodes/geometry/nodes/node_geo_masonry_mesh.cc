/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "DNA_mesh_types.h"

#include "BKE_mesh.hh"

#include "GEO_foreach_geometry.hh"
#include "GEO_mesh_masonry.hh"
#include "GEO_randomize.hh"

#include "UI_resources.hh"

#include "node_geometry_util.hh"

namespace blender::nodes::node_geo_masonry_mesh_cc {

using geometry::MasonryDirection;
using geometry::MasonryPattern;

static const EnumPropertyItem pattern_items[] = {
    {int(MasonryPattern::Running),
     "RUNNING",
     ICON_NONE,
     N_("Running"),
     N_("Each row shifted by half a brick")},
    {int(MasonryPattern::Stack),
     "STACK",
     ICON_NONE,
     N_("Stack"),
     N_("Bricks right on top of each other")},
    {int(MasonryPattern::Flemish),
     "FLEMISH",
     ICON_NONE,
     N_("Flemish"),
     N_("Long and short bricks taking turns in each row")},
    {int(MasonryPattern::English),
     "ENGLISH",
     ICON_NONE,
     N_("English"),
     N_("Rows of long bricks and rows of short bricks taking turns")},
    {int(MasonryPattern::Ashlar),
     "ASHLAR",
     ICON_NONE,
     N_("Ashlar"),
     N_("Cut stones of mixed sizes in rows")},
    {int(MasonryPattern::Ledgestone),
     "LEDGESTONE",
     ICON_NONE,
     N_("Ledgestone"),
     N_("Thin, long stones in rows")},
    {int(MasonryPattern::Rubble),
     "RUBBLE",
     ICON_NONE,
     N_("Rubble"),
     N_("Uneven stones of mixed shapes")},
    {int(MasonryPattern::Herringbone),
     "HERRINGBONE",
     ICON_NONE,
     N_("Herringbone"),
     N_("Bricks in a zigzag, each at a right angle to the next")},
    {int(MasonryPattern::Chevron),
     "CHEVRON",
     ICON_NONE,
     N_("Chevron"),
     N_("Rows of slanted bricks meeting in V shapes")},
    {int(MasonryPattern::BasketWeave),
     "BASKET_WEAVE",
     ICON_NONE,
     N_("Basket Weave"),
     N_("Squares of bricks laid across and along in turns")},
    {int(MasonryPattern::Pinwheel),
     "PINWHEEL",
     ICON_NONE,
     N_("Pinwheel"),
     N_("Four bricks around a small square")},
    {int(MasonryPattern::Versailles),
     "VERSAILLES",
     ICON_NONE,
     N_("Versailles"),
     N_("Squares and rectangles of four sizes")},
    {int(MasonryPattern::Hexagon), "HEXAGON", ICON_NONE, N_("Hexagon"), N_("Six-sided tiles")},
    {int(MasonryPattern::Octagon),
     "OCTAGON",
     ICON_NONE,
     N_("Octagon"),
     N_("Eight-sided tiles with small squares between them")},
    {int(MasonryPattern::Diamond),
     "DIAMOND",
     ICON_NONE,
     N_("Diamond"),
     N_("Diamond-shaped tiles")},
    {int(MasonryPattern::Interlocking),
     "INTERLOCKING",
     ICON_NONE,
     N_("Interlocking"),
     N_("Pavers with wavy sides that fit into each other")},
    {int(MasonryPattern::Cobblestone),
     "COBBLESTONE",
     ICON_NONE,
     N_("Cobblestone"),
     N_("Small stones with cut corners in rough rows")},
    {int(MasonryPattern::Flagstone),
     "FLAGSTONE",
     ICON_NONE,
     N_("Flagstone"),
     N_("Large flat stones of uneven shapes")},
    {int(MasonryPattern::Fan), "FAN", ICON_NONE, N_("Fan"), N_("Stones laid in fan-shaped arcs")},
    {int(MasonryPattern::Radial),
     "RADIAL",
     ICON_NONE,
     N_("Radial"),
     N_("Rings of stones around a center")},
    {int(MasonryPattern::Custom),
     "CUSTOM",
     ICON_NONE,
     N_("Custom"),
     N_("The faces of the Tile mesh, repeated")},
    {0, nullptr, 0, nullptr, nullptr},
};

static const EnumPropertyItem direction_items[] = {
    {int(MasonryDirection::Auto),
     "AUTO",
     ICON_NONE,
     N_("Auto"),
     N_("Level rows on walls, along X on floors")},
    {int(MasonryDirection::WorldX), "X", ICON_NONE, N_("X"), N_("Rows along X")},
    {int(MasonryDirection::WorldY), "Y", ICON_NONE, N_("Y"), N_("Rows along Y")},
    {int(MasonryDirection::Face),
     "FACE",
     ICON_NONE,
     N_("Face"),
     N_("Rows along the longest edge of each face")},
    {0, nullptr, 0, nullptr, nullptr},
};

static void node_declare(NodeDeclarationBuilder &b)
{
  b.use_custom_socket_order();
  b.allow_any_socket_order();

  b.add_input<decl::Geometry>("Mesh")
      .supported_type(GeometryComponent::Type::Mesh)
      .is_default_link_socket()
      .description("Mesh whose faces get blocks");
  b.add_output<decl::Geometry>("Mesh").propagate_all().align_with_previous();
  b.add_output<decl::Int>("Block Index")
      .field_on_all()
      .description("Number of the block of each point, -1 on the faces under the blocks");
  b.add_output<decl::Bool>("Mortar").field_on_all().description("The faces under the blocks");

  b.add_input<decl::Bool>("Selection")
      .default_value(true)
      .hide_value()
      .field_on_all()
      .description("Faces that get blocks");
  b.add_input<decl::Menu>("Pattern")
      .static_items(pattern_items)
      .default_value(MasonryPattern::Running)
      .optional_label()
      .description("How the blocks are laid");
  b.add_input<decl::Geometry>("Tile")
      .only_realized_data()
      .supported_type(GeometryComponent::Type::Mesh)
      .usage_by_menu("Pattern", int(MasonryPattern::Custom))
      .description("Its faces seen from above are the blocks of one repeat of the pattern");
  b.add_input<decl::Vector>("Tile Size")
      .default_value({1.0f, 1.0f})
      .dimensions(2)
      .subtype(PROP_XYZ)
      .usage_by_menu("Pattern", int(MasonryPattern::Custom))
      .description("Size of one repeat of the tile");
  b.add_input<decl::Menu>("Direction")
      .static_items(direction_items)
      .default_value(MasonryDirection::Auto)
      .description("Which way the rows run");
  b.add_input<decl::Float>("Angle").subtype(PROP_ANGLE).description(
      "Turn the pattern on the face");
  b.add_input<decl::Vector>("Center")
      .subtype(PROP_TRANSLATION)
      .usage_by_menu("Pattern", {int(MasonryPattern::Fan), int(MasonryPattern::Radial)})
      .description("Center of the fans or rings");
  b.add_input<decl::Float>("Length")
      .default_value(0.3f)
      .min(0.0001f)
      .subtype(PROP_DISTANCE)
      .description("Size of a block along the rows");
  b.add_input<decl::Float>("Height")
      .default_value(0.1f)
      .min(0.0001f)
      .subtype(PROP_DISTANCE)
      .description("Size of a block across the rows");
  b.add_input<decl::Float>("Depth")
      .default_value(0.05f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .description("How far the blocks stick out of the face");
  b.add_input<decl::Float>("Mortar")
      .default_value(0.01f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .description("Width of the joints between the blocks");
  b.add_input<decl::Float>("Offset")
      .default_value(0.0f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("0 puts the blocks on the face, 1 sinks them in so their tops are at the face");
  b.add_input<decl::Int>("Soldier Rows")
      .default_value(0)
      .min(0)
      .usage_by_menu("Pattern",
                     {int(MasonryPattern::Running),
                      int(MasonryPattern::Stack),
                      int(MasonryPattern::Flemish),
                      int(MasonryPattern::English)})
      .description("After this many rows, a row of upright bricks. 0 for none");
  b.add_input<decl::Bool>("Backing").default_value(true).description(
      "Keep the faces under the blocks, to show as mortar");
  b.add_input<decl::Bool>("Border").default_value(false).description(
      "A row of blocks along the outline of each face. Only on convex faces");
  b.add_input<decl::Bool>("Toothed").default_value(false).description(
      "Blocks at the edges of a face stick out whole instead of being cut");

  PanelDeclarationBuilder &variation = b.add_panel("Variation").default_closed(true);
  variation.add_input<decl::Float>("Random Length")
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("Change the length of each block");
  variation.add_input<decl::Float>("Random Height")
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("Change the height of each row");
  variation.add_input<decl::Float>("Random Depth")
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("Change how far each block sticks out");
  variation.add_input<decl::Float>("Tilt")
      .min(0.0f)
      .max(M_PI_2)
      .subtype(PROP_ANGLE)
      .description("Largest random tilt of a block");
  variation.add_input<decl::Float>("Stone Shape")
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("Make the outlines of the blocks uneven");
  variation.add_input<decl::Float>("Dome")
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("Round the tops of the blocks");
  variation.add_input<decl::Int>("Seed").description("Changes the random values");
}

/** The faces of the tile mesh as outlines in its XY plane. */
static Vector<Vector<float2>> tile_outlines(const GeometrySet &tile)
{
  Vector<Vector<float2>> outlines;
  const Mesh *mesh = tile.get_mesh();
  if (mesh == nullptr) {
    return outlines;
  }
  const Span<float3> positions = mesh->vert_positions();
  const OffsetIndices<int> faces = mesh->faces();
  const Span<int> corner_verts = mesh->corner_verts();
  for (const int face : faces.index_range()) {
    Vector<float2> outline;
    for (const int vert : corner_verts.slice(faces[face])) {
      outline.append(positions[vert].xy());
    }
    outlines.append(std::move(outline));
  }
  return outlines;
}

static void node_geo_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");
  const Field<bool> selection_field = params.extract_input<Field<bool>>("Selection");

  geometry::MeshMasonryParams settings;
  settings.pattern = params.extract_input<MasonryPattern>("Pattern");
  const GeometrySet tile = params.extract_input<GeometrySet>("Tile");
  const float3 tile_size = params.extract_input<float3>("Tile Size");
  if (settings.pattern == MasonryPattern::Custom) {
    settings.tile = tile_outlines(tile);
    settings.tile_size = tile_size.xy();
  }
  settings.direction = params.extract_input<MasonryDirection>("Direction");
  settings.angle = params.extract_input<float>("Angle");
  settings.center = params.extract_input<float3>("Center");
  settings.length = params.extract_input<float>("Length");
  settings.height = params.extract_input<float>("Height");
  settings.depth = std::max(params.extract_input<float>("Depth"), 0.0f);
  settings.mortar = std::max(params.extract_input<float>("Mortar"), 0.0f);
  settings.offset = params.extract_input<float>("Offset");
  settings.soldier_rows = std::max(params.extract_input<int>("Soldier Rows"), 0);
  settings.backing = params.extract_input<bool>("Backing");
  settings.border = params.extract_input<bool>("Border");
  settings.toothed = params.extract_input<bool>("Toothed");
  settings.random_length = params.extract_input<float>("Random Length");
  settings.random_height = params.extract_input<float>("Random Height");
  settings.random_depth = params.extract_input<float>("Random Depth");
  settings.tilt = params.extract_input<float>("Tilt");
  settings.stone_shape = params.extract_input<float>("Stone Shape");
  settings.dome = params.extract_input<float>("Dome");
  settings.seed = params.extract_input<int>("Seed");
  settings.block_index_id = params.get_output_anonymous_attribute_id_if_needed("Block Index");
  settings.mortar_id = params.get_output_anonymous_attribute_id_if_needed("Mortar");

  if (settings.pattern == MasonryPattern::Custom && settings.tile.is_empty()) {
    params.set_output("Mesh", std::move(geometry_set));
    return;
  }

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

    geometry::MeshMasonryParams mesh_settings = settings;
    mesh_settings.selection = selection;
    Mesh *result = geometry::mesh_masonry(*src_mesh, mesh_settings);
    if (result == nullptr) {
      return;
    }
    geometry::debug_randomize_mesh_order(result);
    geometry_set.replace_mesh(result);
  });

  params.set_output("Mesh", std::move(geometry_set));
}

static void node_register()
{
  static blender::bke::bNodeType ntype;

  geo_node_type_base(&ntype, "GeometryNodeMasonryMesh");
  ntype.ui_name = "Masonry";
  ntype.ui_description = "Cover faces with blocks laid in a brick, stone or paving pattern";
  ntype.nclass = NODE_CLASS_GEOMETRY;
  ntype.declare = node_declare;
  ntype.geometry_node_execute = node_geo_exec;
  blender::bke::node_register_type(ntype);
}
NOD_REGISTER_NODE(node_register)

}  // namespace blender::nodes::node_geo_masonry_mesh_cc
