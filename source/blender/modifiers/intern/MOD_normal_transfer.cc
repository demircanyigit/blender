/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Normal Transfer: copies the shading normals of another object. The effect can fade out with
 * the distance to the target, and can be kept to the open edges of the mesh to hide a seam.
 */

#include <algorithm>
#include <cfloat>

#include "BLI_array.hh"
#include "BLI_kdopbvh.hh"
#include "BLI_kdtree.hh"
#include "BLI_math_geom.h"
#include "BLI_math_matrix.hh"
#include "BLI_math_vector.hh"
#include "BLI_task.hh"

#include "BLT_translation.hh"

#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_types.h"
#include "DNA_screen_types.h"

#include "BKE_attribute.hh"
#include "BKE_bvhutils.hh"
#include "BKE_deform.hh"
#include "BKE_lib_id.hh"
#include "BKE_lib_query.hh"
#include "BKE_mesh.hh"
#include "BKE_mesh_wrapper.hh"
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

namespace blender {

static void init_data(ModifierData *md)
{
  NormalTransferModifierData *data = reinterpret_cast<NormalTransferModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

static void required_data_mask(ModifierData *md, CustomData_MeshMasks *r_cddata_masks)
{
  const NormalTransferModifierData &nmd = *reinterpret_cast<NormalTransferModifierData *>(md);
  if (nmd.vertex_group[0] != '\0') {
    r_cddata_masks->vmask |= CD_MASK_MDEFORMVERT;
  }
}

static bool is_disabled(const Scene * /*scene*/, ModifierData *md, bool /*use_render_params*/)
{
  const NormalTransferModifierData &nmd = *reinterpret_cast<NormalTransferModifierData *>(md);
  return !nmd.target || nmd.target->type != OB_MESH || nmd.factor <= 0.0f;
}

static void foreach_ID_link(ModifierData *md, Object *ob, IDWalkFunc walk, void *user_data)
{
  NormalTransferModifierData *nmd = reinterpret_cast<NormalTransferModifierData *>(md);
  walk(user_data, ob, reinterpret_cast<ID **>(&nmd->target), IDWALK_CB_NOP);
}

static void update_depsgraph(ModifierData *md, const ModifierUpdateDepsgraphContext *ctx)
{
  NormalTransferModifierData *nmd = reinterpret_cast<NormalTransferModifierData *>(md);
  if (nmd->target != nullptr) {
    DEG_add_object_relation(
        ctx->node, nmd->target, DEG_OB_COMP_TRANSFORM, "Normal Transfer Modifier");
    DEG_add_object_relation(
        ctx->node, nmd->target, DEG_OB_COMP_GEOMETRY, "Normal Transfer Modifier");
  }
  DEG_add_depends_on_transform_relation(ctx->node, "Normal Transfer Modifier");
}

/**
 * Custom normals don't show on flat faces. Faces the modifier changes are made smooth, and their
 * edges to flat faces it doesn't change are made sharp, so those stay flat.
 */
static void smooth_changed_faces(Mesh &mesh, const Span<float> weights)
{
  bke::MutableAttributeAccessor attributes = mesh.attributes_for_write();
  Array<bool> sharp_faces;
  {
    const VArraySpan<bool> span = *attributes.lookup<bool>("sharp_face", bke::AttrDomain::Face);
    if (span.is_empty()) {
      return;
    }
    sharp_faces = Array<bool>(span);
  }
  const OffsetIndices faces = mesh.faces();
  const Span<int> corner_verts = mesh.corner_verts();
  const Span<int> corner_edges = mesh.corner_edges();

  Array<bool> changed(faces.size(), false);
  for (const int f : faces.index_range()) {
    for (const int corner : faces[f]) {
      if (weights[corner_verts[corner]] > 0.0f) {
        changed[f] = true;
        break;
      }
    }
  }
  /* Bit 1: next to a changed face. Bit 2: next to a flat face that isn't changed. */
  Array<uint8_t> edge_state(mesh.edges_num, 0);
  for (const int f : faces.index_range()) {
    const uint8_t bit = changed[f] ? 1 : (sharp_faces[f] ? 2 : 0);
    for (const int corner : faces[f]) {
      edge_state[corner_edges[corner]] |= bit;
    }
  }
  bke::SpanAttributeWriter<bool> sharp_edges = attributes.lookup_or_add_for_write_span<bool>(
      "sharp_edge", bke::AttrDomain::Edge);
  for (const int e : edge_state.index_range()) {
    if (edge_state[e] == 3) {
      sharp_edges.span[e] = true;
    }
  }
  sharp_edges.finish();

  bool any_sharp = false;
  for (const int f : faces.index_range()) {
    sharp_faces[f] = sharp_faces[f] && !changed[f];
    any_sharp |= sharp_faces[f];
  }
  attributes.remove("sharp_face");
  if (any_sharp) {
    attributes.add<bool>("sharp_face",
                         bke::AttrDomain::Face,
                         bke::AttributeInitVArray(VArray<bool>::from_span(sharp_faces)));
  }
  mesh.tag_sharpness_changed();
}

/** Closest hit of a ray from `origin` in either direction of `dir`. */
static bool ray_both_ways(const bke::BVHTreeFromMesh &tree,
                          const float3 &origin,
                          const float3 &dir,
                          int &r_index,
                          float3 &r_co)
{
  bool found = false;
  float best = FLT_MAX;
  for (const float sign : {1.0f, -1.0f}) {
    BVHTreeRayHit hit;
    hit.index = -1;
    hit.dist = best;
    const float3 d = dir * sign;
    if (BLI_bvhtree_ray_cast(tree.tree,
                             origin,
                             d,
                             0.0f,
                             &hit,
                             tree.raycast_callback,
                             const_cast<bke::BVHTreeFromMesh *>(&tree)) != -1 &&
        hit.index >= 0 && hit.dist < best)
    {
      best = hit.dist;
      r_index = hit.index;
      r_co = float3(hit.co);
      found = true;
    }
  }
  return found;
}

/** Weight from 1 near to 0 far: full up to `distance`, then fading out over `falloff`. */
static float distance_weight(const float dist, const float distance, const float falloff)
{
  if (dist <= distance) {
    return 1.0f;
  }
  if (falloff <= 0.0f || dist >= distance + falloff) {
    return 0.0f;
  }
  const float t = 1.0f - (dist - distance) / falloff;
  return t * t * (3.0f - 2.0f * t);
}

static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  const NormalTransferModifierData &nmd = *reinterpret_cast<const NormalTransferModifierData *>(
      md);
  Object *target = nmd.target;
  if (target == nullptr || target == ctx->object || mesh->corners_num == 0 || nmd.factor <= 0.0f)
  {
    return mesh;
  }
  Mesh *target_mesh = BKE_modifier_get_evaluated_mesh_from_evaluated_object(target);
  if (target_mesh == nullptr || target_mesh->faces_num == 0) {
    return mesh;
  }
  BKE_mesh_wrapper_ensure_mdata(target_mesh);

  const int verts_num = mesh->verts_num;
  const Span<float3> positions = mesh->vert_positions();

  /* Object space of this object to the space of the target, and back. */
  const float4x4 to_target = target->world_to_object() * ctx->object->object_to_world();
  const float4x4 from_target = math::invert(to_target);
  /* Normals of the target into this object's space. */
  const float3x3 normal_from_target = math::transpose(float3x3(to_target));
  const float3x3 world_3 = float3x3(ctx->object->object_to_world());

  /* How much each point takes, before the distance to the target. */
  Array<float> weights(verts_num, nmd.factor);
  const MDeformVert *dvert = nullptr;
  int defgrp_index = -1;
  MOD_get_vgroup(ctx->object, mesh, nmd.vertex_group, &dvert, &defgrp_index);
  if (dvert != nullptr && defgrp_index != -1) {
    const bool invert = nmd.flag & MOD_NORMAL_TRANSFER_INVERT_VGROUP;
    for (const int i : IndexRange(verts_num)) {
      const float w = BKE_defvert_find_weight(&dvert[i], defgrp_index);
      weights[i] *= invert ? 1.0f - w : w;
    }
  }

  if (nmd.flag & MOD_NORMAL_TRANSFER_EDGES_ONLY) {
    /* Points on edges with only one face, and the fade away from them. */
    Array<int> edge_faces(mesh->edges_num, 0);
    for (const int e : mesh->corner_edges()) {
      edge_faces[e]++;
    }
    const Span<int2> edges = mesh->edges();
    Array<bool> on_edge(verts_num, false);
    int on_edge_num = 0;
    for (const int e : edges.index_range()) {
      if (edge_faces[e] == 1) {
        for (const int v : {edges[e][0], edges[e][1]}) {
          if (!on_edge[v]) {
            on_edge[v] = true;
            on_edge_num++;
          }
        }
      }
    }
    if (on_edge_num == 0) {
      return mesh;
    }
    KDTree_3d *tree = kdtree_3d_new(on_edge_num);
    for (const int v : IndexRange(verts_num)) {
      if (on_edge[v]) {
        kdtree_3d_insert(tree, v, world_3 * positions[v]);
      }
    }
    kdtree_3d_balance(tree);
    threading::parallel_for(IndexRange(verts_num), 1024, [&](const IndexRange range) {
      for (const int v : range) {
        if (on_edge[v] || weights[v] <= 0.0f) {
          continue;
        }
        KDTreeNearest_3d nearest;
        if (kdtree_3d_find_nearest(tree, world_3 * positions[v], &nearest) == -1) {
          weights[v] = 0.0f;
          continue;
        }
        weights[v] *= distance_weight(nearest.dist, 0.0f, nmd.edge_distance);
      }
    });
    kdtree_3d_free(tree);
  }

  /* The target's normals: per corner, and per point as the average of its corners. Both keep
   * custom normals and sharp edges of the target. */
  const Span<float3> target_corner_normals = target_mesh->corner_normals();
  const Span<int3> target_tris = target_mesh->corner_tris();
  const Span<int> target_corner_verts = target_mesh->corner_verts();
  const Span<float3> target_positions = target_mesh->vert_positions();
  Array<float3> target_vert_normals;
  const bool by_point = nmd.mapping == MOD_NORMAL_TRANSFER_NEAREST_POINT;
  if (by_point) {
    target_vert_normals = Array<float3>(target_mesh->verts_num, float3(0.0f));
    for (const int corner : target_corner_verts.index_range()) {
      target_vert_normals[target_corner_verts[corner]] += target_corner_normals[corner];
    }
    for (float3 &n : target_vert_normals) {
      n = math::normalize(n);
    }
  }
  const bke::BVHTreeFromMesh tree = by_point ? target_mesh->bvh_verts() :
                                               target_mesh->bvh_corner_tris();
  if (tree.tree == nullptr) {
    return mesh;
  }

  /* The normal of the target at a point on one of its triangles. */
  auto tri_normal = [&](const int tri_index, const float3 &co) {
    const int3 tri = target_tris[tri_index];
    float w[3];
    interp_weights_tri_v3(w,
                          target_positions[target_corner_verts[tri[0]]],
                          target_positions[target_corner_verts[tri[1]]],
                          target_positions[target_corner_verts[tri[2]]],
                          co);
    return target_corner_normals[tri[0]] * w[0] + target_corner_normals[tri[1]] * w[1] +
           target_corner_normals[tri[2]] * w[2];
  };

  const Span<float3> own_vert_normals = mesh->vert_normals();
  const int axis = std::clamp(nmd.axis, 0, 5);
  float3 axis_dir(0.0f);
  axis_dir[axis % 3] = axis < 3 ? 1.0f : -1.0f;
  const bool use_distance = nmd.flag & MOD_NORMAL_TRANSFER_USE_DISTANCE;

  Array<float3> new_normals(verts_num, float3(0.0f));
  threading::parallel_for(IndexRange(verts_num), 256, [&](const IndexRange range) {
    for (const int v : range) {
      if (weights[v] <= 0.0f) {
        continue;
      }
      const float3 co_target = math::transform_point(to_target, positions[v]);
      float3 hit_co;
      float3 normal;
      if (ELEM(nmd.mapping, MOD_NORMAL_TRANSFER_ALONG_NORMAL, MOD_NORMAL_TRANSFER_PROJECTED)) {
        const float3 dir = nmd.mapping == MOD_NORMAL_TRANSFER_PROJECTED ? axis_dir :
                                                                          own_vert_normals[v];
        const float3 dir_target = math::normalize(float3x3(to_target) * dir);
        int index;
        if (!ray_both_ways(tree, co_target, dir_target, index, hit_co)) {
          weights[v] = 0.0f;
          continue;
        }
        normal = tri_normal(index, hit_co);
      }
      else {
        BVHTreeNearest nearest;
        nearest.index = -1;
        nearest.dist_sq = FLT_MAX;
        BLI_bvhtree_find_nearest(tree.tree,
                                 co_target,
                                 &nearest,
                                 tree.nearest_callback,
                                 const_cast<bke::BVHTreeFromMesh *>(&tree));
        if (nearest.index < 0) {
          weights[v] = 0.0f;
          continue;
        }
        hit_co = float3(nearest.co);
        normal = by_point ? target_vert_normals[nearest.index] : tri_normal(nearest.index, hit_co);
      }
      if (use_distance) {
        const float3 offset = math::transform_point(from_target, hit_co) - positions[v];
        weights[v] *= distance_weight(math::length(world_3 * offset), nmd.distance, nmd.falloff);
      }
      new_normals[v] = math::normalize(normal_from_target * normal);
    }
  });

  bool any = false;
  for (const int v : IndexRange(verts_num)) {
    if (weights[v] > 0.0f && !math::is_zero(new_normals[v])) {
      any = true;
    }
    else {
      weights[v] = 0.0f;
    }
  }
  if (!any) {
    return mesh;
  }

  Mesh *result = BKE_mesh_copy_for_eval(*mesh);
  smooth_changed_faces(*result, weights);

  const Span<int> corner_verts = result->corner_verts();
  const Span<float3> corner_normals = result->corner_normals();
  Array<float3> custom(corner_verts.size());
  threading::parallel_for(corner_verts.index_range(), 4096, [&](const IndexRange range) {
    for (const int corner : range) {
      const int v = corner_verts[corner];
      custom[corner] = math::normalize(
          math::interpolate(corner_normals[corner], new_normals[v], std::min(weights[v], 1.0f)));
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
  layout.prop(ptr, "target", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "mapping", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  if (RNA_enum_get(ptr, "mapping") == MOD_NORMAL_TRANSFER_PROJECTED) {
    layout.prop(ptr, "axis", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  layout.prop(ptr, "factor", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  ui::Layout &col = layout.column(false);
  col.prop(ptr, "use_distance", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  ui::Layout &sub = col.column(true);
  sub.active_set(RNA_boolean_get(ptr, "use_distance"));
  sub.prop(ptr, "distance", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  sub.prop(ptr, "falloff", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  ui::Layout &col2 = layout.column(false);
  col2.prop(ptr, "use_edges_only", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  ui::Layout &sub2 = col2.column(true);
  sub2.active_set(RNA_boolean_get(ptr, "use_edges_only"));
  sub2.prop(ptr, "edge_distance", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  modifier_vgroup_ui(layout, ptr, &ob_ptr, "vertex_group", "invert_vertex_group", std::nullopt);
  modifier_error_message_draw(layout, ptr);
}

static void panel_register(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_NormalTransfer, panel_draw);
}

ModifierTypeInfo modifierType_NormalTransfer = {
    /*idname*/ "NormalTransfer",
    /*name*/ N_("Normal Transfer"),
    /*struct_name*/ "NormalTransferModifierData",
    /*struct_size*/ sizeof(NormalTransferModifierData),
    /*srna*/ &RNA_NormalTransferModifier,
    /*type*/ ModifierTypeType::Constructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsMapping |
        eModifierTypeFlag_SupportsEditmode | eModifierTypeFlag_EnableInEditmode,
    /*icon*/ ICON_MOD_NORMAL_TRANSFER,
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
    /*update_depsgraph*/ update_depsgraph,
    /*depends_on_time*/ nullptr,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ foreach_ID_link,
    /*foreach_tex_link*/ nullptr,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ panel_register,
    /*blend_write*/ nullptr,
    /*blend_read*/ nullptr,
    /*foreach_cache*/ nullptr,
    /*foreach_working_space_color*/ nullptr,
};

}  // namespace blender
