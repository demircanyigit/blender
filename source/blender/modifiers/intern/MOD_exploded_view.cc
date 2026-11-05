/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Exploded View. The work is done by shared code in the geometry module, which the matching nodes use too.
 */

#include <algorithm>

#include "BLI_array.hh"
#include "BLI_index_mask.hh"
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
#include "GEO_mesh_elements.hh"
#include "GEO_randomize.hh"

namespace blender {

static void init_data_ExplodedViewModifierData(ModifierData *md)
{
  ExplodedViewModifierData *data = reinterpret_cast<ExplodedViewModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

/** Weights of a vertex group, or an empty array when there is none. */
static Array<float> vgroup_weights(const Object *ob,
                                   const Mesh *mesh,
                                   const char *name,
                                   const bool invert,
                                   const int verts_num)
{
  if (mesh == nullptr || name[0] == '\0') {
    return {};
  }
  const MDeformVert *dvert = nullptr;
  int defgrp_index = -1;
  MOD_get_vgroup(ob, mesh, name, &dvert, &defgrp_index);
  if (dvert == nullptr || defgrp_index == -1) {
    return {};
  }
  Array<float> weights(verts_num);
  for (const int i : IndexRange(verts_num)) {
    const float weight = BKE_defvert_find_weight(&dvert[i], defgrp_index);
    weights[i] = invert ? 1.0f - weight : weight;
  }
  return weights;
}

static void required_data_mask(ModifierData *md, CustomData_MeshMasks *r_cddata_masks)
{
  const ExplodedViewModifierData &data = *reinterpret_cast<ExplodedViewModifierData *>(md);
  if (data.vertex_group[0] != '\0') {
    r_cddata_masks->vmask |= CD_MASK_MDEFORMVERT;
  }
}

static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  const ExplodedViewModifierData &emd = *reinterpret_cast<ExplodedViewModifierData *>(md);
  geometry::ExplodedViewParams params;
  params.element = geometry::MeshElement(emd.element);
  params.uv_map = emd.uv_map;
  params.direction = geometry::ExplodeDirection(emd.direction);
  params.center = float3(emd.center);
  params.factor = emd.factor;
  params.distance = emd.distance;
  params.randomness = emd.randomness;
  params.tumble = emd.tumble;
  params.seed = emd.seed;
  const Array<float> weights = vgroup_weights(ctx->object,
                                              mesh,
                                              emd.vertex_group,
                                              emd.flag & MOD_EXPLODED_VIEW_INVERT_VGROUP,
                                              mesh->verts_num);
  params.weights = weights;
  Mesh *result = geometry::exploded_view(*mesh, params);
  if (result == nullptr) {
    return mesh;
  }
  geometry::debug_randomize_mesh_order(result);
  return result;
}

static void panel_draw_ExplodedView(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);
  UNUSED_VARS(ob_ptr);

  layout.prop(ptr, "element", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.use_property_split_set(true);
  if (RNA_enum_get(ptr, "element") == 2) {
    layout.prop(ptr, "uv_map", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  layout.prop(ptr, "direction", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  if (RNA_enum_get(ptr, "direction") == 0) {
    layout.prop(ptr, "center", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  layout.prop(ptr, "factor", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "distance", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "randomness", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "tumble", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "seed", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  modifier_vgroup_ui(layout, ptr, &ob_ptr, "vertex_group", "invert_vertex_group", std::nullopt);
  modifier_error_message_draw(layout, ptr);
}

static void panel_register_ExplodedView(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_ExplodedView, panel_draw_ExplodedView);
}

ModifierTypeInfo modifierType_ExplodedView = {
    /*idname*/ "ExplodedView",
    /*name*/ N_("Exploded View"),
    /*struct_name*/ "ExplodedViewModifierData",
    /*struct_size*/ sizeof(ExplodedViewModifierData),
    /*srna*/ &RNA_ExplodedViewModifier,
    /*type*/ ModifierTypeType::Constructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode | eModifierTypeFlag_EnableInEditmode,
    /*icon*/ ICON_MOD_EXPLODED_VIEW,

    /*copy_data*/ BKE_modifier_copydata_generic,

    /*deform_verts*/ nullptr,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ modify_mesh,
    /*modify_geometry_set*/ nullptr,

    /*init_data*/ init_data_ExplodedViewModifierData,
    /*required_data_mask*/ required_data_mask,
    /*free_data*/ nullptr,
    /*is_disabled*/ nullptr,
    /*update_depsgraph*/ nullptr,
    /*depends_on_time*/ nullptr,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ nullptr,
    /*foreach_tex_link*/ nullptr,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ panel_register_ExplodedView,
    /*blend_write*/ nullptr,
    /*blend_read*/ nullptr,
    /*foreach_cache*/ nullptr,
    /*foreach_working_space_color*/ nullptr,
};

}  // namespace blender
