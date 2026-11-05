/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Volume Select. The work is done by shared code in the geometry module, which the matching nodes use too.
 */

#include <algorithm>

#include "MEM_guardedalloc.h"

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
#include "BLI_listbase.h"
#include "GEO_volume_select.hh"

namespace blender {

static void init_data_VolumeSelectModifierData(ModifierData *md)
{
  VolumeSelectModifierData *data = reinterpret_cast<VolumeSelectModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

/** Write \a weights to a vertex group of \a mesh, adding the group when needed. */
static void write_vertex_group(Mesh &mesh, const char *name, const Span<float> weights)
{
  int index = BKE_id_defgroup_name_index(&mesh.id, name);
  if (index == -1) {
    bDeformGroup *group = MEM_new<bDeformGroup>(__func__);
    STRNCPY(group->name, name);
    BLI_addtail(&mesh.vertex_group_names, group);
    index = BLI_listbase_count(&mesh.vertex_group_names) - 1;
  }
  MutableSpan<MDeformVert> dverts = mesh.deform_verts_for_write();
  for (const int i : weights.index_range()) {
    if (weights[i] > 0.0f) {
      BKE_defvert_ensure_index(&dverts[i], index)->weight = std::min(weights[i], 1.0f);
    }
    else if (MDeformWeight *dw = BKE_defvert_find_index(&dverts[i], index)) {
      BKE_defvert_remove_group(&dverts[i], dw);
    }
  }
}

static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext * /*ctx*/, Mesh *mesh)
{
  const VolumeSelectModifierData &vmd = *reinterpret_cast<VolumeSelectModifierData *>(md);
  if (mesh->verts_num == 0 || vmd.vertex_group[0] == '\0') {
    return mesh;
  }
  geometry::VolumeSelectParams params;
  params.shape = geometry::VolumeSelectShape(vmd.shape);
  params.center = float3(vmd.center);
  params.rotation = float3(vmd.rotation);
  params.size = float3(vmd.size);
  params.falloff = vmd.falloff;
  params.invert = vmd.flag & MOD_VOLUME_SELECT_INVERT;
  Array<float> weights(mesh->verts_num);
  geometry::volume_select(mesh->vert_positions(), params, weights);
  write_vertex_group(*mesh, vmd.vertex_group, weights);
  return mesh;
}

static void panel_draw_VolumeSelect(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);
  UNUSED_VARS(ob_ptr);

  layout.prop(ptr, "shape", ui::ITEM_R_EXPAND, std::nullopt, ICON_NONE);
  layout.use_property_split_set(true);
  layout.prop(ptr, "vertex_group", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "center", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "rotation", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "size", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "falloff", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "invert", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  modifier_error_message_draw(layout, ptr);
}

static void panel_register_VolumeSelect(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_VolumeSelect, panel_draw_VolumeSelect);
}

ModifierTypeInfo modifierType_VolumeSelect = {
    /*idname*/ "VolumeSelect",
    /*name*/ N_("Volume Select"),
    /*struct_name*/ "VolumeSelectModifierData",
    /*struct_size*/ sizeof(VolumeSelectModifierData),
    /*srna*/ &RNA_VolumeSelectModifier,
    /*type*/ ModifierTypeType::Nonconstructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode | eModifierTypeFlag_EnableInEditmode,
    /*icon*/ ICON_MOD_VOL_SELECT,

    /*copy_data*/ BKE_modifier_copydata_generic,

    /*deform_verts*/ nullptr,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ modify_mesh,
    /*modify_geometry_set*/ nullptr,

    /*init_data*/ init_data_VolumeSelectModifierData,
    /*required_data_mask*/ nullptr,
    /*free_data*/ nullptr,
    /*is_disabled*/ nullptr,
    /*update_depsgraph*/ nullptr,
    /*depends_on_time*/ nullptr,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ nullptr,
    /*foreach_tex_link*/ nullptr,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ panel_register_VolumeSelect,
    /*blend_write*/ nullptr,
    /*blend_read*/ nullptr,
    /*foreach_cache*/ nullptr,
    /*foreach_working_space_color*/ nullptr,
};

}  // namespace blender
