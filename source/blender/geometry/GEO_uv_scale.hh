/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "BLI_math_matrix_types.hh"
#include "BLI_math_vector_types.hh"
#include "BLI_string_ref.hh"

namespace blender {
struct Mesh;
}

namespace blender::geometry {

struct UVScaleParams {
  bool texel_density = false;
  bool individual_islands = true;
  /* 0: bounds center, 1: UV origin, 2: custom pivot. */
  int pivot = 0;
  float2 center = float2(0.5f);
  float2 scale = float2(1.0f);
  float density = 1024.0f;
  int2 resolution = int2(1024);
  float meters_per_unit = 1.0f;
  float4x4 object_to_world = float4x4::identity();
};

/** Modify an existing corner UV map. Empty name uses the active map.
 * Returns a message for a missing map or degenerate density groups; other groups still work. */
const char *uv_scale(Mesh &mesh, StringRef uv_map, const UVScaleParams &params);

}  // namespace blender::geometry
