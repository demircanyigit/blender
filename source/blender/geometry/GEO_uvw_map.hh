/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

/** \file
 * \ingroup geo
 *
 * Project UV coordinates from a gizmo shape, in the style of the 3ds Max UVW Map modifier.
 * Used by the UVW Map modifier and node.
 */

#include "BLI_math_matrix_types.hh"
#include "BLI_math_vector_types.hh"
#include "BLI_offset_indices.hh"
#include "BLI_span.hh"

namespace blender::geometry {

enum class UVWMapType {
  Planar = 0,
  Cylindrical = 1,
  Spherical = 2,
  Box = 3,
  /** Each face gets the whole texture. */
  Face = 4,
};

struct UVWMapParams {
  UVWMapType type = UVWMapType::Planar;
  /** Gizmo center, rotation and size, in the space of the positions. */
  float3 center = float3(0.0f);
  float3 rotation = float3(0.0f);
  float3 size = float3(2.0f);
  /** Cylindrical: map the faces that point along the axis with a planar projection. */
  bool cap = false;
  float2 tile = float2(1.0f);
  float2 offset = float2(0.0f);
  bool flip_u = false;
  bool flip_v = false;
};

/** The transform from the unit gizmo (-0.5 to 0.5 on each axis) to the space of the positions. */
float4x4 uvw_map_gizmo_matrix(const UVWMapParams &params);

/** Compute a UV coordinate for every face corner. */
void uvw_map(Span<float3> positions,
             OffsetIndices<int> faces,
             Span<int> corner_verts,
             const UVWMapParams &params,
             MutableSpan<float2> r_uvs);

}  // namespace blender::geometry
