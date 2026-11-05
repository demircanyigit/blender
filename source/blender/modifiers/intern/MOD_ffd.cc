/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * FFD 2x2x2, FFD 3x3x3, FFD 4x4x4. The work is done by shared code in the geometry module, which the matching nodes use too.
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
#include "BLI_bounds.hh"
#include "BLI_math_vector.h"
#include "GEO_deform_ops.hh"

namespace blender {

static void init_data_FFDModifierData(ModifierData *md)
{
  FFDModifierData *data = reinterpret_cast<FFDModifierData *>(md);
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
  const FFDModifierData &data = *reinterpret_cast<FFDModifierData *>(md);
  if (data.vertex_group[0] != '\0') {
    r_cddata_masks->vmask |= CD_MASK_MDEFORMVERT;
  }
}

static int ffd_resolution(const int modifier_type)
{
  switch (modifier_type) {
    case eModifierType_FFD3:
      return 3;
    case eModifierType_FFD4:
      return 4;
    default:
      return 2;
  }
}

/** Fit the box around the points. Flat shapes get some depth so the box can bend them. */
static void fit_bounds(const Span<float3> positions, float3 &r_min, float3 &r_max)
{
  const std::optional<Bounds<float3>> bounds = bounds::min_max(positions);
  if (!bounds) {
    return;
  }
  r_min = bounds->min;
  r_max = bounds->max;
  const float3 size = r_max - r_min;
  const float largest = std::max({size.x, size.y, size.z, 0.01f});
  for (int axis = 0; axis < 3; axis++) {
    if (size[axis] < largest * 0.01f) {
      r_min[axis] -= largest * 0.05f;
      r_max[axis] += largest * 0.05f;
    }
  }
}

static void deform_verts(ModifierData *md,
                         const ModifierEvalContext *ctx,
                         Mesh *mesh,
                         MutableSpan<float3> positions)
{
  FFDModifierData &fmd = *reinterpret_cast<FFDModifierData *>(md);
  if (!(fmd.flag & MOD_FFD_CUSTOM_BOUNDS)) {
    float3 min(fmd.bounds_min), max(fmd.bounds_max);
    fit_bounds(positions, min, max);
    copy_v3_v3(fmd.bounds_min, min);
    copy_v3_v3(fmd.bounds_max, max);
    /* The gizmos draw the lattice from the original modifier. */
    if (ModifierData *md_orig = BKE_modifier_get_original(ctx->object, md)) {
      FFDModifierData &orig = *reinterpret_cast<FFDModifierData *>(md_orig);
      if (float3(orig.bounds_min) != min || float3(orig.bounds_max) != max) {
        copy_v3_v3(orig.bounds_min, min);
        copy_v3_v3(orig.bounds_max, max);
      }
    }
  }
  geometry::FFDParams params;
  params.resolution = ffd_resolution(md->type);
  params.bounds_min = float3(fmd.bounds_min);
  params.bounds_max = float3(fmd.bounds_max);
  params.offsets = Span<float3>(reinterpret_cast<const float3 *>(fmd.control_points), 64);
  params.inside_only = fmd.flag & MOD_FFD_INSIDE_ONLY;
  const Array<float> weights = vgroup_weights(
      ctx->object, mesh, fmd.vertex_group, fmd.flag & MOD_FFD_INVERT_VGROUP, positions.size());
  geometry::ffd_deform(positions, weights, params);
}

static void panel_draw_FFD2(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);
  UNUSED_VARS(ob_ptr);

  layout.use_property_split_set(true);
  layout.prop(ptr, "use_custom_bounds", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  if (RNA_boolean_get(ptr, "use_custom_bounds")) {
    layout.prop(ptr, "bounds_min", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  if (RNA_boolean_get(ptr, "use_custom_bounds")) {
    layout.prop(ptr, "bounds_max", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  layout.prop(ptr, "use_inside_only", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.op("OBJECT_OT_ffd_reset_points", IFACE_("Reset Points"), ICON_NONE);
  modifier_vgroup_ui(layout, ptr, &ob_ptr, "vertex_group", "invert_vertex_group", std::nullopt);
  modifier_error_message_draw(layout, ptr);
}

static void panel_register_FFD2(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_FFD2, panel_draw_FFD2);
}

static void panel_draw_FFD3(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);
  UNUSED_VARS(ob_ptr);

  layout.use_property_split_set(true);
  layout.prop(ptr, "use_custom_bounds", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  if (RNA_boolean_get(ptr, "use_custom_bounds")) {
    layout.prop(ptr, "bounds_min", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  if (RNA_boolean_get(ptr, "use_custom_bounds")) {
    layout.prop(ptr, "bounds_max", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  layout.prop(ptr, "use_inside_only", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.op("OBJECT_OT_ffd_reset_points", IFACE_("Reset Points"), ICON_NONE);
  modifier_vgroup_ui(layout, ptr, &ob_ptr, "vertex_group", "invert_vertex_group", std::nullopt);
  modifier_error_message_draw(layout, ptr);
}

static void panel_register_FFD3(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_FFD3, panel_draw_FFD3);
}

static void panel_draw_FFD4(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);
  UNUSED_VARS(ob_ptr);

  layout.use_property_split_set(true);
  layout.prop(ptr, "use_custom_bounds", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  if (RNA_boolean_get(ptr, "use_custom_bounds")) {
    layout.prop(ptr, "bounds_min", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  if (RNA_boolean_get(ptr, "use_custom_bounds")) {
    layout.prop(ptr, "bounds_max", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  layout.prop(ptr, "use_inside_only", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.op("OBJECT_OT_ffd_reset_points", IFACE_("Reset Points"), ICON_NONE);
  modifier_vgroup_ui(layout, ptr, &ob_ptr, "vertex_group", "invert_vertex_group", std::nullopt);
  modifier_error_message_draw(layout, ptr);
}

static void panel_register_FFD4(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_FFD4, panel_draw_FFD4);
}

ModifierTypeInfo modifierType_FFD2 = {
    /*idname*/ "FFD2",
    /*name*/ N_("FFD 2x2x2"),
    /*struct_name*/ "FFDModifierData",
    /*struct_size*/ sizeof(FFDModifierData),
    /*srna*/ &RNA_FFD2Modifier,
    /*type*/ ModifierTypeType::OnlyDeform,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_AcceptsCVs | eModifierTypeFlag_AcceptsVertexCosOnly | eModifierTypeFlag_SupportsEditmode | eModifierTypeFlag_EnableInEditmode,
    /*icon*/ ICON_MOD_FFD_2,

    /*copy_data*/ BKE_modifier_copydata_generic,

    /*deform_verts*/ deform_verts,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ nullptr,
    /*modify_geometry_set*/ nullptr,

    /*init_data*/ init_data_FFDModifierData,
    /*required_data_mask*/ required_data_mask,
    /*free_data*/ nullptr,
    /*is_disabled*/ nullptr,
    /*update_depsgraph*/ nullptr,
    /*depends_on_time*/ nullptr,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ nullptr,
    /*foreach_tex_link*/ nullptr,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ panel_register_FFD2,
    /*blend_write*/ nullptr,
    /*blend_read*/ nullptr,
    /*foreach_cache*/ nullptr,
    /*foreach_working_space_color*/ nullptr,
};

ModifierTypeInfo modifierType_FFD3 = {
    /*idname*/ "FFD3",
    /*name*/ N_("FFD 3x3x3"),
    /*struct_name*/ "FFDModifierData",
    /*struct_size*/ sizeof(FFDModifierData),
    /*srna*/ &RNA_FFD3Modifier,
    /*type*/ ModifierTypeType::OnlyDeform,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_AcceptsCVs | eModifierTypeFlag_AcceptsVertexCosOnly | eModifierTypeFlag_SupportsEditmode | eModifierTypeFlag_EnableInEditmode,
    /*icon*/ ICON_MOD_FFD_3,

    /*copy_data*/ BKE_modifier_copydata_generic,

    /*deform_verts*/ deform_verts,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ nullptr,
    /*modify_geometry_set*/ nullptr,

    /*init_data*/ init_data_FFDModifierData,
    /*required_data_mask*/ required_data_mask,
    /*free_data*/ nullptr,
    /*is_disabled*/ nullptr,
    /*update_depsgraph*/ nullptr,
    /*depends_on_time*/ nullptr,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ nullptr,
    /*foreach_tex_link*/ nullptr,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ panel_register_FFD3,
    /*blend_write*/ nullptr,
    /*blend_read*/ nullptr,
    /*foreach_cache*/ nullptr,
    /*foreach_working_space_color*/ nullptr,
};

ModifierTypeInfo modifierType_FFD4 = {
    /*idname*/ "FFD4",
    /*name*/ N_("FFD 4x4x4"),
    /*struct_name*/ "FFDModifierData",
    /*struct_size*/ sizeof(FFDModifierData),
    /*srna*/ &RNA_FFD4Modifier,
    /*type*/ ModifierTypeType::OnlyDeform,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_AcceptsCVs | eModifierTypeFlag_AcceptsVertexCosOnly | eModifierTypeFlag_SupportsEditmode | eModifierTypeFlag_EnableInEditmode,
    /*icon*/ ICON_MOD_FFD_4,

    /*copy_data*/ BKE_modifier_copydata_generic,

    /*deform_verts*/ deform_verts,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ nullptr,
    /*modify_geometry_set*/ nullptr,

    /*init_data*/ init_data_FFDModifierData,
    /*required_data_mask*/ required_data_mask,
    /*free_data*/ nullptr,
    /*is_disabled*/ nullptr,
    /*update_depsgraph*/ nullptr,
    /*depends_on_time*/ nullptr,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ nullptr,
    /*foreach_tex_link*/ nullptr,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ panel_register_FFD4,
    /*blend_write*/ nullptr,
    /*blend_read*/ nullptr,
    /*foreach_cache*/ nullptr,
    /*foreach_working_space_color*/ nullptr,
};

}  // namespace blender
