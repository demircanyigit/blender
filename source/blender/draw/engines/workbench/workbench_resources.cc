/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "../eevee/eevee_lut.hh" /* TODO: find somewhere to share blue noise Table. */

#include "BKE_studiolight.h"

#include "DNA_scene_types.h"

#include <algorithm>
#include <cmath>

#include "BLI_array.hh"
#include "BLI_math_matrix.h"
#include "BLI_task.hh"
#include "GPU_texture.hh"
#include "BLI_math_color.h"
#include "BLI_math_rotation.h"
#include "BLI_math_vector.h"

#include "GPU_batch_utils.hh"
#include "IMB_imbuf_types.hh"

#include "draw_common_c.hh"
#include "workbench_private.hh"

namespace blender::workbench {

static bool get_matcap_tx(Texture &matcap_tx, StudioLight &studio_light)
{
  BKE_studiolight_ensure_flag(&studio_light,
                              STUDIOLIGHT_MATCAP_DIFFUSE_GPUTEXTURE |
                                  STUDIOLIGHT_MATCAP_SPECULAR_GPUTEXTURE);
  ImBuf *matcap_diffuse = studio_light.matcap_diffuse.ibuf;
  ImBuf *matcap_specular = studio_light.matcap_specular.ibuf;
  if (matcap_diffuse && matcap_diffuse->float_buffer.data) {
    int layers = 1;
    float *buffer = matcap_diffuse->float_buffer.data;
    Vector<float> combined_buffer;

    if (matcap_specular && matcap_specular->float_buffer.data) {
      int size = matcap_diffuse->x * matcap_diffuse->y * 4;
      combined_buffer.extend(matcap_diffuse->float_buffer.data, size);
      combined_buffer.extend(matcap_specular->float_buffer.data, size);
      buffer = combined_buffer.begin();
      layers++;
    }

    matcap_tx = Texture(studio_light.name,
                        gpu::TextureFormat::SFLOAT_16_16_16_16,
                        GPU_TEXTURE_USAGE_SHADER_READ,
                        int2(matcap_diffuse->x, matcap_diffuse->y),
                        layers,
                        buffer);
    return true;
  }
  return false;
}

static float4x4 get_world_shading_rotation_matrix(float studiolight_rot_z)
{
  float4x4 V = draw::View::default_get().viewmat();
  float R[4][4];
  axis_angle_to_mat4_single(R, 'Z', -studiolight_rot_z);
  mul_m4_m4m4(R, V.ptr(), R);
  swap_v3_v3(R[2], R[1]);
  negate_v3(R[2]);
  return float4x4(R);
}

static SolidLightData get_light_data_from_studio_solidlight(const SolidLight *sl,
                                                            const float4x4 &world_shading_rotation)
{
  SolidLightData light = {};
  if (sl && sl->flag) {
    float3 direction = math::transform_direction(world_shading_rotation, float3(sl->vec));
    light.direction = float4(direction, 0.0f);
    /* We should pre-divide the power by PI but that makes the lights really dim. */
    light.specular_color = float4(float3(sl->spec), 0.0f);
    light.diffuse_color_wrap = float4(float3(sl->col), sl->smooth);
  }
  else {
    light.direction = float4(1.0f, 0.0f, 0.0f, 0.0f);
    light.specular_color = float4(0.0f);
    light.diffuse_color_wrap = float4(0.0f);
  }
  return light;
}

void SceneResources::load_jitter_tx(int total_samples)
{
  float4 jitter[jitter_tx_size][jitter_tx_size];

  const float total_samples_inv = 1.0f / total_samples;

  /* Create blue noise jitter texture */
  for (int x = 0; x < 64; x++) {
    for (int y = 0; y < 64; y++) {
      float phi = eevee::lut::blue_noise[y][x][0] * 2.0f * M_PI;
      /* This rotate the sample per pixels */
      jitter[y][x].x = math::cos(phi);
      jitter[y][x].y = math::sin(phi);
      /* This offset the sample along its direction axis (reduce banding) */
      float bn = eevee::lut::blue_noise[y][x][1] - 0.5f;
      bn = clamp_f(bn, -0.499f, 0.499f); /* fix fireflies */
      jitter[y][x].z = bn * total_samples_inv;
      jitter[y][x].w = eevee::lut::blue_noise[y][x][1];
    }
  }

  jitter_tx.free();
  jitter_tx.ensure_2d(gpu::TextureFormat::SFLOAT_16_16_16_16,
                      int2(jitter_tx_size),
                      GPU_TEXTURE_USAGE_SHADER_READ,
                      jitter[0][0]);
}

static float3 to_linear(const float3 &c)
{
  return float3(powf(c.x, 2.2f), powf(c.y, 2.2f), powf(c.z, 2.2f));
}

/** Color for the square at (sx, sy) of the Color checker. */
static float4 checker_grid_color(const int sx, const int sy, const int squares)
{
  const float hue = float(sx) / float(squares);
  /* Rows go from darker to lighter, and every other square is darker still. */
  const float row = squares > 1 ? float(sy) / float(squares - 1) : 0.5f;
  const bool odd = (sx + sy) & 1;
  const float saturation = odd ? 0.75f : 0.55f;
  const float value = (odd ? 0.45f : 0.75f) + 0.25f * row;
  float3 rgb;
  hsv_to_rgb(hue, saturation, std::min(value, 1.0f), &rgb.x, &rgb.y, &rgb.z);
  /* The texture is read as linear. */
  return float4(to_linear(rgb), 1.0f);
}

void SceneResources::load_checker_tx(const int squares, const int type)
{
  if (squares == checker_squares && type == checker_type && checker_tx.is_valid()) {
    return;
  }
  checker_squares = squares;
  checker_type = type;
  constexpr int size = 1024;
  constexpr int mips = 11;
  const float4 light(0.62f, 0.62f, 0.62f, 1.0f);
  const float4 dark(0.24f, 0.24f, 0.24f, 1.0f);
  Array<float4> pixels(size * size);
  threading::parallel_for(IndexRange(size), 32, [&](const IndexRange rows) {
    for (const int y : rows) {
      const int sy = y * squares / size;
      for (const int x : IndexRange(size)) {
        const int sx = x * squares / size;
        const bool odd = (sx + sy) & 1;
        float4 color;
        if (type == V3D_SHADING_CHECKER_COLOR_GRID) {
          color = checker_grid_color(sx, sy, squares);
          /* A thin dark line around each square. */
          const int px = x * squares % size;
          const int py = y * squares % size;
          const int line = squares * 2;
          if (px < line || py < line) {
            color = float4(color.xyz() * 0.35f, 1.0f);
          }
        }
        else if (type == V3D_SHADING_CHECKER_UV) {
          const float u = (float(x) + 0.5f) / float(size);
          const float v = (float(y) + 0.5f) / float(size);
          const float3 rgb = float3(u, v, 0.15f) * (odd ? 0.55f : 1.0f);
          color = float4(to_linear(rgb), 1.0f);
        }
        else {
          color = odd ? dark : light;
        }
        pixels[y * size + x] = color;
      }
    }
  });
  checker_tx.free();
  checker_tx.ensure_2d(gpu::TextureFormat::UNORM_8_8_8_8,
                       int2(size),
                       GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT,
                       reinterpret_cast<const float *>(pixels.data()),
                       mips);
  GPU_texture_update_mipmap_chain(checker_tx);

  checker_texture = {};
  checker_texture.gpu.texture = &checker_tx;
  checker_texture.name = "Checker";
  checker_texture.sampler_state.extend_x = GPU_SAMPLER_EXTEND_MODE_REPEAT;
  checker_texture.sampler_state.extend_yz = GPU_SAMPLER_EXTEND_MODE_REPEAT;
  checker_texture.sampler_state.enable_filtering_flag(GPU_SAMPLER_FILTERING_LINEAR |
                                                      GPU_SAMPLER_FILTERING_MIPMAP |
                                                      GPU_SAMPLER_FILTERING_ANISOTROPIC);
}

void SceneResources::init(const SceneState &scene_state, const DRWContext *ctx)
{
  const View3DShading &shading = scene_state.shading;

  world_buf.viewport_size = ctx->viewport_size_get();
  world_buf.viewport_size_inv = 1.0f / world_buf.viewport_size;
  world_buf.xray_alpha = shading.xray_alpha;
  world_buf.background_color = scene_state.background_color;
  world_buf.object_outline_color = float4(float3(shading.object_outline_color), 1.0f);
  world_buf.edge_color = float4(float3(shading.edge_color), shading.edge_strength);
  world_buf.outline_width = max_ff(shading.outline_width, 1.0f);
  world_buf.edge_crease_cos = cosf(shading.edge_crease_angle);
  world_buf.edge_depth_threshold = shading.edge_depth_threshold;
  world_buf.outline_flag = scene_state.outline_flag;

  world_buf.toon_flag = 0;
  if (shading.light == V3D_LIGHTING_TOON) {
    world_buf.toon_flag = 1;
    if (shading.toon_flag & V3D_SHADING_TOON_HIGHLIGHT) {
      world_buf.toon_flag |= 2;
    }
    if (shading.toon_flag & V3D_SHADING_TOON_RIM) {
      world_buf.toon_flag |= 4;
    }
    const float3 direction = scene_state.scene->display.light_direction;
    float3 to_light;
    if (shading.toon_flag & V3D_SHADING_TOON_VIEW_LIGHT) {
      /* With the default direction the light is up, left and in front of the view. */
      to_light = float3(-direction.x, direction.y, direction.z);
    }
    else {
      /* The same light as the shadow, see #ShadowPass::init. */
      const float3 to_light_ws = float3(direction.x, -direction.z, direction.y);
      to_light = math::transform_direction(draw::View::default_get().viewmat(), to_light_ws);
    }
    world_buf.toon_light_vs = float4(math::normalize(to_light), float(shading.toon_bands));
    world_buf.toon_shadow_color = float4(float3(shading.toon_shadow_color),
                                         shading.toon_softness);
    world_buf.toon_rim_color = float4(float3(shading.toon_rim_color), shading.toon_rim_width);
    world_buf.toon_highlight_size = shading.toon_highlight_size;
  }
  world_buf.ui_scale = ctx->is_image_render() ? 1.0f : U.pixelsize;
  world_buf.matcap_orientation = (shading.flag & V3D_SHADING_MATCAP_FLIP_X) != 0;

  StudioLight *studio_light = nullptr;
  if (U.edit_studio_light) {
    studio_light = BKE_studiolight_studio_edit_get();
  }
  else {
    if (shading.light == V3D_LIGHTING_MATCAP) {
      studio_light = BKE_studiolight_find(shading.matcap, STUDIOLIGHT_TYPE_MATCAP);
      if (studio_light && studio_light->name != current_matcap) {
        if (get_matcap_tx(matcap_tx, *studio_light)) {
          current_matcap = studio_light->name;
        }
      }
    }
    /* If matcaps are missing, use this as fallback. */
    if (studio_light == nullptr) {
      studio_light = BKE_studiolight_find(shading.studio_light, STUDIOLIGHT_TYPE_STUDIO);
    }
  }
  if (!matcap_tx.is_valid()) {
    matcap_tx.ensure_2d_array(
        gpu::TextureFormat::SFLOAT_16_16_16_16, int2(1), 1, GPU_TEXTURE_USAGE_SHADER_READ);
  }

  float4x4 world_shading_rotation = float4x4::identity();
  if (shading.flag & V3D_SHADING_WORLD_ORIENTATION) {
    world_shading_rotation = get_world_shading_rotation_matrix(shading.studiolight_rot_z);
  }

  for (int i = 0; i < 4; i++) {
    SolidLight *sl = (studio_light) ? &studio_light->light[i] : nullptr;
    world_buf.lights[i] = get_light_data_from_studio_solidlight(sl, world_shading_rotation);
  }

  if (studio_light != nullptr) {
    world_buf.ambient_color = float4(float3(studio_light->light_ambient), 0.0f);
    world_buf.use_specular = shading.flag & V3D_SHADING_SPECULAR_HIGHLIGHT &&
                             studio_light->flag & STUDIOLIGHT_SPECULAR_HIGHLIGHT_PASS;
  }
  else {
    world_buf.ambient_color = float4(1.0f, 1.0f, 1.0f, 0.0f);
    world_buf.use_specular = false;
  }

  /* TODO(@pragma37): volumes_do */

  cavity.init(scene_state, *this);

  if (scene_state.draw_dof && !jitter_tx.is_valid()) {
    /* We don't care about total_samples in this case */
    load_jitter_tx(1);
  }

  world_buf.push_update();

  for (int i : IndexRange(6)) {
    if (i < scene_state.clip_planes.size()) {
      clip_planes_buf[i] = scene_state.clip_planes[i];
    }
    else {
      clip_planes_buf[i] = float4(0);
    }
  }

  clip_planes_buf.push_update();

  missing_tx.ensure_2d(gpu::TextureFormat::UNORM_8_8_8_8,
                       int2(1),
                       GPU_TEXTURE_USAGE_SHADER_READ,
                       float4(1.0f, 0.0f, 1.0f, 1.0f));
  missing_texture.gpu.texture = &missing_tx;
  missing_texture.name = "Missing Texture";

  if (shading.color_type == V3D_SHADING_CHECKER_COLOR) {
    load_checker_tx(std::clamp(int(std::round(shading.checker_scale)), 1, 256),
                    shading.checker_type);
  }

  dummy_texture_tx.ensure_2d(gpu::TextureFormat::UNORM_8_8_8_8,
                             int2(1),
                             GPU_TEXTURE_USAGE_SHADER_READ,
                             float4(0.0f, 0.0f, 0.0f, 0.0f));
  dummy_tile_array_tx.ensure_2d_array(gpu::TextureFormat::UNORM_8_8_8_8,
                                      int2(1),
                                      1,
                                      GPU_TEXTURE_USAGE_SHADER_READ,
                                      float4(0.0f, 0.0f, 0.0f, 0.0f));
  dummy_tile_data_tx.ensure_1d_array(gpu::TextureFormat::UNORM_8_8_8_8,
                                     1,
                                     1,
                                     GPU_TEXTURE_USAGE_SHADER_READ,
                                     float4(0.0f, 0.0f, 0.0f, 0.0f));

  if (volume_cube_batch == nullptr) {
    volume_cube_batch = GPU_batch_unit_cube();
  }
}

}  // namespace blender::workbench
