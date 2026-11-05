/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Chamfer Spline. The work is done by shared code in the geometry module, which the matching nodes use too.
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
#include "BKE_curves.hh"
#include "BKE_geometry_set.hh"
#include "DNA_curves_types.h"
#include "GEO_curve_modifiers.hh"

namespace blender {

static void init_data_ChamferSplineModifierData(ModifierData *md)
{
  ChamferSplineModifierData *data = reinterpret_cast<ChamferSplineModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

static void modify_geometry_set(ModifierData *md,
                                const ModifierEvalContext * /*ctx*/,
                                bke::GeometrySet *geometry_set)
{
  const ChamferSplineModifierData &cmd = *reinterpret_cast<ChamferSplineModifierData *>(md);
  if (!geometry_set->has_curves()) {
    return;
  }
  Curves *curves_id = geometry_set->get_curves_for_write();
  bke::CurvesGeometry &curves = curves_id->geometry.wrap();
  curves = geometry::chamfer_curves(
      curves, cmd.radius, cmd.segments, cmd.flag & MOD_CHAMFER_SPLINE_LIMIT);
}

static void panel_draw_ChamferSpline(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);
  UNUSED_VARS(ob_ptr);

  layout.use_property_split_set(true);
  layout.prop(ptr, "radius", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "segments", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "use_limit_radius", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  modifier_error_message_draw(layout, ptr);
}

static void panel_register_ChamferSpline(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_ChamferSpline, panel_draw_ChamferSpline);
}

ModifierTypeInfo modifierType_ChamferSpline = {
    /*idname*/ "ChamferSpline",
    /*name*/ N_("Chamfer Spline"),
    /*struct_name*/ "ChamferSplineModifierData",
    /*struct_size*/ sizeof(ChamferSplineModifierData),
    /*srna*/ &RNA_ChamferSplineModifier,
    /*type*/ ModifierTypeType::Constructive,
    /*flags*/ eModifierTypeFlag_AcceptsCurves,
    /*icon*/ ICON_MOD_CHAMFER_SPLINE,

    /*copy_data*/ BKE_modifier_copydata_generic,

    /*deform_verts*/ nullptr,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ nullptr,
    /*modify_geometry_set*/ modify_geometry_set,

    /*init_data*/ init_data_ChamferSplineModifierData,
    /*required_data_mask*/ nullptr,
    /*free_data*/ nullptr,
    /*is_disabled*/ nullptr,
    /*update_depsgraph*/ nullptr,
    /*depends_on_time*/ nullptr,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ nullptr,
    /*foreach_tex_link*/ nullptr,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ panel_register_ChamferSpline,
    /*blend_write*/ nullptr,
    /*blend_read*/ nullptr,
    /*foreach_cache*/ nullptr,
    /*foreach_working_space_color*/ nullptr,
};

}  // namespace blender
