/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "BLI_math_base.h"
#include "DNA_mesh_types.h"

#include "BKE_material.hh"
#include "BKE_mesh.hh"

#include "GEO_foreach_geometry.hh"
#include "GEO_mesh_carve.hh"

#include "UI_resources.hh"

#include "node_geometry_util.hh"

namespace blender::nodes::node_geo_carve_cc {

using geometry::CarveBlend;
using geometry::CarveCurvature;
using geometry::CarveDirection;
using geometry::CarvePattern;

enum class LayerSource : int8_t { Pattern = 0, Value = 1 };

static const EnumPropertyItem source_items[] = {
    {int(LayerSource::Pattern), "PATTERN", ICON_NONE, N_("Pattern"), N_("A built-in pattern")},
    {int(LayerSource::Value),
     "VALUE",
     ICON_NONE,
     N_("Value"),
     N_("A grayscale value per point, from a texture node or an attribute")},
    {0, nullptr, 0, nullptr, nullptr},
};

static const EnumPropertyItem pattern_items[] = {
    {int(CarvePattern::Noise), "NOISE", ICON_NONE, N_("Noise"), N_("Smooth bumps")},
    {int(CarvePattern::Voronoi), "VORONOI", ICON_NONE, N_("Voronoi"), N_("Cracked cells")},
    {int(CarvePattern::RandomStone),
     "STONE_WALL",
     ICON_NONE,
     N_("Stone Wall"),
     N_("Rows of uneven stones with mortar between them")},
    {int(CarvePattern::CoursedBlocks),
     "COURSED_BLOCKS",
     ICON_NONE,
     N_("Coursed Blocks"),
     N_("Squared blocks in even courses")},
    {int(CarvePattern::Slate), "SLATE", ICON_NONE, N_("Slate"), N_("Thin, long stones laid flat")},
    {int(CarvePattern::DryStone),
     "DRY_STONE",
     ICON_NONE,
     N_("Dry Stone"),
     N_("Thin stones stacked tight, with small gaps")},
    {int(CarvePattern::Banded),
     "BANDED",
     ICON_NONE,
     N_("Banded"),
     N_("Small stones with a course of long stones every few courses")},
    {int(CarvePattern::Fieldstone),
     "FIELDSTONE",
     ICON_NONE,
     N_("Fieldstone"),
     N_("Round stones with wide joints")},
    {int(CarvePattern::Rubble), "RUBBLE", ICON_NONE, N_("Rubble"), N_("Large, irregular stones")},
    {int(CarvePattern::Cobblestone),
     "COBBLESTONE",
     ICON_NONE,
     N_("Cobblestone"),
     N_("Rounded stones of about the same size")},
    {int(CarvePattern::Ashlar),
     "ASHLAR",
     ICON_NONE,
     N_("Ashlar"),
     N_("Square and long blocks of mixed sizes")},
    {int(CarvePattern::Bricks), "BRICKS", ICON_NONE, N_("Bricks"), N_("Even rows of bricks")},
    {int(CarvePattern::FittedRubble),
     "FITTED_RUBBLE",
     ICON_NONE,
     N_("Fitted Rubble"),
     N_("Irregular stones that fit their neighbors, some split in pieces")},
    {int(CarvePattern::RoughCourses),
     "ROUGH_COURSES",
     ICON_NONE,
     N_("Rough Courses"),
     N_("Rough stones in wandering courses, fitted to each other")},
    {int(CarvePattern::SquaredStone),
     "SQUARED_STONE",
     ICON_NONE,
     N_("Squared Stone"),
     N_("Squared stones in even courses, fitted tight")},
    {int(CarvePattern::MixedRubble),
     "MIXED_RUBBLE",
     ICON_NONE,
     N_("Mixed Rubble"),
     N_("Big stones broken into pieces of mixed sizes")},
    {int(CarvePattern::WornStone),
     "WORN_STONE",
     ICON_NONE,
     N_("Worn Stone"),
     N_("Fitted stones with well worn, round corners")},
    {int(CarvePattern::ThinCourses),
     "THIN_COURSES",
     ICON_NONE,
     N_("Thin Courses"),
     N_("Thin, long stones fitted in many courses")},
    {int(CarvePattern::PackedRound),
     "ROUND_STONES",
     ICON_NONE,
     N_("Round Stones"),
     N_("Round stones stacked one by one")},
    {int(CarvePattern::PackedAngular),
     "ANGULAR_STONES",
     ICON_NONE,
     N_("Angular Stones"),
     N_("Angular stones stacked one by one")},
    {int(CarvePattern::PackedBlocks),
     "BLOCKS",
     ICON_NONE,
     N_("Blocks"),
     N_("Rough blocks stacked one by one")},
    {int(CarvePattern::PackedSlabs),
     "SLABS",
     ICON_NONE,
     N_("Slabs"),
     N_("Long, thin slabs stacked one by one")},
    {int(CarvePattern::PackedWedges),
     "WEDGES",
     ICON_NONE,
     N_("Wedges"),
     N_("Broken, wedge shaped stones stacked one by one")},
    {int(CarvePattern::PackedMixed),
     "MIXED_STONES",
     ICON_NONE,
     N_("Mixed Stones"),
     N_("Stones of every shape stacked one by one")},
    {int(CarvePattern::DryWall),
     "DRY_WALL",
     ICON_NONE,
     N_("Dry Wall"),
     N_("Blocks and slabs stacked flat, with wedges in the gaps")},
    {int(CarvePattern::Waves), "WAVES", ICON_NONE, N_("Waves"), N_("Bands")},
    {0, nullptr, 0, nullptr, nullptr},
};

static const EnumPropertyItem blend_items[] = {
    {int(CarveBlend::Mix), "MIX", ICON_NONE, N_("Mix"), N_("Mix B over A")},
    {int(CarveBlend::Add), "ADD", ICON_NONE, N_("Add"), N_("Add B to A")},
    {int(CarveBlend::Subtract), "SUBTRACT", ICON_NONE, N_("Subtract"), N_("Take B from A")},
    {int(CarveBlend::Multiply), "MULTIPLY", ICON_NONE, N_("Multiply"), N_("Multiply A by B")},
    {int(CarveBlend::Lighter), "LIGHTER", ICON_NONE, N_("Lighter"), N_("The higher of A and B")},
    {int(CarveBlend::Darker), "DARKER", ICON_NONE, N_("Darker"), N_("The lower of A and B")},
    {int(CarveBlend::Difference),
     "DIFFERENCE",
     ICON_NONE,
     N_("Difference"),
     N_("How far apart A and B are")},
    {0, nullptr, 0, nullptr, nullptr},
};

static const EnumPropertyItem curvature_items[] = {
    {int(CarveCurvature::Off), "OFF", ICON_NONE, N_("Off"), N_("Curvature is not used")},
    {int(CarveCurvature::Convex), "CONVEX", ICON_NONE, N_("Convex"), N_("Only on edges")},
    {int(CarveCurvature::Concave), "CONCAVE", ICON_NONE, N_("Concave"), N_("Only in creases")},
    {int(CarveCurvature::Both), "BOTH", ICON_NONE, N_("Both"), N_("On edges and in creases")},
    {0, nullptr, 0, nullptr, nullptr},
};

static const EnumPropertyItem direction_items[] = {
    {int(CarveDirection::Normal), "NORMAL", ICON_NONE, N_("Normal"), N_("Along the normal")},
    {int(CarveDirection::X), "X", ICON_NONE, N_("X"), N_("Along X")},
    {int(CarveDirection::Y), "Y", ICON_NONE, N_("Y"), N_("Along Y")},
    {int(CarveDirection::Z), "Z", ICON_NONE, N_("Z"), N_("Along Z")},
    {int(CarveDirection::Custom), "CUSTOM", ICON_NONE, N_("Custom"), N_("Along a direction")},
    {0, nullptr, 0, nullptr, nullptr},
};

static void declare_layer(NodeDeclarationBuilder &b, const char *name, const char *id, bool use)
{
  const std::string p = std::string(id) + "_";
  const std::string source_id = p + "Source";
  const std::string pattern_id = p + "Pattern";
  const int pattern = int(LayerSource::Pattern);
  const int value = int(LayerSource::Value);
  const Array<int> stones = {
      int(CarvePattern::RandomStone),
      int(CarvePattern::Bricks),
      int(CarvePattern::CoursedBlocks),
      int(CarvePattern::Slate),
      int(CarvePattern::DryStone),
      int(CarvePattern::Banded),
      int(CarvePattern::Fieldstone),
      int(CarvePattern::Rubble),
      int(CarvePattern::Cobblestone),
      int(CarvePattern::Ashlar),
      int(CarvePattern::FittedRubble),
      int(CarvePattern::RoughCourses),
      int(CarvePattern::SquaredStone),
      int(CarvePattern::MixedRubble),
      int(CarvePattern::WornStone),
      int(CarvePattern::ThinCourses),
      int(CarvePattern::PackedRound),
      int(CarvePattern::PackedAngular),
      int(CarvePattern::PackedBlocks),
      int(CarvePattern::PackedSlabs),
      int(CarvePattern::PackedWedges),
      int(CarvePattern::PackedMixed),
      int(CarvePattern::DryWall)};
  const Array<int> rows = {
      int(CarvePattern::RandomStone),
      int(CarvePattern::Bricks),
      int(CarvePattern::CoursedBlocks),
      int(CarvePattern::Slate),
      int(CarvePattern::DryStone),
      int(CarvePattern::Banded),
      int(CarvePattern::FittedRubble),
      int(CarvePattern::RoughCourses),
      int(CarvePattern::SquaredStone),
      int(CarvePattern::MixedRubble),
      int(CarvePattern::WornStone),
      int(CarvePattern::ThinCourses)};
  const Array<int> cells = {
      int(CarvePattern::Voronoi),
      int(CarvePattern::RandomStone),
      int(CarvePattern::Bricks),
      int(CarvePattern::CoursedBlocks),
      int(CarvePattern::Slate),
      int(CarvePattern::DryStone),
      int(CarvePattern::Banded),
      int(CarvePattern::Fieldstone),
      int(CarvePattern::Rubble),
      int(CarvePattern::Cobblestone),
      int(CarvePattern::Ashlar),
      int(CarvePattern::FittedRubble),
      int(CarvePattern::RoughCourses),
      int(CarvePattern::SquaredStone),
      int(CarvePattern::MixedRubble),
      int(CarvePattern::WornStone),
      int(CarvePattern::ThinCourses),
      int(CarvePattern::PackedRound),
      int(CarvePattern::PackedAngular),
      int(CarvePattern::PackedBlocks),
      int(CarvePattern::PackedSlabs),
      int(CarvePattern::PackedWedges),
      int(CarvePattern::PackedMixed),
      int(CarvePattern::DryWall)};
  const Array<int> noisy = {int(CarvePattern::Noise), int(CarvePattern::Waves)};

  PanelDeclarationBuilder &panel = b.add_panel(name).default_closed(!use);
  panel.add_input<decl::Bool>("Use", p + "Use")
      .default_value(use)
      .panel_toggle()
      .description("Use this layer");
  panel.add_input<decl::Menu>("Source", source_id)
      .static_items(source_items)
      .default_value(LayerSource::Pattern)
      .optional_label()
      .description("Where the grayscale comes from");
  panel.add_input<decl::Float>("Value", p + "Value")
      .default_value(0.5f)
      .field_on_all()
      .usage_by_menu(source_id, value)
      .description("Grayscale per point, 0 low and 1 high");
  panel.add_input<decl::Menu>("Pattern", pattern_id)
      .static_items(pattern_items)
      .default_value(use ? CarvePattern::RandomStone : CarvePattern::Noise)
      .optional_label()
      .usage_by_menu(source_id, pattern)
      .description("Built-in pattern");
  panel.add_input<decl::Vector>("Vector", p + "Vector")
      .implicit_field_on_all(NODE_DEFAULT_INPUT_POSITION_FIELD)
      .usage_by_menu(source_id, pattern)
      .description("Where the pattern is read. Defaults to the position");
  panel.add_input<decl::Bool>("Flat", p + "Flat")
      .default_value(false)
      .usage_by_menu(source_id, pattern)
      .description("Read the pattern on X and Y of the vector, as for a UV map. Otherwise the "
                   "pattern stands upright on surfaces facing any way");
  panel.add_input<decl::Float>("Scale", p + "Scale")
      .default_value(use ? 5.0f : 3.0f)
      .min(0.0f)
      .usage_by_menu(source_id, pattern)
      .description("Size of the pattern. Higher is smaller");
  panel.add_input<decl::Float>("Rotation", p + "Rotation")
      .subtype(PROP_ANGLE)
      .usage_by_menu(source_id, pattern)
      .description("Turns the pattern on the surface");
  panel.add_input<decl::Float>("Stone Length", p + "Stone Length")
      .default_value(0.8f)
      .min(0.01f)
      .usage_by_menu(pattern_id, stones)
      .description("Length of the stones, compared to their height");
  panel.add_input<decl::Float>("Row Height", p + "Row Height")
      .default_value(0.25f)
      .min(0.01f)
      .usage_by_menu(pattern_id, rows)
      .description("Height of the rows, before Scale");
  panel.add_input<decl::Float>("Mortar", p + "Mortar")
      .default_value(0.02f)
      .min(0.0f)
      .max(0.5f)
      .usage_by_menu(pattern_id, cells)
      .description("Width of the gaps between stones");
  panel.add_input<decl::Float>("Roundness", p + "Roundness")
      .default_value(0.5f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .usage_by_menu(pattern_id, cells)
      .description("How rounded the edges of the stones are");
  panel.add_input<decl::Float>("Variation", p + "Variation")
      .default_value(0.5f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .usage_by_menu(pattern_id, cells)
      .description("How much the stones differ in size and height");
  panel.add_input<decl::Float>("Distortion", p + "Distortion")
      .default_value(0.0f)
      .min(0.0f)
      .usage_by_menu(pattern_id, int(CarvePattern::Waves))
      .description("How much the waves wobble");
  panel.add_input<decl::Float>("Detail", p + "Detail")
      .default_value(2.0f)
      .min(0.0f)
      .max(15.0f)
      .usage_by_menu(pattern_id, noisy)
      .description("Layers of finer noise");
  panel.add_input<decl::Float>("Roughness", p + "Roughness")
      .default_value(0.5f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .usage_by_menu(source_id, pattern)
      .description("How rough the surface is");
  panel.add_input<decl::Float>("Black", p + "Black")
      .default_value(0.0f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("Values below this become 0");
  panel.add_input<decl::Float>("White", p + "White")
      .default_value(1.0f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("Values above this become 1");
  panel.add_input<decl::Float>("Contrast", p + "Contrast")
      .default_value(0.0f)
      .min(-1.0f)
      .max(1.0f)
      .description("Pushes values apart, or together when negative");
  panel.add_input<decl::Bool>("Invert", p + "Invert")
      .default_value(false)
      .description("Swap high and low");
  panel.add_input<decl::Float>("Strength", p + "Strength")
      .default_value(1.0f)
      .description("How much this layer counts");
  panel.add_input<decl::Material>("High Material", p + "High Material")
      .description("Material for faces where the layer is above Split");
  panel.add_input<decl::Material>("Low Material", p + "Low Material")
      .description("Material for faces where the layer is below Split");
  panel.add_input<decl::Float>("Split", p + "Split")
      .default_value(0.5f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("Value between the low and high materials");
  panel.add_input<decl::Bool>("Masked", p + "Masked")
      .default_value(true)
      .description("Only give the materials where the mask is on");
}

static void node_declare(NodeDeclarationBuilder &b)
{
  b.use_custom_socket_order();
  b.allow_any_socket_order();

  b.add_input<decl::Geometry>("Mesh")
      .supported_type(GeometryComponent::Type::Mesh)
      .is_default_link_socket()
      .description("Mesh to carve");
  b.add_output<decl::Geometry>("Mesh").propagate_all().align_with_previous();
  b.add_output<decl::Float>("Height").field_on_all().description(
      "Combined grayscale per point, before the masks");
  b.add_output<decl::Float>("Mask").field_on_all().description("Combined mask per point");
  b.add_output<decl::Float>("Curvature").field_on_all().description(
      "Curvature per point, positive on edges and negative in creases");

  b.add_input<decl::Bool>("Selection")
      .default_value(true)
      .hide_value()
      .field_on_all()
      .description("Points that move");
  b.add_input<decl::Float>("Factor")
      .default_value(1.0f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .field_on_all()
      .description("How much each point moves");
  b.add_input<decl::Menu>("Direction")
      .static_items(direction_items)
      .default_value(CarveDirection::Normal)
      .optional_label()
      .description("Which way points move");
  b.add_input<decl::Vector>("Custom Direction")
      .default_value(float3(0.0f, 0.0f, 1.0f))
      .usage_by_menu("Direction", int(CarveDirection::Custom))
      .description("Direction for Custom");
  b.add_input<decl::Float>("Strength")
      .default_value(0.1f)
      .subtype(PROP_DISTANCE)
      .description("How far white moves out");
  b.add_input<decl::Float>("Midlevel")
      .default_value(0.5f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("Gray that does not move");
  b.add_input<decl::Float>("Max Depth")
      .default_value(0.0f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .description("Points move at most this far. 0 for no limit");
  b.add_input<decl::Int>("Smooth")
      .default_value(0)
      .min(0)
      .max(100)
      .description("Smooths the movement this many times");
  b.add_input<decl::Int>("Seed").default_value(0).description("Changes the patterns");

  declare_layer(b, "Layer A", "A", true);
  declare_layer(b, "Layer B", "B", false);

  PanelDeclarationBuilder &combine = b.add_panel("Combine").default_closed(true);
  combine.add_input<decl::Menu>("Blend")
      .static_items(blend_items)
      .default_value(CarveBlend::Mix)
      .optional_label()
      .description("How Layer B goes onto Layer A");
  combine.add_input<decl::Float>("Blend Factor")
      .default_value(1.0f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("How much of Layer B is used");

  PanelDeclarationBuilder &masks = b.add_panel("Masks").default_closed(true);
  masks.add_input<decl::Bool>("Noise Mask").default_value(false).description(
      "Carve only in patches of noise");
  masks.add_input<decl::Float>("Mask Scale")
      .default_value(2.0f)
      .min(0.0f)
      .description("Size of the patches. Higher is smaller");
  masks.add_input<decl::Float>("Mask Detail").default_value(2.0f).min(0.0f).max(15.0f).description(
      "Layers of finer noise in the patches");
  masks.add_input<decl::Float>("Mask Threshold")
      .default_value(0.5f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("Higher gives fewer patches");
  masks.add_input<decl::Float>("Mask Softness")
      .default_value(0.1f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("How soft the edges of the patches are");
  masks.add_input<decl::Bool>("Invert Noise Mask").default_value(false).description(
      "Carve outside the patches");
  masks.add_input<decl::Menu>("Curvature")
      .static_items(curvature_items)
      .default_value(CarveCurvature::Off)
      .description("Carve only on edges or in creases");
  const Array<int> curvature_on = {
      int(CarveCurvature::Convex), int(CarveCurvature::Concave), int(CarveCurvature::Both)};
  masks.add_input<decl::Float>("Curvature Threshold")
      .default_value(0.1f)
      .min(0.0f)
      .usage_by_menu("Curvature", curvature_on)
      .description("How sharp an edge or crease must be");
  masks.add_input<decl::Float>("Curvature Softness")
      .default_value(0.1f)
      .min(0.0f)
      .usage_by_menu("Curvature", curvature_on)
      .description("How soft the change is");
  masks.add_input<decl::Bool>("Invert Curvature")
      .default_value(false)
      .usage_by_menu("Curvature", curvature_on)
      .description("Carve away from edges and creases");
  masks.add_input<decl::Bool>("Top").default_value(false).description(
      "Carve surfaces facing up. With none of Top, Sides and Bottom on, all surfaces are carved");
  masks.add_input<decl::Bool>("Sides").default_value(false).description(
      "Carve surfaces facing sideways");
  masks.add_input<decl::Bool>("Bottom").default_value(false).description(
      "Carve surfaces facing down");
  masks.add_input<decl::Float>("Facing Angle")
      .default_value(0.7853982f)
      .min(0.0f)
      .max(M_PI)
      .subtype(PROP_ANGLE)
      .description("How far from the way a surface may turn");
  masks.add_input<decl::Float>("Facing Softness")
      .default_value(0.1745329f)
      .min(0.0f)
      .max(M_PI)
      .subtype(PROP_ANGLE)
      .description("How soft the change is");
  masks.add_input<decl::Bool>("Invert Facing")
      .default_value(false)
      .description("Carve on the other surfaces");
  masks.add_input<decl::Material>("Mask Material")
      .description("Material for faces where the mask is on");
  masks.add_input<decl::Float>("Mask Material Threshold")
      .default_value(0.5f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("Faces where the mask is above this count as masked");
}

struct LayerInputs {
  geometry::CarveLayer layer;
  Material *high = nullptr;
  Material *low = nullptr;
  LayerSource source = LayerSource::Pattern;
  Field<float> value;
  Field<float3> vector;
};

static LayerInputs extract_layer(GeoNodeExecParams &params, const std::string &id)
{
  const std::string p = id + "_";
  LayerInputs inputs;
  geometry::CarveLayer &layer = inputs.layer;
  layer.enabled = params.extract_input<bool>(p + "Use");
  inputs.source = params.extract_input<LayerSource>(p + "Source");
  inputs.value = params.extract_input<Field<float>>(p + "Value");
  inputs.vector = params.extract_input<Field<float3>>(p + "Vector");
  layer.flat = params.extract_input<bool>(p + "Flat");
  geometry::CarvePatternParams &pattern = layer.pattern;
  pattern.pattern = params.extract_input<CarvePattern>(p + "Pattern");
  pattern.scale = params.extract_input<float>(p + "Scale");
  pattern.rotation = params.extract_input<float>(p + "Rotation");
  pattern.stone_length = params.extract_input<float>(p + "Stone Length");
  pattern.row_height = params.extract_input<float>(p + "Row Height");
  pattern.mortar = params.extract_input<float>(p + "Mortar");
  pattern.roundness = params.extract_input<float>(p + "Roundness");
  pattern.variation = params.extract_input<float>(p + "Variation");
  pattern.distortion = params.extract_input<float>(p + "Distortion");
  pattern.detail = params.extract_input<float>(p + "Detail");
  pattern.roughness = params.extract_input<float>(p + "Roughness");
  layer.black = params.extract_input<float>(p + "Black");
  layer.white = params.extract_input<float>(p + "White");
  layer.contrast = params.extract_input<float>(p + "Contrast");
  layer.invert = params.extract_input<bool>(p + "Invert");
  layer.strength = params.extract_input<float>(p + "Strength");
  inputs.high = params.extract_input<Material *>(p + "High Material");
  inputs.low = params.extract_input<Material *>(p + "Low Material");
  layer.material_split = params.extract_input<float>(p + "Split");
  layer.material_masked = params.extract_input<bool>(p + "Masked");
  return inputs;
}

/** Index of the material in the mesh, added at the end when it is not there yet. -1 for none. */
static int ensure_material_index(Mesh &mesh, Material *material)
{
  if (material == nullptr || mesh.faces_num == 0) {
    return -1;
  }
  if (mesh.totcol == 0) {
    BKE_id_material_eval_ensure_default_slot(&mesh.id);
  }
  const int materials_num = mesh.totcol;
  if (mesh.mat && materials_num > 0) {
    const int index = Span(mesh.mat, materials_num).first_index_try(material);
    if (index != -1) {
      return index;
    }
  }
  BKE_id_material_eval_assign(&mesh.id, materials_num + 1, material);
  return materials_num;
}

static void node_geo_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");
  const Field<bool> selection_field = params.extract_input<Field<bool>>("Selection");
  const Field<float> factor_field = params.extract_input<Field<float>>("Factor");

  geometry::MeshCarveParams settings;
  settings.direction = params.extract_input<CarveDirection>("Direction");
  settings.custom_direction = params.extract_input<float3>("Custom Direction");
  settings.strength = params.extract_input<float>("Strength");
  settings.midlevel = params.extract_input<float>("Midlevel");
  settings.max_depth = params.extract_input<float>("Max Depth");
  settings.smooth = params.extract_input<int>("Smooth");
  settings.seed = params.extract_input<int>("Seed");

  LayerInputs a = extract_layer(params, "A");
  LayerInputs b = extract_layer(params, "B");
  a.layer.pattern.seed = settings.seed;
  b.layer.pattern.seed = settings.seed;

  settings.blend = params.extract_input<CarveBlend>("Blend");
  settings.blend_factor = params.extract_input<float>("Blend Factor");
  settings.noise_mask = params.extract_input<bool>("Noise Mask");
  settings.mask_scale = params.extract_input<float>("Mask Scale");
  settings.mask_detail = params.extract_input<float>("Mask Detail");
  settings.mask_threshold = params.extract_input<float>("Mask Threshold");
  settings.mask_softness = params.extract_input<float>("Mask Softness");
  settings.mask_invert = params.extract_input<bool>("Invert Noise Mask");
  settings.curvature = params.extract_input<CarveCurvature>("Curvature");
  settings.curvature_threshold = params.extract_input<float>("Curvature Threshold");
  settings.curvature_softness = params.extract_input<float>("Curvature Softness");
  settings.curvature_invert = params.extract_input<bool>("Invert Curvature");
  settings.facing_top = params.extract_input<bool>("Top");
  settings.facing_sides = params.extract_input<bool>("Sides");
  settings.facing_bottom = params.extract_input<bool>("Bottom");
  settings.facing_angle = params.extract_input<float>("Facing Angle");
  settings.facing_softness = params.extract_input<float>("Facing Softness");
  settings.facing_invert = params.extract_input<bool>("Invert Facing");
  Material *mask_material = params.extract_input<Material *>("Mask Material");
  settings.mask_material_threshold = params.extract_input<float>("Mask Material Threshold");
  settings.height_id = params.get_output_anonymous_attribute_id_if_needed("Height");
  settings.mask_id = params.get_output_anonymous_attribute_id_if_needed("Mask");
  settings.curvature_id = params.get_output_anonymous_attribute_id_if_needed("Curvature");

  if (!a.layer.enabled && !b.layer.enabled) {
    params.set_output("Mesh", std::move(geometry_set));
    return;
  }

  geometry::foreach_real_geometry(geometry_set, [&](GeometrySet &geometry_set) {
    if (!geometry_set.has_mesh()) {
      return;
    }
    Mesh &mesh = *geometry_set.get_mesh_for_write();
    if (mesh.verts_num == 0) {
      return;
    }
    const bke::MeshFieldContext context(mesh, bke::AttrDomain::Point);
    FieldEvaluator evaluator(context, mesh.verts_num);
    evaluator.add(selection_field);
    evaluator.add(factor_field);
    const int a_index = evaluator.add(a.source == LayerSource::Value ?
                                          GField(a.value) :
                                          GField(a.vector));
    const int b_index = evaluator.add(b.source == LayerSource::Value ?
                                          GField(b.value) :
                                          GField(b.vector));
    evaluator.evaluate();
    const VArray<bool> selection = evaluator.get_evaluated<bool>(0);
    const VArray<float> factor = evaluator.get_evaluated<float>(1);

    Array<float> weights(mesh.verts_num);
    for (const int i : weights.index_range()) {
      weights[i] = selection[i] ? factor[i] : 0.0f;
    }

    geometry::MeshCarveParams mesh_settings = settings;
    mesh_settings.weights = weights;
    mesh_settings.a = a.layer;
    mesh_settings.b = b.layer;
    if (a.layer.enabled) {
      mesh_settings.a.material_high = ensure_material_index(mesh, a.high);
      mesh_settings.a.material_low = ensure_material_index(mesh, a.low);
    }
    if (b.layer.enabled) {
      mesh_settings.b.material_high = ensure_material_index(mesh, b.high);
      mesh_settings.b.material_low = ensure_material_index(mesh, b.low);
    }
    mesh_settings.mask_material = ensure_material_index(mesh, mask_material);

    VArraySpan<float> a_values;
    VArraySpan<float3> a_coords;
    VArraySpan<float> b_values;
    VArraySpan<float3> b_coords;
    if (a.source == LayerSource::Value) {
      a_values = evaluator.get_evaluated<float>(a_index);
      mesh_settings.a.values = a_values;
    }
    else {
      a_coords = evaluator.get_evaluated<float3>(a_index);
      mesh_settings.a.coords = a_coords;
    }
    if (b.source == LayerSource::Value) {
      b_values = evaluator.get_evaluated<float>(b_index);
      mesh_settings.b.values = b_values;
    }
    else {
      b_coords = evaluator.get_evaluated<float3>(b_index);
      mesh_settings.b.coords = b_coords;
    }

    geometry::mesh_carve(mesh, mesh_settings);
  });
  params.set_output("Mesh", std::move(geometry_set));
}

static void node_register()
{
  static blender::bke::bNodeType ntype;

  geo_node_type_base(&ntype, "GeometryNodeCarve");
  ntype.ui_name = "Carve";
  ntype.ui_description =
      "Move points by two grayscale layers, patterns or values, combined and masked";
  ntype.nclass = NODE_CLASS_GEOMETRY;
  ntype.declare = node_declare;
  ntype.geometry_node_execute = node_geo_exec;
  blender::bke::node_register_type(ntype);
}
NOD_REGISTER_NODE(node_register)

}  // namespace blender::nodes::node_geo_carve_cc
