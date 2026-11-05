/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Randomize Elements. The work is done by shared code in the geometry module, which the matching nodes use too.
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

static void init_data_RandomizeElementsModifierData(ModifierData *md)
{
  RandomizeElementsModifierData *data = reinterpret_cast<RandomizeElementsModifierData *>(md);
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
  const RandomizeElementsModifierData &data = *reinterpret_cast<RandomizeElementsModifierData *>(md);
  if (data.vertex_group[0] != '\0') {
    r_cddata_masks->vmask |= CD_MASK_MDEFORMVERT;
  }
}

static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  const RandomizeElementsModifierData &rmd = *reinterpret_cast<RandomizeElementsModifierData *>(
      md);
  geometry::RandomizeElementsParams params;
  params.element = geometry::MeshElement(rmd.element);
  params.uv_map = rmd.uv_map;
  params.seed = rmd.seed;
  params.use_geometry = rmd.flag & MOD_RANDOMIZE_ELEMENTS_GEOMETRY;
  params.use_uvs = rmd.flag & MOD_RANDOMIZE_ELEMENTS_UVS;
  params.use_color = rmd.flag & MOD_RANDOMIZE_ELEMENTS_COLOR;
  params.pivot = geometry::ElementPivot(rmd.pivot);
  params.rotation = float3(rmd.rotation);
  params.normal_rotation = rmd.normal_rotation;
  params.scale_min = rmd.scale_min;
  params.scale_max = rmd.scale_max;
  params.uniform_scale = rmd.flag & MOD_RANDOMIZE_ELEMENTS_UNIFORM_SCALE;
  params.offset = float3(rmd.offset);
  params.normal_offset = rmd.normal_offset;
  params.uv_rotation = rmd.uv_rotation;
  params.uv_scale_min = rmd.uv_scale_min;
  params.uv_scale_max = rmd.uv_scale_max;
  params.uv_offset = float2(rmd.uv_offset);
  params.color_attribute = rmd.color_attribute;
  params.color_mode = geometry::ElementColorMode(rmd.color_mode);
  params.saturation = rmd.saturation;
  params.value_min = rmd.value_min;
  params.value_max = rmd.value_max;
  params.color_blend = rmd.color_blend;
  const Array<float> weights = vgroup_weights(ctx->object,
                                              mesh,
                                              rmd.vertex_group,
                                              rmd.flag & MOD_RANDOMIZE_ELEMENTS_INVERT_VGROUP,
                                              mesh->verts_num);
  params.weights = weights;
  Mesh *result = geometry::randomize_elements(*mesh, params);
  if (result == nullptr) {
    return mesh;
  }
  geometry::debug_randomize_mesh_order(result);
  return result;
}

static void panel_draw_RandomizeElements(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);
  UNUSED_VARS(ob_ptr);

  layout.prop(ptr, "element", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.use_property_split_set(true);
  if (RNA_enum_get(ptr, "element") == 2 || RNA_boolean_get(ptr, "use_uvs")) {
    layout.prop(ptr, "uv_map", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  layout.prop(ptr, "seed", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  modifier_vgroup_ui(layout, ptr, &ob_ptr, "vertex_group", "invert_vertex_group", std::nullopt);
  modifier_error_message_draw(layout, ptr);
}

static void panel_header_RandomizeElements_geometry(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, nullptr);
  layout.prop(ptr, "use_geometry", UI_ITEM_NONE, std::nullopt, ICON_NONE);
}

static void panel_draw_RandomizeElements_geometry(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);
  UNUSED_VARS(ob_ptr);
  layout.active_set(RNA_boolean_get(ptr, "use_geometry"));

  layout.use_property_split_set(true);
  layout.prop(ptr, "pivot", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "rotation", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "normal_rotation", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "scale_min", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "scale_max", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "use_uniform_scale", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "offset", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "normal_offset", UI_ITEM_NONE, std::nullopt, ICON_NONE);
}

static void panel_header_RandomizeElements_uvs(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, nullptr);
  layout.prop(ptr, "use_uvs", UI_ITEM_NONE, std::nullopt, ICON_NONE);
}

static void panel_draw_RandomizeElements_uvs(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);
  UNUSED_VARS(ob_ptr);
  layout.active_set(RNA_boolean_get(ptr, "use_uvs"));

  layout.use_property_split_set(true);
  layout.prop(ptr, "uv_rotation", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "uv_scale_min", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "uv_scale_max", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "uv_offset", UI_ITEM_NONE, std::nullopt, ICON_NONE);
}

static void panel_header_RandomizeElements_color(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, nullptr);
  layout.prop(ptr, "use_color", UI_ITEM_NONE, std::nullopt, ICON_NONE);
}

static void panel_draw_RandomizeElements_color(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);
  UNUSED_VARS(ob_ptr);
  layout.active_set(RNA_boolean_get(ptr, "use_color"));

  layout.use_property_split_set(true);
  layout.prop(ptr, "color_attribute", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "color_mode", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  if (RNA_enum_get(ptr, "color_mode") == 0) {
    layout.prop(ptr, "saturation", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  layout.prop(ptr, "value_min", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "value_max", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "color_blend", UI_ITEM_NONE, std::nullopt, ICON_NONE);
}

static void panel_register_RandomizeElements(ARegionType *region_type)
{
  PanelType *panel_type = modifier_panel_register(region_type, eModifierType_RandomizeElements, panel_draw_RandomizeElements);
  modifier_subpanel_register(region_type, "geometry", "Geometry", panel_header_RandomizeElements_geometry, panel_draw_RandomizeElements_geometry, panel_type);
  modifier_subpanel_register(region_type, "uvs", "UVs", panel_header_RandomizeElements_uvs, panel_draw_RandomizeElements_uvs, panel_type);
  modifier_subpanel_register(region_type, "color", "Color", panel_header_RandomizeElements_color, panel_draw_RandomizeElements_color, panel_type);
}

ModifierTypeInfo modifierType_RandomizeElements = {
    /*idname*/ "RandomizeElements",
    /*name*/ N_("Randomize Elements"),
    /*struct_name*/ "RandomizeElementsModifierData",
    /*struct_size*/ sizeof(RandomizeElementsModifierData),
    /*srna*/ &RNA_RandomizeElementsModifier,
    /*type*/ ModifierTypeType::Constructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode | eModifierTypeFlag_EnableInEditmode,
    /*icon*/ ICON_MOD_RANDOMIZE_ELEMENTS,

    /*copy_data*/ BKE_modifier_copydata_generic,

    /*deform_verts*/ nullptr,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ modify_mesh,
    /*modify_geometry_set*/ nullptr,

    /*init_data*/ init_data_RandomizeElementsModifierData,
    /*required_data_mask*/ required_data_mask,
    /*free_data*/ nullptr,
    /*is_disabled*/ nullptr,
    /*update_depsgraph*/ nullptr,
    /*depends_on_time*/ nullptr,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ nullptr,
    /*foreach_tex_link*/ nullptr,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ panel_register_RandomizeElements,
    /*blend_write*/ nullptr,
    /*blend_read*/ nullptr,
    /*foreach_cache*/ nullptr,
    /*foreach_working_space_color*/ nullptr,
};

}  // namespace blender
