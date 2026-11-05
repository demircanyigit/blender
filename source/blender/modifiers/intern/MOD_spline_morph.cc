/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Spline Morph. The work is done by shared code in the geometry module, which the matching nodes use too.
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
#include "BKE_geometry_set_instances.hh"

namespace blender {

static void init_data_SplineMorphModifierData(ModifierData *md)
{
  SplineMorphModifierData *data = reinterpret_cast<SplineMorphModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

static bool is_disabled(const Scene * /*scene*/, ModifierData *md, bool /*use_render_params*/)
{
  return reinterpret_cast<SplineMorphModifierData *>(md)->target == nullptr;
}

static void foreach_ID_link(ModifierData *md, Object *ob, IDWalkFunc walk, void *user_data)
{
  SplineMorphModifierData *smd = reinterpret_cast<SplineMorphModifierData *>(md);
  walk(user_data, ob, reinterpret_cast<ID **>(&smd->target), IDWALK_CB_NOP);
}

static void update_depsgraph(ModifierData *md, const ModifierUpdateDepsgraphContext *ctx)
{
  SplineMorphModifierData *smd = reinterpret_cast<SplineMorphModifierData *>(md);
  if (smd->target != nullptr) {
    DEG_add_object_relation(ctx->node, smd->target, DEG_OB_COMP_GEOMETRY, "Spline Morph Modifier");
  }
}

static void modify_geometry_set(ModifierData *md,
                                const ModifierEvalContext *ctx,
                                bke::GeometrySet *geometry_set)
{
  const SplineMorphModifierData &smd = *reinterpret_cast<SplineMorphModifierData *>(md);
  if (!geometry_set->has_curves() || smd.target == nullptr) {
    return;
  }
  const bke::GeometrySet target_set = bke::object_get_evaluated_geometry_set(*smd.target);
  const Curves *target_curves = target_set.get_curves();
  if (target_curves == nullptr) {
    BKE_modifier_set_warning(ctx->object, md, "The target has no curves");
    return;
  }
  Curves *curves_id = geometry_set->get_curves_for_write();
  geometry::morph_curves(curves_id->geometry.wrap(), target_curves->geometry.wrap(), smd.factor);
}

static void panel_draw_SplineMorph(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);
  UNUSED_VARS(ob_ptr);

  layout.use_property_split_set(true);
  layout.prop(ptr, "target", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "factor", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  modifier_error_message_draw(layout, ptr);
}

static void panel_register_SplineMorph(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_SplineMorph, panel_draw_SplineMorph);
}

ModifierTypeInfo modifierType_SplineMorph = {
    /*idname*/ "SplineMorph",
    /*name*/ N_("Spline Morph"),
    /*struct_name*/ "SplineMorphModifierData",
    /*struct_size*/ sizeof(SplineMorphModifierData),
    /*srna*/ &RNA_SplineMorphModifier,
    /*type*/ ModifierTypeType::Constructive,
    /*flags*/ eModifierTypeFlag_AcceptsCurves,
    /*icon*/ ICON_MOD_SPLINE_MORPH,

    /*copy_data*/ BKE_modifier_copydata_generic,

    /*deform_verts*/ nullptr,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ nullptr,
    /*modify_geometry_set*/ modify_geometry_set,

    /*init_data*/ init_data_SplineMorphModifierData,
    /*required_data_mask*/ nullptr,
    /*free_data*/ nullptr,
    /*is_disabled*/ is_disabled,
    /*update_depsgraph*/ update_depsgraph,
    /*depends_on_time*/ nullptr,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ foreach_ID_link,
    /*foreach_tex_link*/ nullptr,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ panel_register_SplineMorph,
    /*blend_write*/ nullptr,
    /*blend_read*/ nullptr,
    /*foreach_cache*/ nullptr,
    /*foreach_working_space_color*/ nullptr,
};

}  // namespace blender
