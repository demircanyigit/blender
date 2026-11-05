/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <algorithm>

#include "BLI_math_euler.hh"
#include "BLI_math_matrix.hh"
#include "BLI_math_vector.hh"
#include "BLI_task.hh"

#include "GEO_volume_select.hh"

namespace blender::geometry {

float4x4 volume_select_gizmo_matrix(const VolumeSelectParams &params)
{
  return math::from_loc_rot<float4x4>(params.center, math::EulerXYZ(params.rotation));
}

VolumeSelector::VolumeSelector(const VolumeSelectParams &params)
    : to_gizmo_(math::invert(volume_select_gizmo_matrix(params))),
      half_size_(math::max(math::abs(params.size) * 0.5f, float3(1e-6f))),
      shape_(params.shape),
      falloff_(std::max(params.falloff, 0.0f)),
      invert_(params.invert)
{
}

float VolumeSelector::weight(const float3 &position) const
{
  const float3 p = math::transform_point(to_gizmo_, position);
  /* Distance outside of the shape, in world units, or zero inside it. */
  float distance = 0.0f;
  switch (shape_) {
    case VolumeSelectShape::Sphere: {
      /* Scale to a unit sphere to find the direction, then measure in real units. */
      const float3 unit = p / half_size_;
      const float len = math::length(unit);
      if (len > 1.0f) {
        const float3 surface = p / len;
        distance = math::distance(p, surface);
      }
      break;
    }
    case VolumeSelectShape::Box: {
      const float3 outside = math::max(math::abs(p) - half_size_, float3(0.0f));
      distance = math::length(outside);
      break;
    }
    case VolumeSelectShape::Cylinder: {
      const float2 xy(p.x / half_size_.x, p.y / half_size_.y);
      const float r = math::length(xy);
      float radial = 0.0f;
      if (r > 1.0f) {
        const float2 real(p.x, p.y);
        radial = math::length(real) * (1.0f - 1.0f / r);
      }
      const float axial = std::max(std::abs(p.z) - half_size_.z, 0.0f);
      distance = math::length(float2(radial, axial));
      break;
    }
  }
  float weight;
  if (distance <= 0.0f) {
    weight = 1.0f;
  }
  else if (falloff_ > 0.0f && distance < falloff_) {
    const float t = 1.0f - distance / falloff_;
    weight = t * t * (3.0f - 2.0f * t);
  }
  else {
    weight = 0.0f;
  }
  return invert_ ? 1.0f - weight : weight;
}

void volume_select(const Span<float3> positions,
                   const VolumeSelectParams &params,
                   MutableSpan<float> r_weights)
{
  const VolumeSelector selector(params);
  threading::parallel_for(positions.index_range(), 4096, [&](const IndexRange range) {
    for (const int i : range) {
      r_weights[i] = selector.weight(positions[i]);
    }
  });
}

}  // namespace blender::geometry
