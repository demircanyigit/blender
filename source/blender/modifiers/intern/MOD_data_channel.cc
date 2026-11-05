/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Data Channel. The work is done by shared code in the geometry module, which the matching nodes use too.
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
#include "GEO_mesh_channels.hh"

namespace blender {

static void init_data_DataChannelModifierData(ModifierData *md)
{
  DataChannelModifierData *data = reinterpret_cast<DataChannelModifierData *>(md);
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
  const DataChannelModifierData &dmd = *reinterpret_cast<DataChannelModifierData *>(md);
  if (mesh->verts_num == 0) {
    return mesh;
  }
  geometry::DataChannelParams params;
  params.source = geometry::DataChannelSource(dmd.source);
  params.seed = dmd.seed;
  params.normalize = dmd.flag & MOD_DATA_CHANNEL_NORMALIZE;
  params.invert = dmd.flag & MOD_DATA_CHANNEL_INVERT;
  params.clamp = dmd.flag & MOD_DATA_CHANNEL_CLAMP;
  params.power = dmd.power;
  params.multiply = dmd.multiply;
  params.add = dmd.add;
  Array<float> values(mesh->verts_num);
  geometry::data_channel(*mesh, params, values);

  switch (dmd.output) {
    case 0: {
      if (dmd.name[0] == '\0') {
        break;
      }
      bke::MutableAttributeAccessor attributes = mesh->attributes_for_write();
      attributes.remove(dmd.name);
      attributes.add<float>(
          dmd.name,
          bke::AttrDomain::Point,
          bke::AttributeInitVArray(VArray<float>::from_container(std::move(values))));
      break;
    }
    case 1: {
      if (dmd.name[0] == '\0') {
        break;
      }
      for (float &value : values) {
        value = std::clamp(value, 0.0f, 1.0f);
      }
      write_vertex_group(*mesh, dmd.name, values);
      break;
    }
    case 2: {
      const Array<float3> normals(mesh->vert_normals());
      MutableSpan<float3> positions = mesh->vert_positions_for_write();
      for (const int i : positions.index_range()) {
        positions[i] += normals[i] * values[i];
      }
      mesh->tag_positions_changed();
      break;
    }
  }
  return mesh;
}

static void panel_draw_DataChannel(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);
  UNUSED_VARS(ob_ptr);

  layout.use_property_split_set(true);
  layout.prop(ptr, "source", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  if (RNA_enum_get(ptr, "source") == 9) {
    layout.prop(ptr, "seed", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  layout.prop(ptr, "output", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  if (RNA_enum_get(ptr, "output") != 2) {
    layout.prop(ptr, "name", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  layout.prop(ptr, "use_normalize", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "invert", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "power", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "multiply", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "add", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "use_clamp", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  modifier_error_message_draw(layout, ptr);
}

static void panel_register_DataChannel(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_DataChannel, panel_draw_DataChannel);
}

ModifierTypeInfo modifierType_DataChannel = {
    /*idname*/ "DataChannel",
    /*name*/ N_("Data Channel"),
    /*struct_name*/ "DataChannelModifierData",
    /*struct_size*/ sizeof(DataChannelModifierData),
    /*srna*/ &RNA_DataChannelModifier,
    /*type*/ ModifierTypeType::Nonconstructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode | eModifierTypeFlag_EnableInEditmode,
    /*icon*/ ICON_MOD_DATA_CHANNEL,

    /*copy_data*/ BKE_modifier_copydata_generic,

    /*deform_verts*/ nullptr,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ modify_mesh,
    /*modify_geometry_set*/ nullptr,

    /*init_data*/ init_data_DataChannelModifierData,
    /*required_data_mask*/ nullptr,
    /*free_data*/ nullptr,
    /*is_disabled*/ nullptr,
    /*update_depsgraph*/ nullptr,
    /*depends_on_time*/ nullptr,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ nullptr,
    /*foreach_tex_link*/ nullptr,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ panel_register_DataChannel,
    /*blend_write*/ nullptr,
    /*blend_read*/ nullptr,
    /*foreach_cache*/ nullptr,
    /*foreach_working_space_color*/ nullptr,
};

}  // namespace blender
