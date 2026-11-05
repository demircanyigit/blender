/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Store Element Pivot. The work is done by shared code in the geometry module, which the matching nodes use too.
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

static void init_data_ElementPivotModifierData(ModifierData *md)
{
  ElementPivotModifierData *data = reinterpret_cast<ElementPivotModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext * /*ctx*/, Mesh *mesh)
{
  const ElementPivotModifierData &emd = *reinterpret_cast<ElementPivotModifierData *>(md);
  geometry::ElementPivotParams params;
  params.element = geometry::MeshElement(emd.element);
  params.uv_map = emd.uv_map;
  params.pivot = geometry::ElementPivot(emd.pivot);
  params.center_name = emd.center_name;
  params.x_axis_name = emd.x_axis_name;
  params.z_axis_name = emd.z_axis_name;
  params.index_name = emd.index_name;
  geometry::store_element_pivot(*mesh, params);
  return mesh;
}

static void panel_draw_ElementPivot(const bContext * /*C*/, Panel *panel)
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
  layout.prop(ptr, "pivot", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  {
    ui::Layout &col = layout.column(true, IFACE_("Attributes"));
    col.prop(ptr, "center_name", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    col.prop(ptr, "x_axis_name", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    col.prop(ptr, "z_axis_name", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    col.prop(ptr, "index_name", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  modifier_error_message_draw(layout, ptr);
}

static void panel_register_ElementPivot(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_ElementPivot, panel_draw_ElementPivot);
}

ModifierTypeInfo modifierType_ElementPivot = {
    /*idname*/ "ElementPivot",
    /*name*/ N_("Store Element Pivot"),
    /*struct_name*/ "ElementPivotModifierData",
    /*struct_size*/ sizeof(ElementPivotModifierData),
    /*srna*/ &RNA_ElementPivotModifier,
    /*type*/ ModifierTypeType::Nonconstructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode | eModifierTypeFlag_EnableInEditmode,
    /*icon*/ ICON_MOD_ELEMENT_PIVOT,

    /*copy_data*/ BKE_modifier_copydata_generic,

    /*deform_verts*/ nullptr,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ modify_mesh,
    /*modify_geometry_set*/ nullptr,

    /*init_data*/ init_data_ElementPivotModifierData,
    /*required_data_mask*/ nullptr,
    /*free_data*/ nullptr,
    /*is_disabled*/ nullptr,
    /*update_depsgraph*/ nullptr,
    /*depends_on_time*/ nullptr,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ nullptr,
    /*foreach_tex_link*/ nullptr,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ panel_register_ElementPivot,
    /*blend_write*/ nullptr,
    /*blend_read*/ nullptr,
    /*foreach_cache*/ nullptr,
    /*foreach_working_space_color*/ nullptr,
};

}  // namespace blender
