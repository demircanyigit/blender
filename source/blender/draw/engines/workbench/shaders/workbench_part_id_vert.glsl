/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/workbench_effect_outline_infos.hh"

VERTEX_SHADER_CREATE_INFO(workbench_part_id)

#include "draw_model_lib.glsl"
#include "draw_view_clipping_lib.glsl"
#include "draw_view_lib.glsl"

void main()
{
  /* Same as the Workbench prepass, so the depth matches exactly. */
  float3 world_pos = drw_point_object_to_world(pos);
  gl_Position = drw_point_world_to_homogenous(world_pos);
  view_clipping_distances(world_pos);

  /* 0: loose part, 1: material, 2: face set. 0 is left for "no part". */
  part_id = uint(pid[part_type]) + 1u;
}
