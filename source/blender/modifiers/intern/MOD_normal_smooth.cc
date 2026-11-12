/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Normal Smooth: smooths the shading normals over a few steps, without moving the mesh.
 */

#include <algorithm>

#include "BLI_array.hh"
#include "BLI_math_vector.hh"
#include "BLI_task.hh"

#include "BLT_translation.hh"

#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_types.h"
#include "DNA_screen_types.h"

#include "BKE_attribute.hh"
#include "BKE_deform.hh"
#include "BKE_mesh.hh"
#include "BKE_modifier.hh"

#include "UI_interface_layout.hh"
#include "UI_resources.hh"

#include "RNA_access.hh"
#include "RNA_prototypes.hh"

#include "MOD_modifiertypes.hh"
#include "MOD_ui_common.hh"
#include "MOD_util.hh"

namespace blender {

static void init_data(ModifierData *md)
{
  NormalSmoothModifierData *data = reinterpret_cast<NormalSmoothModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

static void required_data_mask(ModifierData *md, CustomData_MeshMasks *r_cddata_masks)
{
  const NormalSmoothModifierData &nmd = *reinterpret_cast<NormalSmoothModifierData *>(md);
  if (nmd.vertex_group[0] != '\0') {
    r_cddata_masks->vmask |= CD_MASK_MDEFORMVERT;
  }
}

static bool is_disabled(const Scene * /*scene*/, ModifierData *md, bool /*use_render_params*/)
{
  const NormalSmoothModifierData &nmd = *reinterpret_cast<NormalSmoothModifierData *>(md);
  return nmd.iterations <= 0 || nmd.factor <= 0.0f;
}

static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  const NormalSmoothModifierData &nmd = *reinterpret_cast<const NormalSmoothModifierData *>(md);
  if (mesh->corners_num == 0 || nmd.iterations <= 0 || nmd.factor <= 0.0f) {
    return mesh;
  }
  const int verts_num = mesh->verts_num;
  const Span<int2> edges = mesh->edges();
  const Span<int> corner_verts = mesh->corner_verts();
  const Span<int> corner_edges = mesh->corner_edges();
  const OffsetIndices faces = mesh->faces();
  const Span<float3> corner_normals = mesh->corner_normals();

  /* How much each point changes. */
  Array<float> weights(verts_num, 1.0f);
  const MDeformVert *dvert = nullptr;
  int defgrp_index = -1;
  MOD_get_vgroup(ctx->object, mesh, nmd.vertex_group, &dvert, &defgrp_index);
  if (dvert != nullptr && defgrp_index != -1) {
    const bool invert = nmd.flag & MOD_NORMAL_SMOOTH_INVERT_VGROUP;
    for (const int i : IndexRange(verts_num)) {
      const float w = BKE_defvert_find_weight(&dvert[i], defgrp_index);
      weights[i] = invert ? 1.0f - w : w;
    }
  }

  /* Points at sharp edges and flat faces keep their normals, and don't pass them on. */
  Array<bool> fixed(verts_num, false);
  if (nmd.flag & MOD_NORMAL_SMOOTH_KEEP_SHARP) {
    const bke::AttributeAccessor attributes = mesh->attributes();
    const VArraySpan<bool> sharp_edges = *attributes.lookup<bool>("sharp_edge",
                                                                  bke::AttrDomain::Edge);
    const VArraySpan<bool> sharp_faces = *attributes.lookup<bool>("sharp_face",
                                                                  bke::AttrDomain::Face);
    if (!sharp_edges.is_empty()) {
      for (const int e : edges.index_range()) {
        if (sharp_edges[e]) {
          fixed[edges[e][0]] = true;
          fixed[edges[e][1]] = true;
        }
      }
    }
    if (!sharp_faces.is_empty()) {
      for (const int f : faces.index_range()) {
        if (sharp_faces[f]) {
          for (const int corner : faces[f]) {
            fixed[corner_verts[corner]] = true;
          }
        }
      }
    }
    /* Open edges and edges with more than two faces don't blend well either. */
    Array<int> edge_faces(edges.size(), 0);
    for (const int e : corner_edges) {
      edge_faces[e]++;
    }
    for (const int e : edges.index_range()) {
      if (edge_faces[e] > 2) {
        fixed[edges[e][0]] = true;
        fixed[edges[e][1]] = true;
      }
    }
  }

  /* Start from the average of each point's corners. */
  Array<float3> normals(verts_num, float3(0.0f));
  for (const int corner : corner_verts.index_range()) {
    normals[corner_verts[corner]] += corner_normals[corner];
  }
  for (float3 &n : normals) {
    n = math::normalize(n);
  }

  Array<float3> sums(verts_num);
  Array<int> counts(verts_num);
  for ([[maybe_unused]] const int step : IndexRange(nmd.iterations)) {
    sums.fill(float3(0.0f));
    counts.fill(0);
    for (const int2 &edge : edges) {
      sums[edge[0]] += normals[edge[1]];
      sums[edge[1]] += normals[edge[0]];
      counts[edge[0]]++;
      counts[edge[1]]++;
    }
    threading::parallel_for(IndexRange(verts_num), 4096, [&](const IndexRange range) {
      for (const int v : range) {
        if (fixed[v] || counts[v] == 0 || weights[v] <= 0.0f) {
          continue;
        }
        const float3 average = math::normalize(sums[v]);
        if (math::is_zero(average)) {
          continue;
        }
        normals[v] = math::normalize(math::interpolate(normals[v], average, nmd.factor));
      }
    });
  }

  Mesh *result = BKE_mesh_copy_for_eval(*mesh);
  Array<float3> custom(corner_verts.size());
  threading::parallel_for(corner_verts.index_range(), 4096, [&](const IndexRange range) {
    for (const int corner : range) {
      const int v = corner_verts[corner];
      const float w = fixed[v] ? 0.0f : std::clamp(weights[v], 0.0f, 1.0f);
      custom[corner] = math::normalize(math::interpolate(corner_normals[corner], normals[v], w));
    }
  });
  bke::mesh_set_custom_normals_normalized(*result, custom);
  return result;
}

static void panel_draw(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);

  layout.use_property_split_set(true);
  layout.prop(ptr, "factor", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "iterations", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "use_keep_sharp", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  modifier_vgroup_ui(layout, ptr, &ob_ptr, "vertex_group", "invert_vertex_group", std::nullopt);
  modifier_error_message_draw(layout, ptr);
}

static void panel_register(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_NormalSmooth, panel_draw);
}

ModifierTypeInfo modifierType_NormalSmooth = {
    /*idname*/ "NormalSmooth",
    /*name*/ N_("Normal Smooth"),
    /*struct_name*/ "NormalSmoothModifierData",
    /*struct_size*/ sizeof(NormalSmoothModifierData),
    /*srna*/ &RNA_NormalSmoothModifier,
    /*type*/ ModifierTypeType::Constructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsMapping |
        eModifierTypeFlag_SupportsEditmode | eModifierTypeFlag_EnableInEditmode,
    /*icon*/ ICON_MOD_NORMAL_SMOOTH,
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
    /*is_disabled*/ is_disabled,
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
