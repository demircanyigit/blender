/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Bend, Twist, Taper, Stretch. The work is done by shared code in the geometry module, which the matching nodes use too.
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
#include "BKE_curves.hh"
#include "BKE_geometry_set.hh"
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
#include "GEO_axis_deform.hh"

namespace blender {

static void init_data_AxisDeformModifierData(ModifierData *md)
{
  AxisDeformModifierData *data = reinterpret_cast<AxisDeformModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

static geometry::AxisDeformType axis_deform_type(const int modifier_type)
{
  switch (modifier_type) {
    case eModifierType_Twist:
      return geometry::AxisDeformType::Twist;
    case eModifierType_Taper:
      return geometry::AxisDeformType::Taper;
    case eModifierType_Stretch:
      return geometry::AxisDeformType::Stretch;
    default:
      return geometry::AxisDeformType::Bend;
  }
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

static geometry::AxisDeformParams deform_params(const ModifierData *md)
{
  const AxisDeformModifierData &amd = *reinterpret_cast<const AxisDeformModifierData *>(md);
  geometry::AxisDeformParams params;
  params.type = axis_deform_type(md->type);
  params.axis = amd.axis;
  params.center = float3(amd.center);
  params.rotation = float3(amd.rotation);
  params.angle = amd.angle;
  params.direction = amd.direction;
  params.amount = amd.amount;
  params.curve = amd.curve;
  params.use_limits = amd.flag & MOD_AXIS_DEFORM_USE_LIMITS;
  params.lower = amd.lower;
  params.upper = amd.upper;
  return params;
}

static void deform_verts(ModifierData *md,
                         const ModifierEvalContext *ctx,
                         Mesh *mesh,
                         MutableSpan<float3> positions)
{
  const AxisDeformModifierData &amd = *reinterpret_cast<AxisDeformModifierData *>(md);
  const geometry::AxisDeformParams params = deform_params(md);
  const Array<float> weights = vgroup_weights(ctx->object,
                                              mesh,
                                              amd.vertex_group,
                                              amd.flag & MOD_AXIS_DEFORM_INVERT_VGROUP,
                                              positions.size());
  geometry::axis_deform(positions, params, weights);
}

/** Curves objects: move the control points. */
static void modify_geometry_set(ModifierData *md,
                                const ModifierEvalContext * /*ctx*/,
                                bke::GeometrySet *geometry_set)
{
  if (!geometry_set->has_curves()) {
    return;
  }
  const geometry::AxisDeformParams params = deform_params(md);
  bke::CurvesGeometry &curves = geometry_set->get_curves_for_write()->geometry.wrap();
  geometry::axis_deform(curves.positions_for_write(), params);
  curves.tag_positions_changed();
}

static void required_data_mask(ModifierData *md, CustomData_MeshMasks *r_cddata_masks)
{
  const AxisDeformModifierData &amd = *reinterpret_cast<AxisDeformModifierData *>(md);
  if (amd.vertex_group[0] != '\0') {
    r_cddata_masks->vmask |= CD_MASK_MDEFORMVERT;
  }
}

static void panel_draw_Bend(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);
  UNUSED_VARS(ob_ptr);

  layout.prop(ptr, "deform_axis", ui::ITEM_R_EXPAND, std::nullopt, ICON_NONE);
  layout.use_property_split_set(true);
  layout.prop(ptr, "angle", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "direction", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "center", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "rotation", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "use_limits", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  if (RNA_boolean_get(ptr, "use_limits")) {
    layout.prop(ptr, "lower_limit", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  if (RNA_boolean_get(ptr, "use_limits")) {
    layout.prop(ptr, "upper_limit", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  modifier_vgroup_ui(layout, ptr, &ob_ptr, "vertex_group", "invert_vertex_group", std::nullopt);
  modifier_error_message_draw(layout, ptr);
}

static void panel_register_Bend(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_Bend, panel_draw_Bend);
}

static void panel_draw_Twist(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);
  UNUSED_VARS(ob_ptr);

  layout.prop(ptr, "deform_axis", ui::ITEM_R_EXPAND, std::nullopt, ICON_NONE);
  layout.use_property_split_set(true);
  layout.prop(ptr, "angle", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "center", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "rotation", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "use_limits", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  if (RNA_boolean_get(ptr, "use_limits")) {
    layout.prop(ptr, "lower_limit", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  if (RNA_boolean_get(ptr, "use_limits")) {
    layout.prop(ptr, "upper_limit", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  modifier_vgroup_ui(layout, ptr, &ob_ptr, "vertex_group", "invert_vertex_group", std::nullopt);
  modifier_error_message_draw(layout, ptr);
}

static void panel_register_Twist(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_Twist, panel_draw_Twist);
}

static void panel_draw_Taper(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);
  UNUSED_VARS(ob_ptr);

  layout.prop(ptr, "deform_axis", ui::ITEM_R_EXPAND, std::nullopt, ICON_NONE);
  layout.use_property_split_set(true);
  layout.prop(ptr, "amount", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "curve", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "center", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "rotation", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "use_limits", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  if (RNA_boolean_get(ptr, "use_limits")) {
    layout.prop(ptr, "lower_limit", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  if (RNA_boolean_get(ptr, "use_limits")) {
    layout.prop(ptr, "upper_limit", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  modifier_vgroup_ui(layout, ptr, &ob_ptr, "vertex_group", "invert_vertex_group", std::nullopt);
  modifier_error_message_draw(layout, ptr);
}

static void panel_register_Taper(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_Taper, panel_draw_Taper);
}

static void panel_draw_Stretch(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);
  UNUSED_VARS(ob_ptr);

  layout.prop(ptr, "deform_axis", ui::ITEM_R_EXPAND, std::nullopt, ICON_NONE);
  layout.use_property_split_set(true);
  layout.prop(ptr, "amount", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "curve", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "center", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "rotation", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "use_limits", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  if (RNA_boolean_get(ptr, "use_limits")) {
    layout.prop(ptr, "lower_limit", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  if (RNA_boolean_get(ptr, "use_limits")) {
    layout.prop(ptr, "upper_limit", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  modifier_vgroup_ui(layout, ptr, &ob_ptr, "vertex_group", "invert_vertex_group", std::nullopt);
  modifier_error_message_draw(layout, ptr);
}

static void panel_register_Stretch(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_Stretch, panel_draw_Stretch);
}

ModifierTypeInfo modifierType_Bend = {
    /*idname*/ "Bend",
    /*name*/ N_("Bend"),
    /*struct_name*/ "AxisDeformModifierData",
    /*struct_size*/ sizeof(AxisDeformModifierData),
    /*srna*/ &RNA_BendModifier,
    /*type*/ ModifierTypeType::OnlyDeform,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_AcceptsCVs | eModifierTypeFlag_AcceptsCurves | eModifierTypeFlag_AcceptsVertexCosOnly | eModifierTypeFlag_SupportsEditmode | eModifierTypeFlag_EnableInEditmode,
    /*icon*/ ICON_MOD_BEND,

    /*copy_data*/ BKE_modifier_copydata_generic,

    /*deform_verts*/ deform_verts,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ nullptr,
    /*modify_geometry_set*/ modify_geometry_set,

    /*init_data*/ init_data_AxisDeformModifierData,
    /*required_data_mask*/ required_data_mask,
    /*free_data*/ nullptr,
    /*is_disabled*/ nullptr,
    /*update_depsgraph*/ nullptr,
    /*depends_on_time*/ nullptr,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ nullptr,
    /*foreach_tex_link*/ nullptr,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ panel_register_Bend,
    /*blend_write*/ nullptr,
    /*blend_read*/ nullptr,
    /*foreach_cache*/ nullptr,
    /*foreach_working_space_color*/ nullptr,
};

ModifierTypeInfo modifierType_Twist = {
    /*idname*/ "Twist",
    /*name*/ N_("Twist"),
    /*struct_name*/ "AxisDeformModifierData",
    /*struct_size*/ sizeof(AxisDeformModifierData),
    /*srna*/ &RNA_TwistModifier,
    /*type*/ ModifierTypeType::OnlyDeform,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_AcceptsCVs | eModifierTypeFlag_AcceptsCurves | eModifierTypeFlag_AcceptsVertexCosOnly | eModifierTypeFlag_SupportsEditmode | eModifierTypeFlag_EnableInEditmode,
    /*icon*/ ICON_MOD_TWIST,

    /*copy_data*/ BKE_modifier_copydata_generic,

    /*deform_verts*/ deform_verts,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ nullptr,
    /*modify_geometry_set*/ modify_geometry_set,

    /*init_data*/ init_data_AxisDeformModifierData,
    /*required_data_mask*/ required_data_mask,
    /*free_data*/ nullptr,
    /*is_disabled*/ nullptr,
    /*update_depsgraph*/ nullptr,
    /*depends_on_time*/ nullptr,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ nullptr,
    /*foreach_tex_link*/ nullptr,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ panel_register_Twist,
    /*blend_write*/ nullptr,
    /*blend_read*/ nullptr,
    /*foreach_cache*/ nullptr,
    /*foreach_working_space_color*/ nullptr,
};

ModifierTypeInfo modifierType_Taper = {
    /*idname*/ "Taper",
    /*name*/ N_("Taper"),
    /*struct_name*/ "AxisDeformModifierData",
    /*struct_size*/ sizeof(AxisDeformModifierData),
    /*srna*/ &RNA_TaperModifier,
    /*type*/ ModifierTypeType::OnlyDeform,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_AcceptsCVs | eModifierTypeFlag_AcceptsCurves | eModifierTypeFlag_AcceptsVertexCosOnly | eModifierTypeFlag_SupportsEditmode | eModifierTypeFlag_EnableInEditmode,
    /*icon*/ ICON_MOD_TAPER,

    /*copy_data*/ BKE_modifier_copydata_generic,

    /*deform_verts*/ deform_verts,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ nullptr,
    /*modify_geometry_set*/ modify_geometry_set,

    /*init_data*/ init_data_AxisDeformModifierData,
    /*required_data_mask*/ required_data_mask,
    /*free_data*/ nullptr,
    /*is_disabled*/ nullptr,
    /*update_depsgraph*/ nullptr,
    /*depends_on_time*/ nullptr,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ nullptr,
    /*foreach_tex_link*/ nullptr,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ panel_register_Taper,
    /*blend_write*/ nullptr,
    /*blend_read*/ nullptr,
    /*foreach_cache*/ nullptr,
    /*foreach_working_space_color*/ nullptr,
};

ModifierTypeInfo modifierType_Stretch = {
    /*idname*/ "Stretch",
    /*name*/ N_("Stretch"),
    /*struct_name*/ "AxisDeformModifierData",
    /*struct_size*/ sizeof(AxisDeformModifierData),
    /*srna*/ &RNA_StretchModifier,
    /*type*/ ModifierTypeType::OnlyDeform,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_AcceptsCVs | eModifierTypeFlag_AcceptsCurves | eModifierTypeFlag_AcceptsVertexCosOnly | eModifierTypeFlag_SupportsEditmode | eModifierTypeFlag_EnableInEditmode,
    /*icon*/ ICON_MOD_STRETCH,

    /*copy_data*/ BKE_modifier_copydata_generic,

    /*deform_verts*/ deform_verts,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ nullptr,
    /*modify_geometry_set*/ modify_geometry_set,

    /*init_data*/ init_data_AxisDeformModifierData,
    /*required_data_mask*/ required_data_mask,
    /*free_data*/ nullptr,
    /*is_disabled*/ nullptr,
    /*update_depsgraph*/ nullptr,
    /*depends_on_time*/ nullptr,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ nullptr,
    /*foreach_tex_link*/ nullptr,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ panel_register_Stretch,
    /*blend_write*/ nullptr,
    /*blend_read*/ nullptr,
    /*foreach_cache*/ nullptr,
    /*foreach_working_space_color*/ nullptr,
};

}  // namespace blender
