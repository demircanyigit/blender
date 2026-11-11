/* SPDX-FileCopyrightText: 2005 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 */

#include "BLI_string.h"
#include "BLI_utildefines.h"

#include "BLT_translation.hh"

#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_object_types.h"
#include "DNA_screen_types.h"

#include "MEM_guardedalloc.h"

#include "BKE_deform.hh"
#include "BKE_mesh.hh"

#include "UI_interface_layout.hh"
#include "UI_resources.hh"

#include "RNA_access.hh"
#include "RNA_prototypes.hh"

#include "DEG_depsgraph_query.hh"

#include "BLI_array.hh"
#include "GEO_decimate_backends.hh"
#include "GEO_randomize.hh"

#include "bmesh.hh"
#include "bmesh_tools.hh"

// #define USE_TIMEIT

#ifdef USE_TIMEIT
#  include "BLI_time.h"
#  include "BLI_time_utildefines.h"
#endif

#include "MOD_ui_common.hh"
#include "MOD_util.hh"

namespace blender {

static void init_data(ModifierData *md)
{
  DecimateModifierData *dmd = reinterpret_cast<DecimateModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(dmd, modifier);
}

static void required_data_mask(ModifierData *md, CustomData_MeshMasks *r_cddata_masks)
{
  DecimateModifierData *dmd = reinterpret_cast<DecimateModifierData *>(md);

  if (dmd->backend != MOD_DECIMATE_BACKEND_BLENDER) {
    r_cddata_masks->lmask |= CD_MASK_PROP_FLOAT2;
  }
  /* Ask for vertex-groups if we need them. */
  if (dmd->defgrp_name[0] != '\0' &&
      (dmd->defgrp_factor > 0.0f || dmd->backend != MOD_DECIMATE_BACKEND_BLENDER))
  {
    r_cddata_masks->vmask |= CD_MASK_MDEFORMVERT;
  }
}

static DecimateModifierData *getOriginalModifierData(const DecimateModifierData *dmd,
                                                     const ModifierEvalContext *ctx)
{
  Object *ob_orig = DEG_get_original(ctx->object);
  return reinterpret_cast<DecimateModifierData *>(
      BKE_modifiers_findby_name(ob_orig, dmd->modifier.name));
}

static void updateFaceCount(const ModifierEvalContext *ctx,
                            DecimateModifierData *dmd,
                            int face_count)
{
  dmd->face_count = face_count;

  if (DEG_is_active(ctx->depsgraph)) {
    /* update for display only */
    DecimateModifierData *dmd_orig = getOriginalModifierData(dmd, ctx);
    dmd_orig->face_count = face_count;
  }
}

static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext *ctx, Mesh *meshData)
{
  DecimateModifierData *dmd = reinterpret_cast<DecimateModifierData *>(md);
  Mesh *mesh = meshData, *result = nullptr;
  BMesh *bm;
  bool calc_vert_normal;
  bool calc_face_normal;
  float *vweights = nullptr;

#ifdef USE_TIMEIT
  TIMEIT_START(decim);
#endif

  /* Set up front so we don't show invalid info in the UI. */
  updateFaceCount(ctx, dmd, mesh->faces_num);

  const float collapse_ratio = dmd->target_mode == 1 ?
                                   std::min(1.0f,
                                            float(dmd->target_face_count) /
                                                std::max(1, int(mesh->corner_tris().size()))) :
                                   dmd->percent;
  const bool alternative = dmd->mode == MOD_DECIM_MODE_COLLAPSE &&
                           dmd->backend != MOD_DECIMATE_BACKEND_BLENDER;

  switch (dmd->mode) {
    case MOD_DECIM_MODE_COLLAPSE:
      if (collapse_ratio >= 1.0f) {
        return mesh;
      }
      calc_face_normal = true;
      calc_vert_normal = true;
      break;
    case MOD_DECIM_MODE_UNSUBDIV:
      if (dmd->iter == 0) {
        return mesh;
      }
      calc_face_normal = false;
      calc_vert_normal = false;
      break;
    case MOD_DECIM_MODE_DISSOLVE:
      if (dmd->angle == 0.0f) {
        return mesh;
      }
      calc_face_normal = true;
      calc_vert_normal = false;
      break;
    default:
      return mesh;
  }

  if (!alternative && dmd->face_count <= 3) {
    BKE_modifier_set_error(ctx->object, md, "Modifier requires more than 3 input faces");
    return mesh;
  }

  if (dmd->mode == MOD_DECIM_MODE_COLLAPSE) {
    if (dmd->defgrp_name[0] &&
        (dmd->defgrp_factor > 0.0f || dmd->backend != MOD_DECIMATE_BACKEND_BLENDER))
    {
      const MDeformVert *dvert;
      int defgrp_index;

      MOD_get_vgroup(ctx->object, mesh, dmd->defgrp_name, &dvert, &defgrp_index);

      if (dvert) {
        const uint vert_tot = mesh->verts_num;
        uint i;

        vweights = MEM_new_array_uninitialized<float>(vert_tot, __func__);

        if (dmd->flag & MOD_DECIM_FLAG_INVERT_VGROUP) {
          for (i = 0; i < vert_tot; i++) {
            vweights[i] = 1.0f - BKE_defvert_find_weight(&dvert[i], defgrp_index);
          }
        }
        else {
          for (i = 0; i < vert_tot; i++) {
            vweights[i] = BKE_defvert_find_weight(&dvert[i], defgrp_index);
          }
        }
      }
    }
  }

  if (alternative) {
    geometry::DecimateBackendParams params;
    params.backend = geometry::DecimateBackend(dmd->backend);
    params.target_faces = dmd->target_mode == 1 ? dmd->target_face_count :
                                                  int(mesh->corner_tris().size() * collapse_ratio);
    params.error_limit = dmd->error_limit;
    params.feature_angle = dmd->feature_angle;
    params.uv_weight = dmd->uv_weight;
    params.normal_weight = dmd->normal_weight;
    params.virtual_distance = dmd->virtual_distance;
    params.max_normal_change = dmd->max_normal_change;
    params.max_aspect_ratio = dmd->max_aspect_ratio;
    params.uv_map = dmd->uv_map;
    params.preserve_boundary = dmd->backend_flags & MOD_DECIMATE_KEEP_BOUNDARY;
    params.preserve_uv = dmd->backend_flags & MOD_DECIMATE_KEEP_UV;
    params.preserve_materials = dmd->backend_flags & MOD_DECIMATE_KEEP_MATERIALS;
    params.preserve_sharp = dmd->backend_flags & MOD_DECIMATE_KEEP_SHARP;
    params.merge_coincident = dmd->backend_flags & MOD_DECIMATE_MERGE_COINCIDENT;
    Array<bool> selected;
    if (vweights) {
      selected.reinitialize(mesh->verts_num);
      for (const int i : selected.index_range()) {
        selected[i] = vweights[i] > 0.0f;
      }
      params.selection = VArray<bool>::from_span(selected);
      MEM_delete(vweights);
    }
    std::string message;
    result = geometry::decimate_with_backend(*mesh, params, message);
    if (!message.empty()) {
      BKE_modifier_set_error(ctx->object, md, "%s", message.c_str());
    }
    updateFaceCount(ctx, dmd, result ? result->faces_num : mesh->faces_num);
    return result ? result : mesh;
  }

  BMeshCreateParams create_params{};
  BMeshFromMeshParams convert_params{};
  convert_params.calc_face_normal = calc_face_normal;
  convert_params.calc_vert_normal = calc_vert_normal;
  convert_params.cd_mask_extra.vmask = CD_MASK_ORIGINDEX;
  convert_params.cd_mask_extra.emask = CD_MASK_ORIGINDEX;
  convert_params.cd_mask_extra.pmask = CD_MASK_ORIGINDEX;

  bm = BKE_mesh_to_bmesh_ex(mesh, &create_params, &convert_params);

  switch (dmd->mode) {
    case MOD_DECIM_MODE_COLLAPSE: {
      const bool do_triangulate = (dmd->flag & MOD_DECIM_FLAG_TRIANGULATE) != 0;
      const int symmetry_axis = (dmd->flag & MOD_DECIM_FLAG_SYMMETRY) ? dmd->symmetry_axis : -1;
      const float symmetry_eps = 0.00002f;
      BM_mesh_decimate_collapse(bm,
                                collapse_ratio,
                                vweights,
                                dmd->defgrp_factor,
                                do_triangulate,
                                symmetry_axis,
                                symmetry_eps);
      break;
    }
    case MOD_DECIM_MODE_UNSUBDIV: {
      BM_mesh_decimate_unsubdivide(bm, dmd->iter);
      break;
    }
    case MOD_DECIM_MODE_DISSOLVE: {
      const bool do_dissolve_boundaries = (dmd->flag & MOD_DECIM_FLAG_ALL_BOUNDARY_VERTS) != 0;
      BM_mesh_decimate_dissolve(bm, dmd->angle, do_dissolve_boundaries, BMO_Delimit(dmd->delimit));
      break;
    }
  }

  if (vweights) {
    MEM_delete(vweights);
  }

  updateFaceCount(ctx, dmd, bm->totface);

  /* Make sure we never allocated these. */
  BLI_assert(bm->vtoolflagpool == nullptr && bm->etoolflagpool == nullptr &&
             bm->ftoolflagpool == nullptr);

  result = BKE_mesh_from_bmesh_for_eval_nomain(bm, nullptr, mesh);

  BM_mesh_free(bm);

  geometry::debug_randomize_mesh_order(result);

#ifdef USE_TIMEIT
  TIMEIT_END(decim);
#endif

  return result;
}

static void panel_draw(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;

  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);

  int decimate_type = RNA_enum_get(ptr, "decimate_type");
  char count_info[64];
  char face_count_str[BLI_STR_FORMAT_INT32_GROUPED_SIZE];
  BLI_str_format_int_grouped(face_count_str, RNA_int_get(ptr, "face_count"));
  SNPRINTF(count_info, RPT_("Face Count: %s"), face_count_str);

  layout.prop(ptr, "decimate_type", ui::ITEM_R_EXPAND, std::nullopt, ICON_NONE);

  layout.use_property_split_set(true);

  if (decimate_type == MOD_DECIM_MODE_COLLAPSE) {
    layout.prop(ptr, "backend", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    layout.prop(ptr, "target_mode", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    if (RNA_enum_get(ptr, "target_mode") == 0) {
      layout.prop(ptr, "ratio", ui::ITEM_R_SLIDER, std::nullopt, ICON_NONE);
    }
    else {
      layout.prop(ptr, "target_face_count", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    }
    const int backend = RNA_enum_get(ptr, "backend");
    if (backend != MOD_DECIMATE_BACKEND_BLENDER) {
      for (const char *property : {"use_preserve_boundary",
                                   "use_preserve_uv",
                                   "use_preserve_materials",
                                   "use_preserve_sharp",
                                   "use_merge_coincident",
                                   "feature_angle",
                                   "uv_map"})
      {
        layout.prop(ptr, property, UI_ITEM_NONE, std::nullopt, ICON_NONE);
      }
      if (backend != MOD_DECIMATE_BACKEND_CGAL) {
        layout.prop(ptr, "error_limit", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      }
      if (backend == MOD_DECIMATE_BACKEND_MESHOPT) {
        layout.prop(ptr, "uv_weight", UI_ITEM_NONE, std::nullopt, ICON_NONE);
        layout.prop(ptr, "normal_weight", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      }
      if (backend == MOD_DECIMATE_BACKEND_PMP) {
        layout.prop(ptr, "max_aspect_ratio", UI_ITEM_NONE, std::nullopt, ICON_NONE);
        layout.prop(ptr, "max_normal_change", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      }
      if (backend == MOD_DECIMATE_BACKEND_WILD) {
        layout.prop(ptr, "virtual_distance", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      }
      modifier_vgroup_ui(
          layout, ptr, &ob_ptr, "vertex_group", "invert_vertex_group", std::nullopt);
      layout.label(IFACE_("Output uses triangles"), ICON_NONE);
    }
    else {

      ui::Layout &row = layout.row(true, IFACE_("Symmetry"));
      row.use_property_decorate_set(false);
      ui::Layout *sub = &row.row(true);
      sub->prop(ptr, "use_symmetry", UI_ITEM_NONE, "", ICON_NONE);
      sub = &sub->row(true);
      sub->active_set(RNA_boolean_get(ptr, "use_symmetry"));
      sub->prop(ptr, "symmetry_axis", ui::ITEM_R_EXPAND, std::nullopt, ICON_NONE);
      row.decorator(ptr, "symmetry_axis", 0);

      layout.prop(ptr, "use_collapse_triangulate", UI_ITEM_NONE, std::nullopt, ICON_NONE);

      modifier_vgroup_ui(
          layout, ptr, &ob_ptr, "vertex_group", "invert_vertex_group", std::nullopt);
      sub = &layout.row(true);
      bool has_vertex_group = RNA_string_length(ptr, "vertex_group") != 0;
      sub->active_set(has_vertex_group);
      sub->prop(ptr, "vertex_group_factor", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    }
  }
  else if (decimate_type == MOD_DECIM_MODE_UNSUBDIV) {
    layout.prop(ptr, "iterations", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  else { /* decimate_type == MOD_DECIM_MODE_DISSOLVE. */
    layout.prop(ptr, "angle_limit", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    ui::Layout &col = layout.column(false);
    col.prop(ptr, "delimit", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    layout.prop(ptr, "use_dissolve_boundaries", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  layout.label(count_info, ICON_NONE);

  modifier_error_message_draw(layout, ptr);
}

static void panel_register(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_Decimate, panel_draw);
}

ModifierTypeInfo modifierType_Decimate = {
    /*idname*/ "Decimate",
    /*name*/ N_("Decimate"),
    /*struct_name*/ "DecimateModifierData",
    /*struct_size*/ sizeof(DecimateModifierData),
    /*srna*/ &RNA_DecimateModifier,
    /*type*/ ModifierTypeType::Nonconstructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_AcceptsCVs,
    /*icon*/ ICON_MOD_DECIM,

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
