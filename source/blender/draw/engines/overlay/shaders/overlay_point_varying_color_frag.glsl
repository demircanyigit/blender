/* SPDX-FileCopyrightText: 2016-2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/overlay_extra_infos.hh"

FRAGMENT_SHADER_CREATE_INFO(overlay_extra_grid_base)

#include "select_lib.glsl"

void main()
{
  /* Square points like Blender 2.79: the distance is the larger of the two axes, so the shape
   * fills the whole point and the crease ring below is square too. */
  float2 centered = abs(gl_PointCoord - float2(0.5f));
  float dist = max(centered.x, centered.y);
  float dist_squared = dist * dist;
  constexpr float rad_squared = 0.25f;

  if (dist_squared > rad_squared) {
    gpu_discard_fragment();
    return;
  }

#if defined(VERT)
  frag_color = final_color;

  float midStroke = 0.5f * rad_squared;
  if (vertex_crease > 0.0f && dist_squared > midStroke) {
    frag_color.rgb = mix(final_color.rgb, theme.colors.edge_crease.rgb, vertex_crease);
  }
#else
  frag_color = final_color;
#endif

#ifdef LINE_OUTPUT
  line_output = float4(0.0f);
#endif
  select_id_output(select_id);
}
