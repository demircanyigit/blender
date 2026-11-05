/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Element Order. The work is done by shared code in the geometry module, which the matching nodes use too.
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

namespace blender {

static void init_data_ElementOrderModifierData(ModifierData *md)
{
  ElementOrderModifierData *data = reinterpret_cast<ElementOrderModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext * /*ctx*/, Mesh *mesh)
{
  const ElementOrderModifierData &omd = *reinterpret_cast<ElementOrderModifierData *>(md);
  geometry::ElementOrderParams params;
  params.element = geometry::MeshElement(omd.element);
  params.uv_map = omd.uv_map;
  params.mode = geometry::ElementOrderMode(omd.mode);
  params.axis = omd.axis;
  params.point = float3(omd.point);
  params.reverse = omd.flag & MOD_ELEMENT_ORDER_REVERSE;
  params.seed = omd.seed;
  Mesh *result = geometry::element_order(*mesh, params);
  return result ? result : mesh;
}

static void panel_draw_ElementOrder(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);
  UNUSED_VARS(ob_ptr);

  layout.prop(ptr, "mode", ui::ITEM_R_EXPAND, std::nullopt, ICON_NONE);
  layout.use_property_split_set(true);
  layout.prop(ptr, "element", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  if (RNA_enum_get(ptr, "element") == 2) {
    layout.prop(ptr, "uv_map", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  if (RNA_enum_get(ptr, "mode") == 1) {
    layout.prop(ptr, "axis", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  if (RNA_enum_get(ptr, "mode") == 2) {
    layout.prop(ptr, "point", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  if (RNA_enum_get(ptr, "mode") == 0) {
    layout.prop(ptr, "seed", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  layout.prop(ptr, "use_reverse", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  modifier_error_message_draw(layout, ptr);
}

static void panel_register_ElementOrder(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_ElementOrder, panel_draw_ElementOrder);
}

ModifierTypeInfo modifierType_ElementOrder = {
    /*idname*/ "ElementOrder",
    /*name*/ N_("Element Order"),
    /*struct_name*/ "ElementOrderModifierData",
    /*struct_size*/ sizeof(ElementOrderModifierData),
    /*srna*/ &RNA_ElementOrderModifier,
    /*type*/ ModifierTypeType::Constructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode | eModifierTypeFlag_EnableInEditmode,
    /*icon*/ ICON_MOD_ELEMENT_ORDER,

    /*copy_data*/ BKE_modifier_copydata_generic,

    /*deform_verts*/ nullptr,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ modify_mesh,
    /*modify_geometry_set*/ nullptr,

    /*init_data*/ init_data_ElementOrderModifierData,
    /*required_data_mask*/ nullptr,
    /*free_data*/ nullptr,
    /*is_disabled*/ nullptr,
    /*update_depsgraph*/ nullptr,
    /*depends_on_time*/ nullptr,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ nullptr,
    /*foreach_tex_link*/ nullptr,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ panel_register_ElementOrder,
    /*blend_write*/ nullptr,
    /*blend_read*/ nullptr,
    /*foreach_cache*/ nullptr,
    /*foreach_working_space_color*/ nullptr,
};

}  // namespace blender
