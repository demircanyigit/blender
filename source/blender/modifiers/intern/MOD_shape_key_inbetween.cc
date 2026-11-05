/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Shape Key In-between. The work is done by shared code in the geometry module, which the matching nodes use too.
 */

#include <algorithm>

#include "BLI_array.hh"
#include "BLI_math_euler.hh"
#include "BLI_math_matrix.hh"
#include "BLI_math_vector.hh"
#include "BLI_string.h"
#include "BLT_translation.hh"

#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_types.h"
#include "DNA_screen_types.h"

#include "BKE_attribute.hh"
#include "BKE_deform.hh"
#include "BKE_lib_query.hh"
#include "BKE_mesh.hh"
#include "BKE_modifier.hh"

#include "DEG_depsgraph_build.hh"
#include "DEG_depsgraph_query.hh"

#include "UI_interface_layout.hh"
#include "UI_resources.hh"

#include "RNA_access.hh"
#include "RNA_prototypes.hh"

#include "MOD_modifiertypes.hh"
#include "MOD_ui_common.hh"
#include "MOD_util.hh"
#include "DNA_key_types.h"
#include "BKE_key.hh"
#include "BLI_listbase.h"

namespace blender {

static void init_data_ShapeKeyInbetweenModifierData(ModifierData *md)
{
  ShapeKeyInbetweenModifierData *data = reinterpret_cast<ShapeKeyInbetweenModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

static void deform_verts(ModifierData *md,
                         const ModifierEvalContext *ctx,
                         Mesh * /*mesh*/,
                         MutableSpan<float3> positions)
{
  const ShapeKeyInbetweenModifierData &smd = *reinterpret_cast<ShapeKeyInbetweenModifierData *>(
      md);
  const Key *key = BKE_key_from_object(ctx->object);
  if (key == nullptr || key->refkey == nullptr) {
    BKE_modifier_set_warning(ctx->object, md, "The object has no shape keys");
    return;
  }
  Vector<const KeyBlock *> blocks;
  for (const KeyBlock *kb = static_cast<const KeyBlock *>(key->block.first); kb; kb = kb->next) {
    blocks.append(kb);
  }
  const int last = int(blocks.size()) - 1;
  const int start = std::clamp(smd.start_key, 0, last);
  const int end = smd.end_key <= 0 ? last : std::clamp(smd.end_key, start, last);
  const int count = end - start + 1;
  const float position = std::clamp(smd.value, 0.0f, 1.0f) * float(count - 1);
  const int i0 = std::min(int(position), count - 1);
  const int i1 = std::min(i0 + 1, count - 1);
  const float factor = position - float(i0);

  const KeyBlock *basis = key->refkey;
  const KeyBlock *a = blocks[start + i0];
  const KeyBlock *b = blocks[start + i1];
  if (basis->totelem != positions.size() || a->totelem != positions.size() ||
      b->totelem != positions.size())
  {
    BKE_modifier_set_warning(ctx->object, md, "The shape keys don't match the mesh");
    return;
  }
  const Span<float3> basis_co(static_cast<const float3 *>(basis->data), basis->totelem);
  const Span<float3> a_co(static_cast<const float3 *>(a->data), a->totelem);
  const Span<float3> b_co(static_cast<const float3 *>(b->data), b->totelem);
  for (const int i : positions.index_range()) {
    positions[i] += math::interpolate(a_co[i], b_co[i], factor) - basis_co[i];
  }
}

static void panel_draw_ShapeKeyInbetween(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);
  UNUSED_VARS(ob_ptr);

  layout.use_property_split_set(true);
  layout.prop(ptr, "value", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "start_key", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "end_key", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  modifier_error_message_draw(layout, ptr);
}

static void panel_register_ShapeKeyInbetween(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_ShapeKeyInbetween, panel_draw_ShapeKeyInbetween);
}

ModifierTypeInfo modifierType_ShapeKeyInbetween = {
    /*idname*/ "ShapeKeyInbetween",
    /*name*/ N_("Shape Key In-between"),
    /*struct_name*/ "ShapeKeyInbetweenModifierData",
    /*struct_size*/ sizeof(ShapeKeyInbetweenModifierData),
    /*srna*/ &RNA_ShapeKeyInbetweenModifier,
    /*type*/ ModifierTypeType::OnlyDeform,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_AcceptsVertexCosOnly | eModifierTypeFlag_SupportsEditmode,
    /*icon*/ ICON_MOD_SHAPEKEY_INBETWEEN,

    /*copy_data*/ BKE_modifier_copydata_generic,

    /*deform_verts*/ deform_verts,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ nullptr,
    /*modify_geometry_set*/ nullptr,

    /*init_data*/ init_data_ShapeKeyInbetweenModifierData,
    /*required_data_mask*/ nullptr,
    /*free_data*/ nullptr,
    /*is_disabled*/ nullptr,
    /*update_depsgraph*/ nullptr,
    /*depends_on_time*/ nullptr,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ nullptr,
    /*foreach_tex_link*/ nullptr,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ panel_register_ShapeKeyInbetween,
    /*blend_write*/ nullptr,
    /*blend_read*/ nullptr,
    /*foreach_cache*/ nullptr,
    /*foreach_working_space_color*/ nullptr,
};

}  // namespace blender
