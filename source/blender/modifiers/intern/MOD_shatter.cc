/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Break a closed mesh into solid pieces the way glass, wood, stone, ceramic or ice breaks.
 */

#include "BLI_math_matrix.hh"

#include "BLT_translation.hh"

#include "DNA_mesh_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_types.h"
#include "DNA_screen_types.h"

#include "BKE_attribute.hh"
#include "BKE_lib_query.hh"
#include "BKE_mesh.hh"
#include "BKE_modifier.hh"

#include "DEG_depsgraph_build.hh"

#include "UI_interface_layout.hh"
#include "UI_resources.hh"

#include "RNA_access.hh"
#include "RNA_prototypes.hh"

#include "GEO_mesh_shatter.hh"
#include "GEO_randomize.hh"

#include "MOD_modifiertypes.hh"
#include "MOD_ui_common.hh"
#include "MOD_util.hh"

namespace blender {

/** Internal face attribute used to find the broken faces for the interior material. */
static const char *SHATTER_INTERIOR_ATTRIBUTE = ".shatter_interior_material";

static void init_data(ModifierData *md)
{
  ShatterModifierData *data = reinterpret_cast<ShatterModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

static void foreach_ID_link(ModifierData *md, Object *ob, IDWalkFunc walk, void *user_data)
{
  ShatterModifierData *smd = reinterpret_cast<ShatterModifierData *>(md);
  walk(user_data, ob, reinterpret_cast<ID **>(&smd->impact), IDWALK_CB_NOP);
}

static void update_depsgraph(ModifierData *md, const ModifierUpdateDepsgraphContext *ctx)
{
  const ShatterModifierData *smd = reinterpret_cast<ShatterModifierData *>(md);
  if (smd->impact != nullptr) {
    DEG_add_object_relation(ctx->node, smd->impact, DEG_OB_COMP_TRANSFORM, "Shatter Modifier");
    DEG_add_depends_on_transform_relation(ctx->node, "Shatter Modifier");
  }
}

static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  const ShatterModifierData &smd = *reinterpret_cast<ShatterModifierData *>(md);
  if (mesh->faces_num == 0) {
    return mesh;
  }

  const bool use_interior_material = smd.flag & MOD_SHATTER_INTERIOR_MATERIAL;
  geometry::MeshShatterParams params;
  params.material = geometry::ShatterMaterial(smd.material);
  params.pieces = smd.pieces;
  params.seed = smd.seed;
  params.focus = smd.focus;
  params.grain_axis = smd.grain_axis - 1;
  params.grain = smd.grain;
  params.roughness = smd.roughness;
  params.detail = smd.detail;
  params.cut_gap = smd.cut_gap;
  if (smd.impact != nullptr && smd.impact != ctx->object) {
    const float4x4 to_self = ctx->object->world_to_object() * smd.impact->object_to_world();
    params.impact = to_self.location();
  }
  if (use_interior_material) {
    params.interior_id = SHATTER_INTERIOR_ATTRIBUTE;
  }

  const char *error = nullptr;
  Mesh *result = geometry::mesh_shatter(*mesh, params, &error);
  if (result == nullptr) {
    if (error != nullptr) {
      BKE_modifier_set_error(ctx->object, md, "%s", error);
    }
    return mesh;
  }

  if (use_interior_material) {
    bke::MutableAttributeAccessor attributes = result->attributes_for_write();
    {
      const VArraySpan<bool> interior = *attributes.lookup_or_default<bool>(
          SHATTER_INTERIOR_ATTRIBUTE, bke::AttrDomain::Face, false);
      bke::SpanAttributeWriter<int> material_index =
          attributes.lookup_or_add_for_write_span<int>("material_index", bke::AttrDomain::Face);
      if (material_index) {
        for (const int face : interior.index_range()) {
          if (interior[face]) {
            material_index.span[face] = smd.interior_material;
          }
        }
        material_index.finish();
      }
    }
    attributes.remove(SHATTER_INTERIOR_ATTRIBUTE);
  }

  geometry::debug_randomize_mesh_order(result);
  return result;
}

static void panel_draw(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);

  layout.prop(ptr, "material", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.use_property_split_set(true);

  ui::Layout &col = layout.column(true);
  col.prop(ptr, "pieces", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  col.prop(ptr, "seed", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  layout.prop(ptr, "impact", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "focus", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  if (RNA_enum_get(ptr, "material") == int(geometry::ShatterMaterial::Wood)) {
    ui::Layout &grain = layout.column(true);
    grain.prop(ptr, "grain_axis", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    grain.prop(ptr, "grain", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }

  ui::Layout &rough = layout.column(true);
  rough.prop(ptr, "roughness", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  ui::Layout &detail = rough.column(true);
  detail.active_set(RNA_float_get(ptr, "roughness") > 0.0f);
  detail.prop(ptr, "detail", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  layout.prop(ptr, "cut_gap", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  ui::Layout &row = layout.row(true, IFACE_("Interior Material"));
  row.prop(ptr, "use_interior_material", UI_ITEM_NONE, "", ICON_NONE);
  ui::Layout &sub = row.row(true);
  sub.active_set(RNA_boolean_get(ptr, "use_interior_material"));
  sub.prop(ptr, "interior_material", UI_ITEM_NONE, "", ICON_NONE);

  modifier_error_message_draw(layout, ptr);
}

static void panel_register(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_Shatter, panel_draw);
}

ModifierTypeInfo modifierType_Shatter = {
    /*idname*/ "Shatter",
    /*name*/ N_("Shatter"),
    /*struct_name*/ "ShatterModifierData",
    /*struct_size*/ sizeof(ShatterModifierData),
    /*srna*/ &RNA_ShatterModifier,
    /*type*/ ModifierTypeType::Constructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode,
    /*icon*/ ICON_MOD_SHATTER,

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
