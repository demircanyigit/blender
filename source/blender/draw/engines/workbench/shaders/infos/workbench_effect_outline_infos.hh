/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#ifdef GPU_SHADER
#  pragma once
#  include "gpu_shader_compat.hh"

#  include "draw_object_infos_infos.hh"
#  include "draw_view_infos.hh"

#  include "workbench_shader_shared.hh"
#endif

#include "workbench_defines.hh"

#include "gpu_shader_create_info.hh"

GPU_SHADER_CREATE_INFO(workbench_effect_outline)
TYPEDEF_SOURCE("workbench_shader_shared.hh")
FRAGMENT_SOURCE("workbench_effect_outline_frag.glsl")
VERTEX_SOURCE("workbench_fullscreen_vert.glsl")
SAMPLER(0, usampler2D, object_id_buffer)
SAMPLER(1, usampler2D, part_id_buffer)
SAMPLER(2, sampler2DDepth, depth_tx)
SAMPLER(3, sampler2D, normal_tx)
UNIFORM_BUF(WB_WORLD_SLOT, WorldData, world_data)
FRAGMENT_OUT(0, float4, frag_color)
ADDITIONAL_INFO(draw_view)
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()

/* Writes the loose part, material or face set of each mesh face, for the outline. */
GPU_SHADER_INTERFACE_INFO(workbench_part_id_iface)
FLAT(uint, part_id)
GPU_SHADER_INTERFACE_END()

GPU_SHADER_CREATE_INFO(workbench_part_id)
VERTEX_IN(0, float3, pos)
VERTEX_IN(1, int3, pid)
PUSH_CONSTANT(int, part_type)
VERTEX_OUT(workbench_part_id_iface)
FRAGMENT_OUT(0, uint, out_part_id)
VERTEX_SOURCE("workbench_part_id_vert.glsl")
FRAGMENT_SOURCE("workbench_part_id_frag.glsl")
ADDITIONAL_INFO(draw_view)
ADDITIONAL_INFO(draw_modelmat)
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()

CREATE_INFO_VARIANT(workbench_part_id_clipped, workbench_part_id, drw_clipped)
