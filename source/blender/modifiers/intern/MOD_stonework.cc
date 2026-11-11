/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Stonework: fill the mesh with whole stones, stacked one at a time from the bottom up.
 */

#include "BLI_math_matrix.hh"
#include "BLI_string.h"
#include "BLI_utildefines.h"
#include "BLI_vector.hh"

#include "BLT_translation.hh"

#include "DNA_collection_types.h"
#include "DNA_layer_types.h"
#include "DNA_mesh_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_types.h"
#include "DNA_screen_types.h"

#include "BKE_attribute.h"
#include "BKE_collection.hh"
#include "BKE_lib_id.hh"
#include "BKE_lib_query.hh"
#include "BKE_mesh.hh"
#include "BKE_mesh_wrapper.hh"
#include "BKE_modifier.hh"

#include "UI_interface_layout.hh"
#include "UI_resources.hh"

#include "RNA_access.hh"
#include "RNA_prototypes.hh"

#include "DEG_depsgraph_build.hh"
#include "DEG_depsgraph_query.hh"

#include "GEO_mesh_stonework.hh"
#include "GEO_randomize.hh"

#include "MOD_modifiertypes.hh"
#include "MOD_ui_common.hh"
#include "MOD_util.hh"

namespace blender {

static void init_data(ModifierData *md)
{
  StoneworkModifierData *data = reinterpret_cast<StoneworkModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

static void foreach_ID_link(ModifierData *md, Object *ob, IDWalkFunc walk, void *user_data)
{
  StoneworkModifierData *smd = reinterpret_cast<StoneworkModifierData *>(md);
  walk(user_data, ob, reinterpret_cast<ID **>(&smd->collection), IDWALK_CB_USER);
}

static void update_depsgraph(ModifierData *md, const ModifierUpdateDepsgraphContext *ctx)
{
  StoneworkModifierData *smd = reinterpret_cast<StoneworkModifierData *>(md);
  if (smd->source == MOD_STONEWORK_SOURCE_COLLECTION && smd->collection != nullptr) {
    DEG_add_collection_geometry_relation(ctx->node, smd->collection, "Stonework Modifier");
  }
}

static bool is_disabled(const Scene * /*scene*/, ModifierData *md, bool /*use_render_params*/)
{
  const StoneworkModifierData *smd = reinterpret_cast<const StoneworkModifierData *>(md);
  return smd->source == MOD_STONEWORK_SOURCE_COLLECTION && smd->collection == nullptr;
}

static void store_count(ModifierData *md, const ModifierEvalContext *ctx, const int count)
{
  if (!DEG_is_active(ctx->depsgraph)) {
    return;
  }
  StoneworkModifierData *orig = reinterpret_cast<StoneworkModifierData *>(
      BKE_modifier_get_original(ctx->object, md));
  if (orig != nullptr) {
    orig->stones_num = count;
  }
}

static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  const StoneworkModifierData &smd = *reinterpret_cast<StoneworkModifierData *>(md);

  geometry::StoneworkParams params;
  params.fill = geometry::StoneworkFill(smd.fill);
  params.shape = geometry::StoneworkShape(smd.shape);
  params.stone_size = smd.stone_size;
  params.size_variation = smd.size_variation;
  params.flatness = smd.flatness;
  params.gap = smd.gap;
  params.fillers = smd.flag & MOD_STONEWORK_FILLERS;
  params.poke_out = smd.poke_out;
  params.roughness = smd.roughness;
  params.detail = smd.detail;
  params.max_stones = smd.max_stones;
  params.seed = smd.seed;
  params.close_gaps = smd.close_gaps;
  params.scale_min = smd.scale_min;
  params.scale_max = smd.scale_max;
  params.color_mode = geometry::StoneworkColorMode(smd.color_mode);
  params.hue_min = smd.hue_min;
  params.hue_max = smd.hue_max;
  params.saturation_min = smd.saturation_min;
  params.saturation_max = smd.saturation_max;
  params.value_min = smd.value_min;
  params.value_max = smd.value_max;
  params.mortar = smd.flag & MOD_STONEWORK_MORTAR;
  params.mortar_depth = smd.mortar_depth;
  params.mortar_remesh = geometry::StoneworkRemesh(smd.mortar_remesh);
  params.mortar_resolution = smd.mortar_resolution;
  params.merge = smd.flag & MOD_STONEWORK_MERGE;
  params.material_index = smd.material_index;
  params.mortar_material_index = smd.mortar_material_index;
  params.stone_id_id = "stone_id";
  params.random_id = "stone_random";
  const bool use_color = smd.flag & MOD_STONEWORK_COLOR;
  if (use_color) {
    params.color_id = "stone_color";
  }

  /* Your own stones, turned and scaled as their objects are. */
  Vector<Mesh *> owned;
  Vector<const Mesh *> custom;
  if (smd.source == MOD_STONEWORK_SOURCE_COLLECTION && smd.collection != nullptr) {
    FOREACH_COLLECTION_OBJECT_RECURSIVE_BEGIN (smd.collection, ob) {
      if (ob->type != OB_MESH || ob == ctx->object) {
        continue;
      }
      const Mesh *stone = BKE_modifier_get_evaluated_mesh_from_evaluated_object(ob);
      if (stone == nullptr) {
        continue;
      }
      Mesh *copy = BKE_mesh_copy_for_eval(*stone);
      float4x4 shape = ob->object_to_world();
      shape.location() = float3(0.0f);
      bke::mesh_transform(*copy, shape, false);
      owned.append(copy);
      custom.append(copy);
    }
    FOREACH_COLLECTION_OBJECT_RECURSIVE_END;
  }
  params.custom_stones = custom;

  int count = 0;
  std::string message;
  Mesh *result = geometry::mesh_stonework(*mesh, params, &count, &message);
  if (!message.empty()) {
    BKE_modifier_set_warning(ctx->object, md, "%s", message.c_str());
  }
  for (Mesh *m : owned) {
    BKE_id_free(nullptr, m);
  }
  store_count(md, ctx, count);
  if (result == nullptr) {
    return BKE_mesh_new_nomain(0, 0, 0, 0);
  }
  if (use_color) {
    BKE_id_attributes_active_color_set(&result->id, "stone_color");
    BKE_id_attributes_default_color_set(&result->id, "stone_color");
  }
  geometry::debug_randomize_mesh_order(result);
  return result;
}

static void panel_draw(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);
  const StoneworkModifierData &smd = *static_cast<const StoneworkModifierData *>(ptr->data);
  const bool builtin = smd.source != MOD_STONEWORK_SOURCE_COLLECTION;

  layout.use_property_split_set(true);

  layout.prop(ptr, "fill", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "source", ui::ITEM_R_EXPAND, std::nullopt, ICON_NONE);
  if (builtin) {
    layout.prop(ptr, "shape", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  else {
    layout.prop(ptr, "collection", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }

  ui::Layout &stones = layout.column(true);
  if (builtin) {
    stones.prop(ptr, "stone_size", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  stones.prop(ptr, "size_variation", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  if (builtin) {
    stones.prop(ptr, "flatness", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    stones.prop(ptr, "roughness", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    stones.prop(ptr, "detail", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  ui::Layout &scale = layout.column(true);
  scale.prop(ptr, "scale_min", UI_ITEM_NONE, IFACE_("Scale Min"), ICON_NONE);
  scale.prop(ptr, "scale_max", UI_ITEM_NONE, IFACE_("Max"), ICON_NONE);

  ui::Layout &fit = layout.column(true);
  fit.prop(ptr, "gap", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  fit.prop(ptr, "close_gaps", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  fit.prop(ptr, "poke_out", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "use_fillers", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  const bool mortar = smd.flag & MOD_STONEWORK_MORTAR;
  layout.prop(ptr, "use_mortar", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  ui::Layout &mortar_col = layout.column(true);
  mortar_col.active_set(mortar);
  mortar_col.prop(ptr, "mortar_depth", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  mortar_col.prop(ptr, "mortar_remesh", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  ui::Layout &res_col = mortar_col.column(true);
  res_col.active_set(mortar && smd.mortar_remesh != 3);
  res_col.prop(ptr, "mortar_resolution", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "use_merge", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  ui::Layout &more = layout.column(true);
  more.prop(ptr, "max_stones", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  more.prop(ptr, "seed", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  ui::Layout &materials = layout.column(true);
  materials.prop(ptr, "material_index", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  ui::Layout &mortar_mat = materials.column(true);
  mortar_mat.active_set(mortar);
  mortar_mat.prop(ptr, "mortar_material_index", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "use_random_color", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  if (smd.flag & MOD_STONEWORK_COLOR) {
    layout.prop(ptr, "color_mode", ui::ITEM_R_EXPAND, std::nullopt, ICON_NONE);
    if (smd.color_mode == 0) {
      ui::Layout &hue = layout.column(true);
      hue.prop(ptr, "hue_min", UI_ITEM_NONE, IFACE_("Hue Min"), ICON_NONE);
      hue.prop(ptr, "hue_max", UI_ITEM_NONE, IFACE_("Max"), ICON_NONE);
      ui::Layout &sat = layout.column(true);
      sat.prop(ptr, "saturation_min", UI_ITEM_NONE, IFACE_("Saturation Min"), ICON_NONE);
      sat.prop(ptr, "saturation_max", UI_ITEM_NONE, IFACE_("Max"), ICON_NONE);
    }
    ui::Layout &val = layout.column(true);
    val.prop(ptr, "value_min", UI_ITEM_NONE, IFACE_("Value Min"), ICON_NONE);
    val.prop(ptr, "value_max", UI_ITEM_NONE, IFACE_("Max"), ICON_NONE);
  }

  if (smd.stones_num > 0) {
    char text[64];
    SNPRINTF(text, IFACE_("Stones: %d"), smd.stones_num);
    layout.label(text, ICON_NONE);
  }

  modifier_error_message_draw(layout, ptr);
}

static void panel_register(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_Stonework, panel_draw);
}

ModifierTypeInfo modifierType_Stonework = {
    /*idname*/ "Stonework",
    /*name*/ N_("Stonework"),
    /*struct_name*/ "StoneworkModifierData",
    /*struct_size*/ sizeof(StoneworkModifierData),
    /*srna*/ &RNA_StoneworkModifier,
    /*type*/ ModifierTypeType::Constructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh,
    /*icon*/ ICON_MOD_STONEWORK,

    /*copy_data*/ BKE_modifier_copydata_generic,

    /*deform_verts*/ nullptr,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ modify_mesh,
    /*modify_geometry_set*/ nullptr,

    /*init_data*/ init_data,
    /*required_data_mask*/ nullptr,
    /*free_data*/ nullptr,
    /*is_disabled*/ is_disabled,
    /*update_depsgraph*/ update_depsgraph,
    /*depends_on_time*/ nullptr,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ foreach_ID_link,
    /*foreach_tex_link*/ nullptr,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ panel_register,
    /*blend_write*/ nullptr,
    /*blend_read*/ nullptr,
    /*foreach_cache*/ nullptr,
    /*foreach_working_space_color*/ nullptr,
};

}  // namespace blender
