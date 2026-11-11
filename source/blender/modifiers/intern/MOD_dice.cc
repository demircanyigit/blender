/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Cut the mesh into a grid of pieces along the axes of its bounding box.
 */

#include "BLI_math_matrix.hh"

#include "BLT_translation.hh"

#include "DNA_mesh_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_types.h"
#include "DNA_screen_types.h"

#include "BKE_attribute.hh"
#include "BKE_mesh.hh"
#include "BKE_modifier.hh"

#include "DEG_depsgraph_build.hh"

#include "UI_interface_layout.hh"
#include "UI_resources.hh"

#include "RNA_access.hh"
#include "RNA_prototypes.hh"

#include "GEO_mesh_sim_cache.hh"
#include "GEO_mesh_dice.hh"
#include "GEO_randomize.hh"

#include "MOD_modifiertypes.hh"
#include "MOD_ui_common.hh"
#include "MOD_util.hh"

namespace blender {

/** Internal face attribute used to find the cut faces for the interior material. */
static const char *DICE_INTERIOR_ATTRIBUTE = ".dice_interior";

static void init_data(ModifierData *md)
{
  DiceModifierData *data = reinterpret_cast<DiceModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

static void update_depsgraph(ModifierData *md, const ModifierUpdateDepsgraphContext *ctx)
{
  const DiceModifierData *dmd = reinterpret_cast<DiceModifierData *>(md);
  if (dmd->flag & MOD_DICE_WORLD_SPACE) {
    DEG_add_depends_on_transform_relation(ctx->node, "Dice Modifier");
  }
}

static Mesh *modify_mesh_compute(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  const DiceModifierData &dmd = *reinterpret_cast<DiceModifierData *>(md);
  if (mesh->verts_num == 0) {
    return mesh;
  }

  const bool use_interior_material = dmd.flag & MOD_DICE_INTERIOR_MATERIAL;
  geometry::MeshDiceParams params;
  params.mode = dmd.mode == MOD_DICE_MODE_SIZE ? geometry::MeshDiceMode::Size :
                                                 geometry::MeshDiceMode::Count;
  params.counts = int3(dmd.count[0], dmd.count[1], dmd.count[2]);
  params.size = float3(dmd.size);
  params.gap = dmd.cut_gap;
  params.fill = dmd.flag & MOD_DICE_FILL;
  if (dmd.flag & MOD_DICE_WORLD_SPACE) {
    params.mesh_to_space = ctx->object->object_to_world();
  }
  if (use_interior_material) {
    params.interior_id = DICE_INTERIOR_ATTRIBUTE;
  }

  Mesh *result = geometry::mesh_dice(*mesh, params);
  if (result == nullptr) {
    BKE_modifier_set_error(ctx->object, md, "Too many cells, the limit is one million");
    return mesh;
  }

  if (use_interior_material) {
    bke::MutableAttributeAccessor attributes = result->attributes_for_write();
    {
      const VArraySpan<bool> interior = *attributes.lookup_or_default<bool>(
          DICE_INTERIOR_ATTRIBUTE, bke::AttrDomain::Face, false);
      bke::SpanAttributeWriter<int> material_index =
          attributes.lookup_or_add_for_write_span<int>("material_index", bke::AttrDomain::Face);
      if (material_index) {
        for (const int face : interior.index_range()) {
          if (interior[face]) {
            material_index.span[face] = dmd.interior_material;
          }
        }
        material_index.finish();
      }
    }
    attributes.remove(DICE_INTERIOR_ATTRIBUTE);
  }

  geometry::debug_randomize_mesh_order(result);
  return result;
}

static void panel_draw(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);

  layout.prop(ptr, "mode", ui::ITEM_R_EXPAND, std::nullopt, ICON_NONE);
  layout.use_property_split_set(true);
  if (RNA_enum_get(ptr, "mode") == MOD_DICE_MODE_SIZE) {
    layout.prop(ptr, "size", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  else {
    layout.prop(ptr, "count", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  layout.prop(ptr, "use_world_space", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "cut_gap", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "use_fill", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  ui::Layout &row = layout.row(true, IFACE_("Interior Material"));
  row.prop(ptr, "use_interior_material", UI_ITEM_NONE, "", ICON_NONE);
  ui::Layout &sub = row.row(true);
  sub.active_set(RNA_boolean_get(ptr, "use_interior_material"));
  sub.prop(ptr, "interior_material", UI_ITEM_NONE, "", ICON_NONE);

  modifier_error_message_draw(layout, ptr);
}

static void panel_register(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_Dice, panel_draw);
}

/* Taken from the cache when nothing it depends on changed, see #MOD_cached_modify_mesh. */
static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  const DiceModifierData &dmd = *reinterpret_cast<DiceModifierData *>(md);
  return MOD_cached_modify_mesh(
      md, ctx, mesh, modify_mesh_compute, [&](geometry::SimCacheHasher &hasher) {
        if (dmd.flag & MOD_DICE_WORLD_SPACE) {
          hasher.add(ctx->object->object_to_world());
        }
      });
}

ModifierTypeInfo modifierType_Dice = {
    /*idname*/ "Dice",
    /*name*/ N_("Dice"),
    /*struct_name*/ "DiceModifierData",
    /*struct_size*/ sizeof(DiceModifierData),
    /*srna*/ &RNA_DiceModifier,
    /*type*/ ModifierTypeType::Constructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode,
    /*icon*/ ICON_MOD_DICE,

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
    /*is_disabled*/ nullptr,
    /*update_depsgraph*/ update_depsgraph,
    /*depends_on_time*/ nullptr,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ nullptr,
    /*foreach_tex_link*/ nullptr,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ panel_register,
    /*blend_write*/ nullptr,
    /*blend_read*/ nullptr,
    /*foreach_cache*/ nullptr,
    /*foreach_working_space_color*/ nullptr,
};

}  // namespace blender
