/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Carve: displace with two grayscale layers (textures, patterns or attributes), combined and
 * masked by noise, weights, curvature and facing.
 */

#include "BLI_array.hh"
#include "BLI_math_matrix.hh"
#include "BLI_string.h"
#include "BLI_task.hh"
#include "BLI_utildefines.h"

#include "BLT_translation.hh"

#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_types.h"
#include "DNA_screen_types.h"
#include "DNA_texture_types.h"

#include "BKE_attribute.hh"
#include "BKE_deform.hh"
#include "BKE_image.hh"
#include "BKE_lib_query.hh"
#include "BKE_material.hh"
#include "BKE_mesh.hh"
#include "BKE_modifier.hh"
#include "BKE_subdiv.hh"
#include "BKE_subdiv_mesh.hh"
#include "BKE_texture.h"

#include "UI_interface.hh"
#include "UI_interface_layout.hh"
#include "UI_resources.hh"

#include "RNA_access.hh"
#include "RNA_prototypes.hh"

#include "DEG_depsgraph.hh"
#include "DEG_depsgraph_build.hh"
#include "DEG_depsgraph_query.hh"

#include "RE_texture.h"

#include "GEO_mesh_carve.hh"

#include "MOD_modifiertypes.hh"
#include "MOD_ui_common.hh"
#include "MOD_util.hh"

namespace blender {

enum class LayerSource : int8_t { Texture = 0, Pattern = 1, Attribute = 2 };
enum class LayerCoords : int8_t { Local = 0, Global = 1, Object = 2, UV = 3 };

static void init_data(ModifierData *md)
{
  auto *data = reinterpret_cast<CarveModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
  data->layer_a.flag |= MOD_CARVE_LAYER_ENABLED;
  data->layer_b.pattern = int8_t(geometry::CarvePattern::Noise);
  data->layer_b.scale = 3.0f;
}

static void required_data_mask(ModifierData *md, CustomData_MeshMasks *r_cddata_masks)
{
  const auto &data = *reinterpret_cast<CarveModifierData *>(md);
  if (data.vertex_group[0] != '\0') {
    r_cddata_masks->vmask |= CD_MASK_MDEFORMVERT;
  }
}

static bool layer_used(const CarveModifierLayer &layer)
{
  return layer.flag & MOD_CARVE_LAYER_ENABLED;
}

static bool is_disabled(const Scene * /*scene*/, ModifierData *md, bool /*use_render_params*/)
{
  const auto &data = *reinterpret_cast<CarveModifierData *>(md);
  return !layer_used(data.layer_a) && !layer_used(data.layer_b);
}

static bool depends_on_time(Scene * /*scene*/, ModifierData *md)
{
  const auto &data = *reinterpret_cast<CarveModifierData *>(md);
  for (const CarveModifierLayer *layer : {&data.layer_a, &data.layer_b}) {
    if (layer_used(*layer) && LayerSource(layer->source) == LayerSource::Texture &&
        layer->texture && BKE_texture_dependsOnTime(layer->texture))
    {
      return true;
    }
  }
  return false;
}

static void foreach_ID_link(ModifierData *md, Object *ob, IDWalkFunc walk, void *user_data)
{
  auto *data = reinterpret_cast<CarveModifierData *>(md);
  for (CarveModifierLayer *layer : {&data->layer_a, &data->layer_b}) {
    walk(user_data, ob, reinterpret_cast<ID **>(&layer->texture), IDWALK_CB_USER);
    walk(user_data, ob, reinterpret_cast<ID **>(&layer->object), IDWALK_CB_NOP);
    walk(user_data, ob, reinterpret_cast<ID **>(&layer->material_high), IDWALK_CB_USER);
    walk(user_data, ob, reinterpret_cast<ID **>(&layer->material_low), IDWALK_CB_USER);
  }
  walk(user_data, ob, reinterpret_cast<ID **>(&data->mask_material), IDWALK_CB_USER);
}

static void foreach_tex_link(ModifierData *md, Object *ob, TexWalkFunc walk, void *user_data)
{
  auto *data = reinterpret_cast<CarveModifierData *>(md);
  for (CarveModifierLayer *layer : {&data->layer_a, &data->layer_b}) {
    PointerRNA ptr = RNA_pointer_create_discrete(&ob->id, RNA_CarveModifierLayer, layer);
    PropertyRNA *prop = RNA_struct_find_property(&ptr, "texture");
    walk(user_data, ob, md, &ptr, prop);
  }
}

static void update_depsgraph(ModifierData *md, const ModifierUpdateDepsgraphContext *ctx)
{
  const auto &data = *reinterpret_cast<CarveModifierData *>(md);
  bool need_transform = false;
  for (const CarveModifierLayer *layer : {&data.layer_a, &data.layer_b}) {
    if (!layer_used(*layer)) {
      continue;
    }
    const LayerSource source = LayerSource(layer->source);
    if (source == LayerSource::Texture && layer->texture) {
      DEG_add_generic_id_relation(ctx->node, &layer->texture->id, "Carve Modifier");
    }
    if (source == LayerSource::Attribute) {
      continue;
    }
    const LayerCoords coords = LayerCoords(layer->coords);
    if (coords == LayerCoords::Object && layer->object) {
      DEG_add_object_relation(
          ctx->node, layer->object, DEG_OB_COMP_TRANSFORM, "Carve Modifier");
      need_transform = true;
    }
    if (coords == LayerCoords::Global) {
      need_transform = true;
    }
  }
  if (need_transform) {
    DEG_add_depends_on_transform_relation(ctx->node, "Carve Modifier");
  }
}

static Mesh *simple_subdivide(const Mesh &mesh, const int level)
{
  bke::subdiv::ToMeshSettings mesh_settings;
  mesh_settings.resolution = (1 << level) + 1;
  mesh_settings.use_optimal_display = false;

  bke::subdiv::Settings subdiv_settings;
  subdiv_settings.is_simple = true;
  subdiv_settings.is_adaptive = false;
  subdiv_settings.use_creases = false;
  subdiv_settings.level = 1;
  subdiv_settings.vtx_boundary_interpolation =
      bke::subdiv::vtx_boundary_interpolation_from_subsurf(0);
  subdiv_settings.fvar_linear_interpolation = bke::subdiv::fvar_interpolation_from_uv_smooth(0);

  bke::subdiv::Subdiv *subdiv = bke::subdiv::new_from_mesh(&subdiv_settings, &mesh);
  if (!subdiv) {
    return nullptr;
  }
  Mesh *result = bke::subdiv::subdiv_to_mesh(subdiv, &mesh_settings, &mesh);
  bke::subdiv::free(subdiv);
  return result;
}

/** Coordinates of each point for a layer. Flat for UV. */
static Array<float3> layer_coords(const CarveModifierLayer &layer,
                                  const ModifierEvalContext &ctx,
                                  const Mesh &mesh,
                                  bool &r_flat)
{
  const Span<float3> positions = mesh.vert_positions();
  Array<float3> coords(positions);
  r_flat = false;
  switch (LayerCoords(layer.coords)) {
    case LayerCoords::Local:
      break;
    case LayerCoords::Global: {
      const float4x4 &to_world = ctx.object->object_to_world();
      for (float3 &co : coords) {
        co = math::transform_point(to_world, co);
      }
      break;
    }
    case LayerCoords::Object: {
      if (layer.object == nullptr) {
        break;
      }
      const float4x4 to_object = layer.object->world_to_object() * ctx.object->object_to_world();
      for (float3 &co : coords) {
        co = math::transform_point(to_object, co);
      }
      break;
    }
    case LayerCoords::UV: {
      r_flat = true;
      const VectorSet<StringRefNull> uv_map_names = mesh.uv_map_names();
      coords.fill(float3(0.0f));
      if (uv_map_names.is_empty()) {
        break;
      }
      const StringRef name = uv_map_names.contains(layer.uv_map) ? StringRef(layer.uv_map) :
                                                                  mesh.active_uv_map_name();
      const VArraySpan uv_map = *mesh.attributes().lookup_or_default<float2>(
          name, bke::AttrDomain::Point, float2(0.0f));
      for (const int i : coords.index_range()) {
        coords[i] = float3(uv_map[i], 0.0f);
      }
      break;
    }
  }
  return coords;
}

static Array<float> texture_values(const CarveModifierLayer &layer,
                                   const ModifierEvalContext &ctx,
                                   const Span<float3> coords,
                                   const bool flat)
{
  Tex *tex = layer.texture;
  Array<float> values(coords.size(), 0.0f);
  if (tex == nullptr) {
    return values;
  }
  if (tex->ima && BKE_image_is_animated(tex->ima)) {
    BKE_image_user_frame_calc(tex->ima, &tex->iuser, DEG_get_ctime(ctx.depsgraph));
  }
  ImagePool *pool = BKE_image_pool_new();
  BKE_texture_fetch_images_for_pool(tex, pool);
  const float3 offset(layer.offset);
  threading::parallel_for(coords.index_range(), 512, [&](const IndexRange range) {
    for (const int i : range) {
      float3 co = coords[i] + offset;
      if (flat) {
        /* Textures read UVs from -1 to 1. */
        co = float3(co.x * 2.0f - 1.0f, co.y * 2.0f - 1.0f, 0.0f);
      }
      TexResult texres;
      BKE_texture_get_value_ex(tex, co, &texres, pool, false);
      values[i] = texres.tin;
    }
  });
  BKE_image_pool_free(pool);
  return values;
}

static Array<float> attribute_values(const char *name, const Mesh &mesh)
{
  Array<float> values(mesh.verts_num, 0.0f);
  if (name[0] == '\0') {
    return values;
  }
  const bke::GAttributeReader attribute = mesh.attributes().lookup(name);
  if (!attribute) {
    return values;
  }
  const VArraySpan<float> read = *mesh.attributes().lookup<float>(name, bke::AttrDomain::Point);
  if (!read.is_empty()) {
    values.as_mutable_span().copy_from(read);
  }
  return values;
}

struct LayerData {
  Array<float3> coords;
  Array<float> values;
};

static void prepare_layer(const CarveModifierLayer &dna,
                          const CarveModifierData &data,
                          const ModifierEvalContext &ctx,
                          const Mesh &mesh,
                          LayerData &storage,
                          geometry::CarveLayer &r_layer)
{
  r_layer.enabled = layer_used(dna);
  if (!r_layer.enabled) {
    return;
  }
  const LayerSource source = LayerSource(dna.source);
  if (source == LayerSource::Attribute) {
    storage.values = attribute_values(dna.attribute, mesh);
    r_layer.values = storage.values;
  }
  else {
    bool flat = false;
    storage.coords = layer_coords(dna, ctx, mesh, flat);
    if (source == LayerSource::Texture) {
      storage.values = texture_values(dna, ctx, storage.coords, flat);
      r_layer.values = storage.values;
    }
    else {
      r_layer.coords = storage.coords;
      r_layer.flat = flat;
    }
  }
  geometry::CarvePatternParams &p = r_layer.pattern;
  p.pattern = geometry::CarvePattern(dna.pattern);
  p.scale = dna.scale;
  p.offset = float3(dna.offset);
  p.rotation = dna.rotation;
  p.detail = dna.detail;
  p.roughness = dna.roughness;
  p.distortion = dna.distortion;
  p.stone_length = dna.stone_length;
  p.row_height = dna.row_height;
  p.mortar = dna.mortar;
  p.roundness = dna.roundness;
  p.variation = dna.variation;
  p.seed = data.seed;
  r_layer.black = dna.black;
  r_layer.white = dna.white;
  r_layer.contrast = dna.contrast;
  r_layer.invert = dna.flag & MOD_CARVE_LAYER_INVERT;
  r_layer.strength = dna.strength;
}

/** Index of the material in the mesh, added at the end when it is not there yet. -1 for none. */
static int ensure_material_index(Mesh &mesh, Material *material)
{
  if (material == nullptr) {
    return -1;
  }
  if (mesh.totcol == 0) {
    /* Faces that get no material keep pointing at an empty slot. */
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

static void layer_materials(const CarveModifierLayer &dna, Mesh &mesh, geometry::CarveLayer &layer)
{
  layer.material_high = ensure_material_index(mesh, dna.material_high);
  layer.material_low = ensure_material_index(mesh, dna.material_low);
  layer.material_split = dna.material_split;
  layer.material_masked = dna.flag & MOD_CARVE_LAYER_MATERIAL_MASKED;
}

static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  const auto &data = *reinterpret_cast<CarveModifierData *>(md);
  if (mesh->verts_num == 0 || (!layer_used(data.layer_a) && !layer_used(data.layer_b))) {
    return mesh;
  }

  Mesh *result = nullptr;
  if (data.subdivide > 0 && mesh->faces_num > 0) {
    result = simple_subdivide(*mesh, std::min(data.subdivide, 6));
  }
  if (result == nullptr) {
    result = BKE_mesh_copy_for_eval(*mesh);
  }

  geometry::MeshCarveParams params;
  LayerData storage_a;
  LayerData storage_b;
  prepare_layer(data.layer_a, data, *ctx, *result, storage_a, params.a);
  prepare_layer(data.layer_b, data, *ctx, *result, storage_b, params.b);
  if (result->faces_num > 0) {
    if (params.a.enabled) {
      layer_materials(data.layer_a, *result, params.a);
    }
    if (params.b.enabled) {
      layer_materials(data.layer_b, *result, params.b);
    }
    params.mask_material = ensure_material_index(*result, data.mask_material);
    params.mask_material_threshold = data.mask_material_threshold;
  }
  params.blend = geometry::CarveBlend(data.blend);
  params.blend_factor = data.blend_factor;

  /* Weights from the vertex group and the mask attribute. */
  Array<float> weights;
  const MDeformVert *dvert = nullptr;
  int defgrp_index = -1;
  MOD_get_vgroup(ctx->object, result, data.vertex_group, &dvert, &defgrp_index);
  const bool use_vgroup = data.vertex_group[0] != '\0';
  const bool use_attribute = data.mask_attribute[0] != '\0';
  if (use_vgroup || use_attribute) {
    weights = Array<float>(result->verts_num, 1.0f);
    if (use_vgroup) {
      const bool invert = data.flag & MOD_CARVE_INVERT_VGROUP;
      for (const int i : weights.index_range()) {
        const float w = dvert ? BKE_defvert_find_weight(&dvert[i], defgrp_index) : 0.0f;
        weights[i] *= invert ? 1.0f - w : w;
      }
    }
    if (use_attribute) {
      const Array<float> values = attribute_values(data.mask_attribute, *result);
      const bool invert = data.flag & MOD_CARVE_INVERT_ATTRIBUTE;
      for (const int i : weights.index_range()) {
        const float w = std::clamp(values[i], 0.0f, 1.0f);
        weights[i] *= invert ? 1.0f - w : w;
      }
    }
    params.weights = weights;
  }

  params.noise_mask = data.flag & MOD_CARVE_NOISE_MASK;
  params.mask_scale = data.mask_scale;
  params.mask_detail = data.mask_detail;
  params.mask_threshold = data.mask_threshold;
  params.mask_softness = data.mask_softness;
  params.mask_invert = data.flag & MOD_CARVE_NOISE_MASK_INVERT;
  params.seed = data.seed;
  params.curvature = geometry::CarveCurvature(data.curvature);
  params.curvature_threshold = data.curvature_threshold;
  params.curvature_softness = data.curvature_softness;
  params.curvature_invert = data.flag & MOD_CARVE_CURVATURE_INVERT;
  params.facing_top = data.facing & MOD_CARVE_FACING_TOP;
  params.facing_sides = data.facing & MOD_CARVE_FACING_SIDES;
  params.facing_bottom = data.facing & MOD_CARVE_FACING_BOTTOM;
  params.facing_angle = data.facing_angle;
  params.facing_softness = data.facing_softness;
  params.facing_invert = data.flag & MOD_CARVE_FACING_INVERT;
  params.direction = geometry::CarveDirection(data.direction);
  params.custom_direction = float3(data.custom_direction);
  params.strength = data.strength;
  params.midlevel = data.midlevel;
  params.max_depth = data.max_depth;
  params.smooth = data.smooth;
  params.height_id = "carve_height";
  params.mask_id = "carve_mask";
  params.curvature_id = "carve_curvature";

  geometry::mesh_carve(*result, params);
  return result;
}

/* -------------------------------------------------------------------- */
/** \name UI
 * \{ */

static void panel_draw(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);
  layout.use_property_split_set(true);

  layout.prop(ptr, "direction", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  if (geometry::CarveDirection(RNA_enum_get(ptr, "direction")) == geometry::CarveDirection::Custom)
  {
    layout.prop(ptr, "custom_direction", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  ui::Layout &col = layout.column(true);
  col.prop(ptr, "strength", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  col.prop(ptr, "midlevel", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  col.prop(ptr, "max_depth", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "subdivide", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "smooth", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "seed", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  modifier_error_message_draw(layout, ptr);
}

static void draw_layer(const bContext *C, Panel *panel, const char *layer_name)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);
  PointerRNA layer_ptr = RNA_pointer_get(ptr, layer_name);
  layout.use_property_split_set(true);
  layout.active_set(RNA_boolean_get(&layer_ptr, "use"));

  layout.prop(&layer_ptr, "source", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  const LayerSource source = LayerSource(RNA_enum_get(&layer_ptr, "source"));
  PointerRNA mesh_ptr = RNA_pointer_get(&ob_ptr, "data");

  if (source == LayerSource::Attribute) {
    layout.prop_search(
        &layer_ptr, "attribute", &mesh_ptr, "attributes", std::nullopt, ICON_NONE);
  }
  else {
    if (source == LayerSource::Texture) {
      template_id(&layout, C, &layer_ptr, "texture", "texture.new", nullptr, nullptr);
    }
    else {
      layout.prop(&layer_ptr, "pattern", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    }
    layout.prop(&layer_ptr, "coords", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    const LayerCoords coords = LayerCoords(RNA_enum_get(&layer_ptr, "coords"));
    if (coords == LayerCoords::Object) {
      layout.prop(&layer_ptr, "object", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    }
    else if (coords == LayerCoords::UV) {
      layout.prop_search(
          &layer_ptr, "uv_map", &mesh_ptr, "uv_layers", std::nullopt, ICON_GROUP_UVS);
    }
    ui::Layout &place = layout.column(true);
    if (source == LayerSource::Pattern) {
      place.prop(&layer_ptr, "scale", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      place.prop(&layer_ptr, "rotation", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    }
    layout.prop(&layer_ptr, "offset", UI_ITEM_NONE, std::nullopt, ICON_NONE);

    if (source == LayerSource::Pattern) {
      ui::Layout &shape = layout.column(true);
      switch (geometry::CarvePattern(RNA_enum_get(&layer_ptr, "pattern"))) {
        case geometry::CarvePattern::Noise:
          shape.prop(&layer_ptr, "detail", UI_ITEM_NONE, std::nullopt, ICON_NONE);
          shape.prop(&layer_ptr, "roughness", UI_ITEM_NONE, std::nullopt, ICON_NONE);
          break;
        case geometry::CarvePattern::Voronoi:
          shape.prop(&layer_ptr, "mortar", UI_ITEM_NONE, std::nullopt, ICON_NONE);
          shape.prop(&layer_ptr, "roundness", UI_ITEM_NONE, std::nullopt, ICON_NONE);
          shape.prop(&layer_ptr, "variation", UI_ITEM_NONE, std::nullopt, ICON_NONE);
          shape.prop(&layer_ptr, "roughness", UI_ITEM_NONE, std::nullopt, ICON_NONE);
          break;
        case geometry::CarvePattern::Bricks:
          shape.prop(&layer_ptr, "stone_length", UI_ITEM_NONE, std::nullopt, ICON_NONE);
          shape.prop(&layer_ptr, "row_height", UI_ITEM_NONE, std::nullopt, ICON_NONE);
          shape.prop(&layer_ptr, "mortar", UI_ITEM_NONE, std::nullopt, ICON_NONE);
          shape.prop(&layer_ptr, "roundness", UI_ITEM_NONE, std::nullopt, ICON_NONE);
          shape.prop(&layer_ptr, "roughness", UI_ITEM_NONE, std::nullopt, ICON_NONE);
          break;
        case geometry::CarvePattern::RandomStone:
        case geometry::CarvePattern::CoursedBlocks:
        case geometry::CarvePattern::Slate:
        case geometry::CarvePattern::DryStone:
        case geometry::CarvePattern::Banded:
        case geometry::CarvePattern::FittedRubble:
        case geometry::CarvePattern::RoughCourses:
        case geometry::CarvePattern::SquaredStone:
        case geometry::CarvePattern::MixedRubble:
        case geometry::CarvePattern::WornStone:
        case geometry::CarvePattern::ThinCourses:
          shape.prop(&layer_ptr, "stone_length", UI_ITEM_NONE, std::nullopt, ICON_NONE);
          shape.prop(&layer_ptr, "row_height", UI_ITEM_NONE, std::nullopt, ICON_NONE);
          shape.prop(&layer_ptr, "mortar", UI_ITEM_NONE, std::nullopt, ICON_NONE);
          shape.prop(&layer_ptr, "roundness", UI_ITEM_NONE, std::nullopt, ICON_NONE);
          shape.prop(&layer_ptr, "variation", UI_ITEM_NONE, std::nullopt, ICON_NONE);
          shape.prop(&layer_ptr, "roughness", UI_ITEM_NONE, std::nullopt, ICON_NONE);
          break;
        case geometry::CarvePattern::Fieldstone:
        case geometry::CarvePattern::Rubble:
        case geometry::CarvePattern::Cobblestone:
        case geometry::CarvePattern::Ashlar:
        case geometry::CarvePattern::PackedRound:
        case geometry::CarvePattern::PackedAngular:
        case geometry::CarvePattern::PackedBlocks:
        case geometry::CarvePattern::PackedSlabs:
        case geometry::CarvePattern::PackedWedges:
        case geometry::CarvePattern::PackedMixed:
        case geometry::CarvePattern::DryWall:
          shape.prop(&layer_ptr, "stone_length", UI_ITEM_NONE, IFACE_("Stone Size"), ICON_NONE);
          shape.prop(&layer_ptr, "mortar", UI_ITEM_NONE, std::nullopt, ICON_NONE);
          shape.prop(&layer_ptr, "roundness", UI_ITEM_NONE, std::nullopt, ICON_NONE);
          shape.prop(&layer_ptr, "variation", UI_ITEM_NONE, std::nullopt, ICON_NONE);
          shape.prop(&layer_ptr, "roughness", UI_ITEM_NONE, std::nullopt, ICON_NONE);
          break;
        case geometry::CarvePattern::Waves:
          shape.prop(&layer_ptr, "distortion", UI_ITEM_NONE, std::nullopt, ICON_NONE);
          shape.prop(&layer_ptr, "detail", UI_ITEM_NONE, std::nullopt, ICON_NONE);
          shape.prop(&layer_ptr, "roughness", UI_ITEM_NONE, std::nullopt, ICON_NONE);
          break;
      }
    }
  }

  ui::Layout &levels = layout.column(true);
  levels.prop(&layer_ptr, "black", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  levels.prop(&layer_ptr, "white", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  levels.prop(&layer_ptr, "contrast", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(&layer_ptr, "invert", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(&layer_ptr, "strength", UI_ITEM_NONE, std::nullopt, ICON_NONE);
}

static void draw_layer_header(Panel *panel, const char *layer_name)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, nullptr);
  PointerRNA layer_ptr = RNA_pointer_get(ptr, layer_name);
  layout.prop(&layer_ptr, "use", UI_ITEM_NONE, "", ICON_NONE);
}

static void layer_a_header_draw(const bContext * /*C*/, Panel *panel)
{
  draw_layer_header(panel, "layer_a");
}

static void layer_b_header_draw(const bContext * /*C*/, Panel *panel)
{
  draw_layer_header(panel, "layer_b");
}

static void layer_a_panel_draw(const bContext *C, Panel *panel)
{
  draw_layer(C, panel, "layer_a");
}

static void layer_b_panel_draw(const bContext *C, Panel *panel)
{
  draw_layer(C, panel, "layer_b");
}

static void combine_panel_draw(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, nullptr);
  layout.use_property_split_set(true);
  layout.prop(ptr, "blend", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "blend_factor", UI_ITEM_NONE, std::nullopt, ICON_NONE);
}

static void masks_panel_draw(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);
  layout.use_property_split_set(true);

  ui::Layout &noise = layout.column(true, IFACE_("Noise Mask"));
  noise.prop(ptr, "use_noise_mask", UI_ITEM_NONE, "", ICON_NONE);
  ui::Layout &noise_values = noise.column(true);
  noise_values.active_set(RNA_boolean_get(ptr, "use_noise_mask"));
  noise_values.prop(ptr, "mask_scale", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  noise_values.prop(ptr, "mask_detail", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  noise_values.prop(ptr, "mask_threshold", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  noise_values.prop(ptr, "mask_softness", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  noise_values.prop(ptr, "invert_noise_mask", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  modifier_vgroup_ui(layout, ptr, &ob_ptr, "vertex_group", "invert_vertex_group", std::nullopt);

  PointerRNA mesh_ptr = RNA_pointer_get(&ob_ptr, "data");
  ui::Layout &attr = layout.row(true);
  attr.prop_search(ptr, "mask_attribute", &mesh_ptr, "attributes", std::nullopt, ICON_NONE);
  attr.prop(ptr, "invert_mask_attribute", UI_ITEM_NONE, "", ICON_ARROW_LEFTRIGHT);

  layout.prop(ptr, "curvature", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  if (geometry::CarveCurvature(RNA_enum_get(ptr, "curvature")) != geometry::CarveCurvature::Off) {
    ui::Layout &c = layout.column(true);
    c.prop(ptr, "curvature_threshold", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    c.prop(ptr, "curvature_softness", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    c.prop(ptr, "invert_curvature", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }

  layout.row(true).prop(ptr, "facing", ui::ITEM_R_EXPAND, std::nullopt, ICON_NONE);
  if (RNA_enum_get(ptr, "facing") != 0) {
    ui::Layout &f = layout.column(true);
    f.prop(ptr, "facing_angle", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    f.prop(ptr, "facing_softness", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    f.prop(ptr, "invert_facing", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
}

static void materials_panel_draw(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, nullptr);
  layout.use_property_split_set(true);

  for (const char *layer_name : {"layer_a", "layer_b"}) {
    PointerRNA layer_ptr = RNA_pointer_get(ptr, layer_name);
    ui::Layout &col = layout.column(
        true, STREQ(layer_name, "layer_a") ? IFACE_("Layer A") : IFACE_("Layer B"));
    col.active_set(RNA_boolean_get(&layer_ptr, "use"));
    col.prop(&layer_ptr, "material_high", UI_ITEM_NONE, IFACE_("High"), ICON_NONE);
    col.prop(&layer_ptr, "material_low", UI_ITEM_NONE, IFACE_("Low"), ICON_NONE);
    col.prop(&layer_ptr, "material_split", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    col.prop(&layer_ptr, "material_masked", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    layout.separator();
  }

  ui::Layout &mask = layout.column(true, IFACE_("Mask"));
  mask.prop(ptr, "mask_material", UI_ITEM_NONE, IFACE_("Material"), ICON_NONE);
  mask.prop(ptr, "mask_material_threshold", UI_ITEM_NONE, std::nullopt, ICON_NONE);
}

static void panel_register(ARegionType *region_type)
{
  PanelType *parent = modifier_panel_register(region_type, eModifierType_Carve, panel_draw);
  modifier_subpanel_register(
      region_type, "layer_a", "Layer A", layer_a_header_draw, layer_a_panel_draw, parent);
  modifier_subpanel_register(
      region_type, "layer_b", "Layer B", layer_b_header_draw, layer_b_panel_draw, parent);
  modifier_subpanel_register(region_type, "combine", "Combine", nullptr, combine_panel_draw, parent);
  modifier_subpanel_register(region_type, "masks", "Masks", nullptr, masks_panel_draw, parent);
  modifier_subpanel_register(
      region_type, "materials", "Materials", nullptr, materials_panel_draw, parent);
}

/** \} */

ModifierTypeInfo modifierType_Carve = {
    /*idname*/ "Carve",
    /*name*/ N_("Carve"),
    /*struct_name*/ "CarveModifierData",
    /*struct_size*/ sizeof(CarveModifierData),
    /*srna*/ &RNA_CarveModifier,
    /*type*/ ModifierTypeType::Constructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode,
    /*icon*/ ICON_MOD_CARVE,

    /*copy_data*/ BKE_modifier_copydata_generic,

    /*deform_verts*/ nullptr,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ modify_mesh,
    /*modify_geometry_set*/ nullptr,

    /*init_data*/ init_data,
    /*required_data_mask*/ required_data_mask,
    /*free_data*/ nullptr,
    /*is_disabled*/ is_disabled,
    /*update_depsgraph*/ update_depsgraph,
    /*depends_on_time*/ depends_on_time,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ foreach_ID_link,
    /*foreach_tex_link*/ foreach_tex_link,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ panel_register,
    /*blend_write*/ nullptr,
    /*blend_read*/ nullptr,
    /*foreach_cache*/ nullptr,
    /*foreach_working_space_color*/ nullptr,
};

}  // namespace blender
