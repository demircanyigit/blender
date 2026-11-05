/* SPDX-FileCopyrightText: 2016-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/overlay_edit_mode_infos.hh"

FRAGMENT_SHADER_CREATE_INFO(overlay_edit_uv_verts)

void main()
{
  /* Square points like Blender 2.79: the distance is the larger of the two axes. */
  float2 centered = abs(gl_PointCoord - float2(0.5f));
  float dist = max(centered.x, centered.y);

  /* transparent outside of point
   * --- 0 ---
   * smooth transition
   * --- 1 ---
   * pure outline color
   * --- 2 ---
   * smooth transition
   * --- 3 ---
   * pure fill color
   * ...
   * dist = 0 at center of point */

  float midStroke = 0.5f * (radii[1] + radii[2]);

  if (dist > midStroke) {
    frag_color.rgb = outline_color.rgb;
    frag_color.a = mix(outline_color.a, 0.0f, smoothstep(radii[1], radii[0], dist));
  }
  else {
    frag_color = mix(fill_color, outline_color, smoothstep(radii[3], radii[2], dist));
  }
}
