/* SPDX-FileCopyrightText: 2026 Blender Authors
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "BKE_modifier.hh"
#include "BLI_utildefines.h"
#include "BLT_translation.hh"
#include "DNA_mesh_types.h"
#include "DNA_modifier_types.h"
#include "DNA_screen_types.h"
#include "GEO_mesh_divide.hh"
#include "MOD_ui_common.hh"
#include "MOD_util.hh"
#include "RNA_access.hh"
#include "RNA_prototypes.hh"
#include "UI_interface_layout.hh"
#include "UI_resources.hh"

namespace blender {
static void init_data(ModifierData *md)
{
  auto *data = reinterpret_cast<DivideModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}
static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  const auto &data = *reinterpret_cast<DivideModifierData *>(md);
  geometry::MeshDivideParams params;
  params.size = float3(data.grid_size);
  params.offset = float3(data.offset);
  params.rotation = float3(data.rotation);
  params.tolerance = data.tolerance;
  params.axes = {bool(data.flag & MOD_DIVIDE_AXIS_X),
                 bool(data.flag & MOD_DIVIDE_AXIS_Y),
                 bool(data.flag & MOD_DIVIDE_AXIS_Z)};
  std::string message;
  Mesh *result = geometry::mesh_divide(*mesh, params, message);
  if (!message.empty())
    BKE_modifier_set_warning(ctx->object, md, "%s", message.c_str());
  return result ? result : mesh;
}
static void panel_draw(const bContext * /*C*/, Panel *panel)
{
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, nullptr);
  ui::Layout &layout = *panel->layout;
  layout.use_property_split_set(true);
  ui::Layout &axes = layout.row(true, IFACE_("Grid Axes"));
  axes.prop(ptr, "use_axis_x", UI_ITEM_NONE, "X", ICON_NONE);
  axes.prop(ptr, "use_axis_y", UI_ITEM_NONE, "Y", ICON_NONE);
  axes.prop(ptr, "use_axis_z", UI_ITEM_NONE, "Z", ICON_NONE);
  layout.prop(ptr, "grid_size", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "offset", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "rotation", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  modifier_error_message_draw(layout, ptr);
}
static void advanced_panel_draw(const bContext * /*C*/, Panel *panel)
{
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, nullptr);
  ui::Layout &layout = *panel->layout;
  layout.use_property_split_set(true);
  layout.prop(ptr, "tolerance", UI_ITEM_NONE, std::nullopt, ICON_NONE);
}
static void panel_register(ARegionType *region_type)
{
  PanelType *parent = modifier_panel_register(region_type, eModifierType_Divide, panel_draw);
  modifier_subpanel_register(
      region_type, "advanced", "Advanced", nullptr, advanced_panel_draw, parent);
}

ModifierTypeInfo modifierType_Divide = {
    /*idname*/ "Divide",
    /*name*/ N_("Divide"),
    /*struct_name*/ "DivideModifierData",
    /*struct_size*/ sizeof(DivideModifierData),
    /*srna*/ &RNA_DivideModifier,
    /*type*/ ModifierTypeType::Nonconstructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode,
    /*icon*/ ICON_MOD_DIVIDE,

    /*copy_data*/ BKE_modifier_copydata_generic,

    /*deform_verts*/ nullptr,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ modify_mesh,
    /*modify_geometry_set*/ nullptr,

    /*init_data*/ init_data,
    /*required_data_mask*/ nullptr,
    /*free_data*/ nullptr,
    /*is_disabled*/ nullptr,
    /*update_depsgraph*/ nullptr,
    /*depends_on_time*/ nullptr,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ nullptr,
    /*foreach_tex_link*/ nullptr,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ panel_register,
    /*blend_write*/ nullptr,
    /*blend_read*/ nullptr,
    /*foreach_cache*/ nullptr,
    /*foreach_working_space_color*/ nullptr,
};

}  // namespace blender
