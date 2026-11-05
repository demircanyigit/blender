/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <algorithm>
#include <cfloat>

#include "BLI_math_base.hh"
#include "BLI_math_euler.hh"
#include "BLI_math_matrix.hh"
#include "BLI_math_rotation.hh"
#include "BLI_math_vector.hh"
#include "BLI_task.hh"

#include "GEO_axis_deform.hh"

namespace blender::geometry {

float4x4 axis_deform_gizmo_matrix(const AxisDeformParams &params)
{
  return math::from_loc_rot<float4x4>(params.center, math::EulerXYZ(params.rotation));
}

static float2 rotate_2d(const float2 &p, const float angle)
{
  const float c = std::cos(angle);
  const float s = std::sin(angle);
  return {p.x * c - p.y * s, p.x * s + p.y * c};
}

/**
 * Deform a point given in a frame where z runs along the axis. \a lower and \a upper are the
 * range along z that is deformed, the rest moves rigidly with the ends.
 */
static float3 deform_point(const AxisDeformParams &params,
                           const float3 &p,
                           const float lower,
                           const float upper)
{
  const float length = std::max(upper - lower, 1e-6f);
  const float z_clamped = std::clamp(p.z, lower, upper);
  const float dz = p.z - z_clamped;

  switch (params.type) {
    case AxisDeformType::Bend: {
      const float k = params.angle / length;
      if (std::abs(k) < 1e-7f) {
        return p;
      }
      /* Bend in the plane of x and z, after turning the bend direction onto x. */
      const float2 xy = rotate_2d(float2(p.x, p.y), -params.direction);
      const float r = 1.0f / k;
      const float theta = z_clamped * k;
      const float s = std::sin(theta);
      const float c = std::cos(theta);
      float x = r - (r - xy.x) * c;
      float z = (r - xy.x) * s;
      /* Points past the limits continue along the tangent at the end. */
      x += dz * s;
      z += dz * c;
      const float2 out = rotate_2d(float2(x, xy.y), params.direction);
      return {out.x, out.y, z};
    }
    case AxisDeformType::Twist: {
      const float theta = params.angle * z_clamped / length;
      const float2 xy = rotate_2d(float2(p.x, p.y), theta);
      return {xy.x, xy.y, p.z};
    }
    case AxisDeformType::Taper: {
      const float t = z_clamped / length;
      const float u = (z_clamped - lower) / length;
      const float scale = 1.0f + params.amount * t + params.curve * 4.0f * u * (1.0f - u);
      return {p.x * scale, p.y * scale, p.z};
    }
    case AxisDeformType::Stretch: {
      const float middle = (lower + upper) * 0.5f;
      const float u = (z_clamped - middle) / (length * 0.5f);
      const float stretch = std::max(1.0f + params.amount, 1e-4f);
      /* Thinner in the middle, to roughly keep the volume. */
      const float thin = 1.0f / std::sqrt(stretch) - 1.0f;
      const float scale = 1.0f + thin * (1.0f + params.curve) * (1.0f - u * u);
      const float z = middle + (z_clamped - middle) * stretch + dz;
      return {p.x * scale, p.y * scale, z};
    }
  }
  return p;
}

void axis_deform(MutableSpan<float3> positions,
                 const AxisDeformParams &params,
                 const Span<float> weights)
{
  if (positions.is_empty()) {
    return;
  }
  const float4x4 from_gizmo = axis_deform_gizmo_matrix(params);
  const float4x4 to_gizmo = math::invert(from_gizmo);
  const int axis = std::clamp(params.axis, 0, 2);
  const int axis_x = (axis + 1) % 3;
  const int axis_y = (axis + 2) % 3;

  float lower = std::min(params.lower, params.upper);
  float upper = std::max(params.lower, params.upper);
  if (!params.use_limits) {
    lower = FLT_MAX;
    upper = -FLT_MAX;
    for (const float3 &position : positions) {
      const float z = math::transform_point(to_gizmo, position)[axis];
      lower = std::min(lower, z);
      upper = std::max(upper, z);
    }
  }

  threading::parallel_for(positions.index_range(), 2048, [&](const IndexRange range) {
    for (const int i : range) {
      const float weight = weights.is_empty() ? 1.0f : weights[i];
      if (weight <= 0.0f) {
        continue;
      }
      const float3 co = math::transform_point(to_gizmo, positions[i]);
      const float3 local(co[axis_x], co[axis_y], co[axis]);
      const float3 deformed = deform_point(params, local, lower, upper);
      float3 out;
      out[axis_x] = deformed.x;
      out[axis_y] = deformed.y;
      out[axis] = deformed.z;
      const float3 result = math::transform_point(from_gizmo, out);
      positions[i] = weight >= 1.0f ? result : math::interpolate(positions[i], result, weight);
    }
  });
}

}  // namespace blender::geometry
