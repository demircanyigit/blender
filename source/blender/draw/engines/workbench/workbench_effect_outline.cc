/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 *
 * Outline Effect:
 *
 * Samples an object id buffer to detect objects outlines. For outlines around loose parts,
 * materials or face sets, meshes are drawn again into a part id buffer, on top of the depth of
 * the opaque pass. Edge lines are found from the depth buffer.
 */

#include "DNA_view3d_types.h"

#include "draw_cache.hh"

#include "workbench_private.hh"

namespace blender::workbench {

void OutlinePass::init(const SceneState &scene_state)
{
  enabled_ = scene_state.draw_outline;
  if (!enabled_) {
    return;
  }
  use_part_ids_ = scene_state.draw_part_ids;
  use_clipping_ = !scene_state.clip_planes.is_empty();
  switch (scene_state.shading.outline_type) {
    case V3D_SHADING_OUTLINE_MATERIAL:
      part_type_ = 1;
      break;
    case V3D_SHADING_OUTLINE_FACE_SET:
      part_type_ = 2;
      break;
    default:
      part_type_ = 0;
      break;
  }
}

void OutlinePass::sync(const SceneState &scene_state, SceneResources &resources)
{
  if (!enabled_) {
    return;
  }

  if (use_part_ids_) {
    part_ps_.init();
    part_ps_.state_set(DRW_STATE_WRITE_COLOR | DRW_STATE_DEPTH_LESS_EQUAL |
                       scene_state.cull_state);
    part_ps_.shader_set(use_clipping_ ? ShaderCache::get().part_id_clipped.get() :
                                        ShaderCache::get().part_id.get());
    part_ps_.bind_ubo(DRW_CLIPPING_UBO_SLOT, resources.clip_planes_buf);
    part_ps_.push_constant("part_type", part_type_);
  }

  ps_.init();
  ps_.state_set(DRW_STATE_WRITE_COLOR | DRW_STATE_BLEND_ALPHA_PREMUL);
  ps_.shader_set(ShaderCache::get().outline.get());
  ps_.bind_ubo("world_data", resources.world_buf);
  ps_.bind_texture("object_id_buffer", &resources.object_id_tx);
  /* Without part ids the object ids are bound in their place, the shader doesn't read them. */
  ps_.bind_texture("part_id_buffer",
                   use_part_ids_ ? &part_id_tx_ : &resources.object_id_tx);
  ps_.bind_texture("depth_tx", &resources.depth_tx);
  ps_.bind_texture("normal_tx", &normal_tx_);
  ps_.draw_procedural(GPU_PRIM_TRIS, 1, 3);
}

void OutlinePass::object_sync(ObjectRef &ob_ref, ResourceHandleRange handle)
{
  if (!use_part_ids()) {
    return;
  }
  if (gpu::Batch *batch = DRW_cache_mesh_surface_part_ids_get(ob_ref.object)) {
    part_ps_.draw(batch, handle);
  }
}

void OutlinePass::draw(Manager &manager,
                       View &view,
                       SceneResources &resources,
                       int2 resolution,
                       gpu::Texture *normal_tx)
{
  if (!enabled_) {
    return;
  }
  normal_tx_.wrap(normal_tx);

  if (use_part_ids_) {
    part_id_tx_.acquire(resolution,
                        gpu::TextureFormat::UINT_32,
                        GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT);
    part_fb_.ensure(GPU_ATTACHMENT_TEXTURE(resources.depth_tx),
                    GPU_ATTACHMENT_TEXTURE(part_id_tx_));
    part_fb_.bind();
    GPU_framebuffer_clear_color(part_fb_, float4(0.0f));
    manager.submit(part_ps_, view);
  }

  fb_.ensure(GPU_ATTACHMENT_NONE, GPU_ATTACHMENT_TEXTURE(resources.color_tx));
  fb_.bind();
  manager.submit(ps_, view);

  if (use_part_ids_) {
    part_id_tx_.release();
  }
}

}  // namespace blender::workbench
