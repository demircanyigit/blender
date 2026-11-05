/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * UVW Map. The work is done by shared code in the geometry module, which the matching nodes use too.
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
#include "GEO_uvw_map.hh"

namespace blender {

static void init_data_UVWMapModifierData(ModifierData *md)
{
  UVWMapModifierData *data = reinterpret_cast<UVWMapModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext * /*ctx*/, Mesh *mesh)
{
  const UVWMapModifierData &umd = *reinterpret_cast<UVWMapModifierData *>(md);
  if (mesh->faces_num == 0) {
    return mesh;
  }
  geometry::UVWMapParams params;
  params.type = geometry::UVWMapType(umd.map_type);
  params.center = float3(umd.center);
  params.rotation = float3(umd.rotation);
  params.size = float3(umd.size);
  params.tile = float2(umd.tile);
  params.offset = float2(umd.offset);
  params.cap = umd.flag & MOD_UVW_MAP_CAP;
  params.flip_u = umd.flag & MOD_UVW_MAP_FLIP_U;
  params.flip_v = umd.flag & MOD_UVW_MAP_FLIP_V;

  const StringRefNull name = umd.uv_map[0] ? StringRefNull(umd.uv_map) : StringRefNull("UVMap");
  bke::MutableAttributeAccessor attributes = mesh->attributes_for_write();
  if (const std::optional<bke::AttributeMetaData> meta = attributes.lookup_meta_data(name)) {
    if (meta->domain != bke::AttrDomain::Corner || meta->data_type != bke::AttrType::Float2) {
      attributes.remove(name);
    }
  }
  bke::SpanAttributeWriter<float2> uvs = attributes.lookup_or_add_for_write_only_span<float2>(
      name, bke::AttrDomain::Corner);
  if (!uvs) {
    return mesh;
  }
  geometry::uvw_map(mesh->vert_positions(), mesh->faces(), mesh->corner_verts(), params, uvs.span);
  uvs.finish();
  if (mesh->active_uv_map_name().is_empty()) {
    mesh->uv_maps_active_set(name);
  }
  return mesh;
}

static void panel_draw_UVWMap(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);
  UNUSED_VARS(ob_ptr);

  layout.prop(ptr, "map_type", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.use_property_split_set(true);
  layout.prop(ptr, "uv_map", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "center", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "rotation", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "size", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "tile", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "offset", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  if (RNA_enum_get(ptr, "map_type") == 1) {
    layout.prop(ptr, "use_cap", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  {
    ui::Layout &row = layout.row(true);
    row.prop(ptr, "flip_u", ui::ITEM_R_TOGGLE, std::nullopt, ICON_NONE);
    row.prop(ptr, "flip_v", ui::ITEM_R_TOGGLE, std::nullopt, ICON_NONE);
  }
  modifier_error_message_draw(layout, ptr);
}

static void panel_register_UVWMap(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_UVWMap, panel_draw_UVWMap);
}

ModifierTypeInfo modifierType_UVWMap = {
    /*idname*/ "UVWMap",
    /*name*/ N_("UVW Map"),
    /*struct_name*/ "UVWMapModifierData",
    /*struct_size*/ sizeof(UVWMapModifierData),
    /*srna*/ &RNA_UVWMapModifier,
    /*type*/ ModifierTypeType::Nonconstructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode | eModifierTypeFlag_EnableInEditmode,
    /*icon*/ ICON_MOD_UVW_MAP,

    /*copy_data*/ BKE_modifier_copydata_generic,

    /*deform_verts*/ nullptr,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ modify_mesh,
    /*modify_geometry_set*/ nullptr,

    /*init_data*/ init_data_UVWMapModifierData,
    /*required_data_mask*/ nullptr,
    /*free_data*/ nullptr,
    /*is_disabled*/ nullptr,
    /*update_depsgraph*/ nullptr,
    /*depends_on_time*/ nullptr,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ nullptr,
    /*foreach_tex_link*/ nullptr,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ panel_register_UVWMap,
    /*blend_write*/ nullptr,
    /*blend_read*/ nullptr,
    /*foreach_cache*/ nullptr,
    /*foreach_working_space_color*/ nullptr,
};

}  // namespace blender
