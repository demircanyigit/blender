/* SPDX-FileCopyrightText: 2020-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/workbench_effect_outline_infos.hh"

FRAGMENT_SHADER_CREATE_INFO(workbench_effect_outline)

#include "draw_view_lib.glsl"
#include "gpu_shader_math_constants_lib.glsl"

/* Bits of #WorldData.outline_flag, see #OutlinePass. */
#define OUTLINE_OBJECT 1
#define OUTLINE_PARTS 2
#define EDGE_DEPTH 4
#define EDGE_CREASE 8
#define EDGE_HARD 16

int2 texel_clamp(int2 texel)
{
  return clamp(texel, int2(0), textureSize(object_id_buffer, 0) - 1);
}

/** True when the texel is in another object, or in another part of the same object. */
bool outline_differs(int2 center, int2 other)
{
  uint center_object = texelFetch(object_id_buffer, center, 0).r;
  uint other_object = texelFetch(object_id_buffer, other, 0).r;
  if (center_object != other_object) {
    return true;
  }
  if ((world_data.outline_flag & OUTLINE_PARTS) == 0 || center_object == 0u) {
    return false;
  }
  return texelFetch(part_id_buffer, center, 0).r != texelFetch(part_id_buffer, other, 0).r;
}

float outline_opacity(int2 texel, float width)
{
  if (width <= 1.0f) {
    /* Same as before there was a width: a quarter per differing neighbor. */
    float opacity = 0.0f;
    opacity += outline_differs(texel, texel_clamp(texel + int2(1, 0))) ? 0.25f : 0.0f;
    opacity += outline_differs(texel, texel_clamp(texel - int2(1, 0))) ? 0.25f : 0.0f;
    opacity += outline_differs(texel, texel_clamp(texel + int2(0, 1))) ? 0.25f : 0.0f;
    opacity += outline_differs(texel, texel_clamp(texel - int2(0, 1))) ? 0.25f : 0.0f;
    return opacity;
  }
  /* Half the width on each side of the border. */
  float radius = width * 0.5f;
  int steps = int(ceil(radius));
  for (int step = 1; step <= steps; step++) {
    float dist = min(float(step), radius);
    for (int i = 0; i < 8; i++) {
      float angle = float(i) * (M_PI / 4.0f);
      int2 offset = int2(round(float2(cos(angle), sin(angle)) * dist));
      if (outline_differs(texel, texel_clamp(texel + offset))) {
        return 1.0f;
      }
    }
  }
  return 0.0f;
}

float depth_at(int2 texel)
{
  return texelFetch(depth_tx, texel_clamp(texel), 0).r;
}

float3 view_position_at(int2 texel)
{
  float2 uv = (float2(texel) + 0.5f) / float2(textureSize(depth_tx, 0));
  return drw_point_screen_to_view(float3(uv, depth_at(texel)));
}

/** Normal from the depth buffer, using the side that best continues the surface. */
float3 view_normal_at(int2 texel)
{
  float3 P = view_position_at(texel);
  float3 px = view_position_at(texel + int2(1, 0)) - P;
  float3 nx = P - view_position_at(texel - int2(1, 0));
  float3 py = view_position_at(texel + int2(0, 1)) - P;
  float3 ny = P - view_position_at(texel - int2(0, 1));
  float3 dx = (abs(px.z) < abs(nx.z)) ? px : nx;
  float3 dy = (abs(py.z) < abs(ny.z)) ? py : ny;
  return normalize(cross(dx, dy));
}

/** Same decoding as the Workbench gbuffer. */
float3 shading_normal_at(int2 texel)
{
  float2 fenc = texelFetch(normal_tx, texel_clamp(texel), 0).xy * 4.0f - 2.0f;
  float f = dot(fenc, fenc);
  float g = sqrt(max(1.0f - f / 4.0f, 0.0f));
  return float3(fenc * g, 1.0f - f / 2.0f);
}

float edge_opacity(int2 texel, float width)
{
  float center_depth = depth_at(texel);
  if (center_depth >= 1.0f) {
    return 0.0f;
  }
  int reach = max(1, int(round(width * 0.5f)));
  int2 offsets[4] = int2_array(
      int2(reach, 0), int2(-reach, 0), int2(0, reach), int2(0, -reach));

  if ((world_data.outline_flag & EDGE_DEPTH) != 0) {
    float center_z = abs(drw_depth_screen_to_view(center_depth));
    for (int i = 0; i < 4; i++) {
      float other_depth = depth_at(texel + offsets[i]);
      /* Only the front side of a jump gets the line. */
      if (other_depth >= 1.0f) {
        return 1.0f;
      }
      float other_z = abs(drw_depth_screen_to_view(other_depth));
      if (other_z - center_z > world_data.edge_depth_threshold * center_z) {
        return 1.0f;
      }
    }
  }

  if ((world_data.outline_flag & EDGE_HARD) != 0) {
    /* The shading normals split along sharp edges and between flat faces. */
    for (int i = 0; i < 4; i += 2) {
      int2 a = texel + offsets[i];
      int2 b = texel + offsets[i + 1];
      if (depth_at(a) >= 1.0f || depth_at(b) >= 1.0f) {
        continue;
      }
      if (dot(shading_normal_at(a), shading_normal_at(b)) < 0.999f) {
        return 1.0f;
      }
    }
  }

  if ((world_data.outline_flag & EDGE_CREASE) != 0) {
    for (int i = 0; i < 4; i += 2) {
      int2 a = texel + offsets[i];
      int2 b = texel + offsets[i + 1];
      if (depth_at(a) >= 1.0f || depth_at(b) >= 1.0f) {
        continue;
      }
      if (dot(view_normal_at(a), view_normal_at(b)) < world_data.edge_crease_cos) {
        return 1.0f;
      }
    }
  }
  return 0.0f;
}

void main()
{
  int2 texel = int2(gl_FragCoord.xy);
  float width = world_data.outline_width * world_data.ui_scale;

  float4 color = float4(0.0f);

  if ((world_data.outline_flag & (EDGE_DEPTH | EDGE_CREASE | EDGE_HARD)) != 0) {
    float edge = edge_opacity(texel, width) * world_data.edge_color.a;
    color = float4(world_data.edge_color.rgb * edge, edge);
  }

  if ((world_data.outline_flag & OUTLINE_OBJECT) != 0) {
    float outline = outline_opacity(texel, width);
    color = world_data.object_outline_color * outline + color * (1.0f - outline);
  }

  frag_color = color;
}
