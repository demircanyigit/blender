/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "DNA_mesh_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"

#include "BKE_customdata.hh"
#include "BKE_mesh.hh"
#include "BKE_modifier.hh"

#include "BLT_translation.hh"

#include "DEG_depsgraph_build.hh"
#include "DEG_depsgraph_query.hh"

#include "GEO_uv_scale.hh"

#include "RNA_access.hh"
#include "RNA_prototypes.hh"
#include "UI_interface_layout.hh"
#include "UI_resources.hh"

#include "MOD_modifiertypes.hh"
#include "MOD_ui_common.hh"

namespace blender {

static void init_data(ModifierData *md)
{
  UVScaleModifierData *data = reinterpret_cast<UVScaleModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

static void required_data_mask(ModifierData * /*md*/, CustomData_MeshMasks *mask)
{
  mask->lmask |= CD_MASK_PROP_FLOAT2;
}

static void update_depsgraph(ModifierData *md, const ModifierUpdateDepsgraphContext *ctx)
{
  const auto &data = *reinterpret_cast<const UVScaleModifierData *>(md);
  if (data.mode == MOD_UV_SCALE_DENSITY) {
    DEG_add_depends_on_transform_relation(ctx->node, "UV Texel Density");
    DEG_add_scene_relation(ctx->node, ctx->scene, DEG_SCENE_COMP_PARAMETERS, "UV Texel Density");
  }
}

static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  const auto &data = *reinterpret_cast<const UVScaleModifierData *>(md);
  geometry::UVScaleParams params;
  params.texel_density = data.mode == MOD_UV_SCALE_DENSITY;
  params.individual_islands = data.flag & MOD_UV_SCALE_ISLANDS;
  params.pivot = data.pivot;
  params.center = float2(data.center);
  params.scale = float2(data.scale);
  params.density = data.density;
  params.resolution = int2(data.resolution);
  if (params.texel_density) {
    params.object_to_world = ctx->object->object_to_world();
    const Scene *scene = DEG_get_evaluated_scene(ctx->depsgraph);
    params.meters_per_unit = scene->unit.scale_length;
  }
  if (const char *error = geometry::uv_scale(*mesh, data.uv_map, params)) {
    BKE_modifier_set_error(ctx->object, md, "%s", error);
  }
  return mesh;
}

static void panel_draw(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);
  PointerRNA mesh_ptr = RNA_pointer_get(&ob_ptr, "data");
  layout.use_property_split_set(true);
  layout.prop_search(ptr, "uv_map", &mesh_ptr, "uv_layers", std::nullopt, ICON_GROUP_UVS);
  layout.prop(ptr, "mode", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "use_individual_islands", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  if (RNA_enum_get(ptr, "mode") == MOD_UV_SCALE_DENSITY) {
    layout.prop(ptr, "density", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    PropertyRNA *resolution = RNA_struct_find_property(ptr, "resolution");
    layout.prop(ptr, resolution, 0, 0, UI_ITEM_NONE, IFACE_("Texture Width"), ICON_NONE);
    layout.prop(ptr, resolution, 1, 0, UI_ITEM_NONE, IFACE_("Texture Height"), ICON_NONE);
  }
  else {
    PropertyRNA *scale = RNA_struct_find_property(ptr, "scale");
    layout.prop(ptr, scale, 0, 0, UI_ITEM_NONE, IFACE_("Scale U"), ICON_NONE);
    layout.prop(ptr, scale, 1, 0, UI_ITEM_NONE, IFACE_("Scale V"), ICON_NONE);
  }
  layout.prop(ptr, "pivot", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  if (RNA_enum_get(ptr, "pivot") == MOD_UV_SCALE_PIVOT_CUSTOM) {
    layout.prop(ptr, "center", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  modifier_error_message_draw(layout, ptr);
}

static void panel_register(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_UVScale, panel_draw);
}

ModifierTypeInfo modifierType_UVScale = {
    /*idname*/ "UVScale",
    /*name*/ N_("UV Scale"),
    /*struct_name*/ "UVScaleModifierData",
    /*struct_size*/ sizeof(UVScaleModifierData),
    /*srna*/ &RNA_UVScaleModifier,
    /*type*/ ModifierTypeType::Nonconstructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode |
        eModifierTypeFlag_EnableInEditmode,
    /*icon*/ ICON_MOD_UV_SCALE,
    /*copy_data*/ BKE_modifier_copydata_generic,
    /*deform_verts*/ nullptr,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ modify_mesh,
    /*modify_geometry_set*/ nullptr,
    /*init_data*/ init_data,
    /*required_data_mask*/ required_data_mask,
    /*free_data*/ nullptr,
    /*is_disabled*/ nullptr,
    /*update_depsgraph*/ update_depsgraph,
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
