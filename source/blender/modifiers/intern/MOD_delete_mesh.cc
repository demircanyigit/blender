/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Delete Mesh. The work is done by shared code in the geometry module, which the matching nodes use too.
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
#include "BLI_index_mask.hh"
#include "GEO_mesh_copy_selection.hh"
#include "GEO_randomize.hh"

namespace blender {

static void init_data_DeleteMeshModifierData(ModifierData *md)
{
  DeleteMeshModifierData *data = reinterpret_cast<DeleteMeshModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  const DeleteMeshModifierData &dmd = *reinterpret_cast<DeleteMeshModifierData *>(md);
  if (mesh->verts_num == 0 || dmd.selection[0] == '\0') {
    return mesh;
  }
  const bke::AttrDomain domain = dmd.domain == 0 ? bke::AttrDomain::Point :
                                 dmd.domain == 1 ? bke::AttrDomain::Edge :
                                                   bke::AttrDomain::Face;
  const VArray<float> values = *mesh->attributes().lookup<float>(dmd.selection, domain);
  if (!values) {
    BKE_modifier_set_warning(ctx->object, md, "No vertex group or attribute \"%s\"", dmd.selection);
    return mesh;
  }
  const bool invert = dmd.flag & MOD_DELETE_MESH_INVERT;
  const float threshold = dmd.threshold;
  /* The elements to keep. */
  Array<bool> keep(values.size());
  for (const int i : keep.index_range()) {
    keep[i] = (values[i] > threshold) == invert;
  }
  const VArray<bool> keep_varray = VArray<bool>::from_span(keep);
  std::optional<Mesh *> result;
  if (dmd.domain == 3) {
    result = geometry::mesh_copy_selection_keep_edges(
        *mesh, keep_varray, domain, bke::AttributeFilter::default_filter());
  }
  else {
    result = geometry::mesh_copy_selection(
        *mesh, keep_varray, domain, bke::AttributeFilter::default_filter());
  }
  if (!result) {
    return mesh;
  }
  if (*result == nullptr) {
    Mesh *empty = BKE_mesh_new_nomain(0, 0, 0, 0);
    BKE_mesh_copy_parameters_for_eval(empty, mesh);
    return empty;
  }
  geometry::debug_randomize_mesh_order(*result);
  return *result;
}

static void panel_draw_DeleteMesh(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);
  UNUSED_VARS(ob_ptr);

  layout.prop(ptr, "domain", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.use_property_split_set(true);
  layout.prop(ptr, "selection", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "threshold", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "invert", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  modifier_error_message_draw(layout, ptr);
}

static void panel_register_DeleteMesh(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_DeleteMesh, panel_draw_DeleteMesh);
}

ModifierTypeInfo modifierType_DeleteMesh = {
    /*idname*/ "DeleteMesh",
    /*name*/ N_("Delete Mesh"),
    /*struct_name*/ "DeleteMeshModifierData",
    /*struct_size*/ sizeof(DeleteMeshModifierData),
    /*srna*/ &RNA_DeleteMeshModifier,
    /*type*/ ModifierTypeType::Constructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode | eModifierTypeFlag_EnableInEditmode,
    /*icon*/ ICON_MOD_DELETE_MESH,

    /*copy_data*/ BKE_modifier_copydata_generic,

    /*deform_verts*/ nullptr,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ modify_mesh,
    /*modify_geometry_set*/ nullptr,

    /*init_data*/ init_data_DeleteMeshModifierData,
    /*required_data_mask*/ nullptr,
    /*free_data*/ nullptr,
    /*is_disabled*/ nullptr,
    /*update_depsgraph*/ nullptr,
    /*depends_on_time*/ nullptr,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ nullptr,
    /*foreach_tex_link*/ nullptr,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ panel_register_DeleteMesh,
    /*blend_write*/ nullptr,
    /*blend_read*/ nullptr,
    /*foreach_cache*/ nullptr,
    /*foreach_working_space_color*/ nullptr,
};

}  // namespace blender
