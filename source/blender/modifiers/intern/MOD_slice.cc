/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Slice. The work is done by shared code in the geometry module, which the matching nodes use too.
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
#include "BLI_bounds.hh"
#include "BKE_customdata.hh"
#include "BKE_mesh.h"
#include "bmesh.hh"
#include "GEO_mesh_clip.hh"
#include "GEO_randomize.hh"

namespace blender {

static void init_data_SliceModifierData(ModifierData *md)
{
  SliceModifierData *data = reinterpret_cast<SliceModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext * /*ctx*/, Mesh *mesh)
{
  const SliceModifierData &smd = *reinterpret_cast<SliceModifierData *>(md);
  if (mesh->verts_num == 0) {
    return mesh;
  }
  const float3x3 rotation = math::from_rotation<float3x3>(math::EulerXYZ(float3(smd.rotation)));
  const float3 normal = math::normalize(rotation * float3(0.0f, 0.0f, 1.0f));

  geometry::BMeshClipParams params;
  params.plane_no = normal;
  params.plane_co = float3(smd.location) + normal * smd.distance;
  params.keep = geometry::BMeshClipKeep(smd.keep);
  params.fill = smd.flag & MOD_SLICE_FILL;
  if (const std::optional<Bounds<float3>> bounds = mesh->bounds_min_max()) {
    params.threshold = std::max(math::distance(bounds->min, bounds->max) * 1e-5f, 1e-7f);
  }

  BMeshCreateParams create_params{};
  create_params.use_toolflags = true;
  BMeshFromMeshParams convert_params{};
  convert_params.calc_face_normal = true;
  convert_params.calc_vert_normal = true;
  BMesh *bm = BKE_mesh_to_bmesh_ex(mesh, &create_params, &convert_params);
  geometry::bmesh_clip(bm, params);
  Mesh *result = BKE_mesh_from_bmesh_for_eval_nomain(bm, nullptr, mesh);
  BM_mesh_free(bm);
  geometry::debug_randomize_mesh_order(result);
  return result;
}

static void panel_draw_Slice(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);
  UNUSED_VARS(ob_ptr);

  layout.prop(ptr, "keep", ui::ITEM_R_EXPAND, std::nullopt, ICON_NONE);
  layout.use_property_split_set(true);
  layout.prop(ptr, "plane_location", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "plane_rotation", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "distance", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  if (RNA_enum_get(ptr, "keep") != 2) {
    layout.prop(ptr, "use_fill", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  modifier_error_message_draw(layout, ptr);
}

static void panel_register_Slice(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_Slice, panel_draw_Slice);
}

ModifierTypeInfo modifierType_Slice = {
    /*idname*/ "Slice",
    /*name*/ N_("Slice"),
    /*struct_name*/ "SliceModifierData",
    /*struct_size*/ sizeof(SliceModifierData),
    /*srna*/ &RNA_SliceModifier,
    /*type*/ ModifierTypeType::Constructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode | eModifierTypeFlag_EnableInEditmode,
    /*icon*/ ICON_MOD_SLICE,

    /*copy_data*/ BKE_modifier_copydata_generic,

    /*deform_verts*/ nullptr,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ modify_mesh,
    /*modify_geometry_set*/ nullptr,

    /*init_data*/ init_data_SliceModifierData,
    /*required_data_mask*/ nullptr,
    /*free_data*/ nullptr,
    /*is_disabled*/ nullptr,
    /*update_depsgraph*/ nullptr,
    /*depends_on_time*/ nullptr,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ nullptr,
    /*foreach_tex_link*/ nullptr,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ panel_register_Slice,
    /*blend_write*/ nullptr,
    /*blend_read*/ nullptr,
    /*foreach_cache*/ nullptr,
    /*foreach_working_space_color*/ nullptr,
};

}  // namespace blender
