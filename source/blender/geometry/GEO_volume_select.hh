/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

/** \file
 * \ingroup geo
 *
 * Select the points inside a gizmo shape, with a soft falloff, in the style of the 3ds Max
 * Vol. Select modifier. Used by the Volume Select modifier and node.
 */

#include "BLI_math_matrix_types.hh"
#include "BLI_math_vector_types.hh"
#include "BLI_span.hh"

namespace blender::geometry {

enum class VolumeSelectShape {
  Sphere = 0,
  Box = 1,
  Cylinder = 2,
};

struct VolumeSelectParams {
  VolumeSelectShape shape = VolumeSelectShape::Sphere;
  /** Gizmo center, rotation and size (the full width on each axis). */
  float3 center = float3(0.0f);
  float3 rotation = float3(0.0f);
  float3 size = float3(2.0f);
  /** Distance outside the shape over which the weight fades to zero. */
  float falloff = 0.0f;
  bool invert = false;
};

/** Precomputed form of #VolumeSelectParams, cheap to evaluate for many points. */
class VolumeSelector {
  float4x4 to_gizmo_;
  float3 half_size_;
  VolumeSelectShape shape_;
  float falloff_;
  bool invert_;

 public:
  VolumeSelector(const VolumeSelectParams &params);
  /** Weight from 0 to 1 of a point. */
  float weight(const float3 &position) const;
};

float4x4 volume_select_gizmo_matrix(const VolumeSelectParams &params);

void volume_select(Span<float3> positions,
                   const VolumeSelectParams &params,
                   MutableSpan<float> r_weights);

}  // namespace blender::geometry
