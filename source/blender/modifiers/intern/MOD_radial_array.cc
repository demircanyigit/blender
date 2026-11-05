/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Radial Array. The work is done by shared code in the geometry module, which the matching nodes use too.
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
#include "GEO_radial_array.hh"
#include "GEO_randomize.hh"

namespace blender {

static void init_data_RadialArrayModifierData(ModifierData *md)
{
  RadialArrayModifierData *data = reinterpret_cast<RadialArrayModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext * /*ctx*/, Mesh *mesh)
{
  const RadialArrayModifierData &rmd = *reinterpret_cast<RadialArrayModifierData *>(md);
  geometry::RadialArrayParams params;
  params.count = rmd.count;
  params.angle = rmd.angle;
  params.axis = rmd.axis;
  params.center = float3(rmd.center);
  params.radius = rmd.radius;
  params.step = rmd.step;
  params.rotate_copies = rmd.flag & MOD_RADIAL_ARRAY_ROTATE;
  params.merge = rmd.flag & MOD_RADIAL_ARRAY_MERGE;
  params.merge_distance = rmd.merge_distance;
  Mesh *result = geometry::radial_array(*mesh, params);
  if (result == nullptr) {
    return mesh;
  }
  BKE_mesh_copy_parameters_for_eval(result, mesh);
  geometry::debug_randomize_mesh_order(result);
  return result;
}

static void panel_draw_RadialArray(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);
  UNUSED_VARS(ob_ptr);

  layout.prop(ptr, "axis", ui::ITEM_R_EXPAND, std::nullopt, ICON_NONE);
  layout.use_property_split_set(true);
  layout.prop(ptr, "count", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "angle", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "center", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "radius", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "step", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "use_rotate_copies", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "use_merge", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  if (RNA_boolean_get(ptr, "use_merge")) {
    layout.prop(ptr, "merge_distance", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  modifier_error_message_draw(layout, ptr);
}

static void panel_register_RadialArray(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_RadialArray, panel_draw_RadialArray);
}

ModifierTypeInfo modifierType_RadialArray = {
    /*idname*/ "RadialArray",
    /*name*/ N_("Radial Array"),
    /*struct_name*/ "RadialArrayModifierData",
    /*struct_size*/ sizeof(RadialArrayModifierData),
    /*srna*/ &RNA_RadialArrayModifier,
    /*type*/ ModifierTypeType::Constructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode | eModifierTypeFlag_EnableInEditmode,
    /*icon*/ ICON_MOD_RADIAL_ARRAY,

    /*copy_data*/ BKE_modifier_copydata_generic,

    /*deform_verts*/ nullptr,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ modify_mesh,
    /*modify_geometry_set*/ nullptr,

    /*init_data*/ init_data_RadialArrayModifierData,
    /*required_data_mask*/ nullptr,
    /*free_data*/ nullptr,
    /*is_disabled*/ nullptr,
    /*update_depsgraph*/ nullptr,
    /*depends_on_time*/ nullptr,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ nullptr,
    /*foreach_tex_link*/ nullptr,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ panel_register_RadialArray,
    /*blend_write*/ nullptr,
    /*blend_read*/ nullptr,
    /*foreach_cache*/ nullptr,
    /*foreach_working_space_color*/ nullptr,
};

}  // namespace blender
