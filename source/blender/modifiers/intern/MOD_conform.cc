/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Conform: lays the mesh onto the surface of another object. Each point keeps its height above
 * the bottom of the mesh, measured along the chosen axis, so the mesh keeps its thickness.
 */

#include <algorithm>
#include <cfloat>

#include "BLI_kdopbvh.hh"
#include "BLI_math_matrix.hh"
#include "BLI_math_vector.hh"
#include "BLI_task.hh"

#include "BLT_translation.hh"

#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_types.h"
#include "DNA_screen_types.h"

#include "BKE_bvhutils.hh"
#include "BKE_deform.hh"
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
  ConformModifierData *data = reinterpret_cast<ConformModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

static void required_data_mask(ModifierData *md, CustomData_MeshMasks *r_cddata_masks)
{
  const ConformModifierData &cmd = *reinterpret_cast<ConformModifierData *>(md);
  if (cmd.vertex_group[0] != '\0') {
    r_cddata_masks->vmask |= CD_MASK_MDEFORMVERT;
  }
}

static bool is_disabled(const Scene * /*scene*/, ModifierData *md, bool /*use_render_params*/)
{
  const ConformModifierData &cmd = *reinterpret_cast<ConformModifierData *>(md);
  return !cmd.target || cmd.target->type != OB_MESH;
}

static void foreach_ID_link(ModifierData *md, Object *ob, IDWalkFunc walk, void *user_data)
{
  ConformModifierData *cmd = reinterpret_cast<ConformModifierData *>(md);
  walk(user_data, ob, reinterpret_cast<ID **>(&cmd->target), IDWALK_CB_NOP);
}

static void update_depsgraph(ModifierData *md, const ModifierUpdateDepsgraphContext *ctx)
{
  ConformModifierData *cmd = reinterpret_cast<ConformModifierData *>(md);
  if (cmd->target != nullptr) {
    DEG_add_object_relation(ctx->node, cmd->target, DEG_OB_COMP_TRANSFORM, "Conform Modifier");
    DEG_add_object_relation(ctx->node, cmd->target, DEG_OB_COMP_GEOMETRY, "Conform Modifier");
  }
  DEG_add_depends_on_transform_relation(ctx->node, "Conform Modifier");
}

/** Closest hit of a ray from `origin` in either direction of `dir`. */
static bool ray_both_ways(const bke::BVHTreeFromMesh &tree,
                          const float3 &origin,
                          const float3 &dir,
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
      r_co = float3(hit.co);
      found = true;
    }
  }
  return found;
}

static void deform_verts(ModifierData *md,
                         const ModifierEvalContext *ctx,
                         Mesh *mesh,
                         MutableSpan<float3> positions)
{
  const ConformModifierData &cmd = *reinterpret_cast<const ConformModifierData *>(md);
  Object *target = cmd.target;
  if (target == nullptr || target == ctx->object || positions.is_empty()) {
    return;
  }
  Mesh *target_mesh = BKE_modifier_get_evaluated_mesh_from_evaluated_object(target);
  if (target_mesh == nullptr || target_mesh->faces_num == 0) {
    return;
  }
  BKE_mesh_wrapper_ensure_mdata(target_mesh);
  const bke::BVHTreeFromMesh tree = target_mesh->bvh_corner_tris();
  if (tree.tree == nullptr) {
    return;
  }

  /* Object space of this object to the space of the target, and back. */
  const float4x4 to_target = target->world_to_object() * ctx->object->object_to_world();
  const float4x4 from_target = math::invert(to_target);
  const float3x3 to_target_3 = float3x3(to_target);
  /* Normals of the target into this object's space. */
  const float3x3 normal_from_target = math::transpose(to_target_3);
  const float3x3 world_3 = float3x3(ctx->object->object_to_world());

  const int axis = std::clamp(cmd.axis, 0, 5);
  float3 up(0.0f);
  /* Points move along the axis, so "up" is the other way. */
  up[axis % 3] = axis < 3 ? -1.0f : 1.0f;
  const float3 dir_target = math::normalize(to_target_3 * -up);
  /* World length of one object unit along "up", so the offset is in world units. */
  const float up_world_scale = std::max(math::length(world_3 * up), 1e-8f);

  /* Height of each point above the bottom of the mesh. */
  float bottom = FLT_MAX;
  for (const float3 &p : positions) {
    bottom = std::min(bottom, math::dot(p, up));
  }
  const bool keep_thickness = cmd.flag & MOD_CONFORM_KEEP_THICKNESS;

  const MDeformVert *dvert = nullptr;
  int defgrp_index = -1;
  MOD_get_vgroup(ctx->object, mesh, cmd.vertex_group, &dvert, &defgrp_index);
  const bool invert_vgroup = cmd.flag & MOD_CONFORM_INVERT_VGROUP;

  threading::parallel_for(positions.index_range(), 512, [&](const IndexRange range) {
    for (const int i : range) {
      float weight = 1.0f;
      if (dvert != nullptr && defgrp_index != -1) {
        weight = invert_vgroup ? 1.0f - BKE_defvert_find_weight(&dvert[i], defgrp_index) :
                                 BKE_defvert_find_weight(&dvert[i], defgrp_index);
        if (weight <= 0.0f) {
          continue;
        }
      }
      const float3 p = positions[i];
      const float height = math::dot(p, up) - bottom;
      /* Where the point would be at the bottom of the mesh. */
      const float3 foot = p - up * height;
      const float3 foot_target = math::transform_point(to_target, foot);
      const float thickness = keep_thickness ? height : 0.0f;

      float3 result;
      if (cmd.mode == MOD_CONFORM_MODE_NEAREST) {
        BVHTreeNearest nearest;
        nearest.index = -1;
        nearest.dist_sq = FLT_MAX;
        BLI_bvhtree_find_nearest(tree.tree,
                                 foot_target,
                                 &nearest,
                                 tree.nearest_callback,
                                 const_cast<bke::BVHTreeFromMesh *>(&tree));
        if (nearest.index < 0) {
          continue;
        }
        float3 normal = math::normalize(normal_from_target * float3(nearest.no));
        if (math::is_zero(normal)) {
          normal = up;
        }
        const float normal_world_scale = std::max(math::length(world_3 * normal), 1e-8f);
        result = math::transform_point(from_target, float3(nearest.co)) +
                 normal * (thickness + cmd.offset / normal_world_scale);
      }
      else {
        float3 hit;
        if (!ray_both_ways(tree, foot_target, dir_target, hit)) {
          continue;
        }
        result = math::transform_point(from_target, hit) +
                 up * (thickness + cmd.offset / up_world_scale);
      }
      positions[i] = math::interpolate(p, result, weight);
    }
  });
}

static void panel_draw(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);

  layout.use_property_split_set(true);
  layout.prop(ptr, "target", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "mode", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "axis", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "offset", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "use_keep_thickness", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  modifier_vgroup_ui(layout, ptr, &ob_ptr, "vertex_group", "invert_vertex_group", std::nullopt);
  modifier_error_message_draw(layout, ptr);
}

static void panel_register(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_Conform, panel_draw);
}

ModifierTypeInfo modifierType_Conform = {
    /*idname*/ "Conform",
    /*name*/ N_("Conform"),
    /*struct_name*/ "ConformModifierData",
    /*struct_size*/ sizeof(ConformModifierData),
    /*srna*/ &RNA_ConformModifier,
    /*type*/ ModifierTypeType::OnlyDeform,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode |
        eModifierTypeFlag_EnableInEditmode,
    /*icon*/ ICON_MOD_CONFORM,
    /*copy_data*/ BKE_modifier_copydata_generic,
    /*deform_verts*/ deform_verts,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ nullptr,
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
