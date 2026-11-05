/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Symmetrize. The work is done by shared code in the geometry module, which the matching nodes use too.
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
#include "GEO_mesh_topology_ops.hh"
#include "GEO_randomize.hh"

namespace blender {

static void init_data_SymmetrizeModifierData(ModifierData *md)
{
  SymmetrizeModifierData *data = reinterpret_cast<SymmetrizeModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext * /*ctx*/, Mesh *mesh)
{
  const SymmetrizeModifierData &smd = *reinterpret_cast<SymmetrizeModifierData *>(md);
  Mesh *result = geometry::mesh_symmetrize(
      *mesh, geometry::SymmetrizeDirection(smd.direction), smd.merge_distance);
  if (result == nullptr) {
    return mesh;
  }
  geometry::debug_randomize_mesh_order(result);
  return result;
}

static void panel_draw_Symmetrize(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);
  UNUSED_VARS(ob_ptr);

  layout.use_property_split_set(true);
  layout.prop(ptr, "direction", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "merge_distance", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  modifier_error_message_draw(layout, ptr);
}

static void panel_register_Symmetrize(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_Symmetrize, panel_draw_Symmetrize);
}

ModifierTypeInfo modifierType_Symmetrize = {
    /*idname*/ "Symmetrize",
    /*name*/ N_("Symmetrize"),
    /*struct_name*/ "SymmetrizeModifierData",
    /*struct_size*/ sizeof(SymmetrizeModifierData),
    /*srna*/ &RNA_SymmetrizeModifier,
    /*type*/ ModifierTypeType::Constructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode | eModifierTypeFlag_EnableInEditmode,
    /*icon*/ ICON_MOD_SYMMETRIZE,

    /*copy_data*/ BKE_modifier_copydata_generic,

    /*deform_verts*/ nullptr,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ modify_mesh,
    /*modify_geometry_set*/ nullptr,

    /*init_data*/ init_data_SymmetrizeModifierData,
    /*required_data_mask*/ nullptr,
    /*free_data*/ nullptr,
    /*is_disabled*/ nullptr,
    /*update_depsgraph*/ nullptr,
    /*depends_on_time*/ nullptr,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ nullptr,
    /*foreach_tex_link*/ nullptr,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ panel_register_Symmetrize,
    /*blend_write*/ nullptr,
    /*blend_read*/ nullptr,
    /*foreach_cache*/ nullptr,
    /*foreach_working_space_color*/ nullptr,
};

}  // namespace blender
