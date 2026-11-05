/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <algorithm>
#include <cfloat>
#include <cmath>

#include "BLI_bounds.hh"
#include "BLI_hash.h"
#include "BLI_math_vector.hh"
#include "BLI_noise.hh"
#include "BLI_task.hh"

#include "GEO_deform_ops.hh"

namespace blender::geometry {

static float weight_at(const Span<float> weights, const int i)
{
  return weights.is_empty() ? 1.0f : weights[i];
}

/* -------------------------------------------------------------------- */
/** \name Noise
 * \{ */

void noise_deform(MutableSpan<float3> positions,
                  const Span<float> weights,
                  const NoiseDeformParams &params)
{
  if (math::is_zero(params.strength) || params.scale == 0.0f) {
    return;
  }
  /* A different far-away place in the noise for each axis and seed. */
  float3 axis_offsets[3];
  for (int axis = 0; axis < 3; axis++) {
    axis_offsets[axis] = float3(BLI_hash_int_3d_to_float(uint32_t(params.seed), axis, 0),
                                BLI_hash_int_3d_to_float(uint32_t(params.seed), axis, 1),
                                BLI_hash_int_3d_to_float(uint32_t(params.seed), axis, 2)) *
                         1000.0f;
  }
  const float inv_scale = 1.0f / params.scale;
  const float detail = params.fractal ? params.detail : 0.0f;
  threading::parallel_for(positions.index_range(), 1024, [&](const IndexRange range) {
    for (const int i : range) {
      const float w = weight_at(weights, i);
      if (w == 0.0f) {
        continue;
      }
      const float3 p = positions[i] * inv_scale;
      float3 offset;
      for (int axis = 0; axis < 3; axis++) {
        if (params.strength[axis] == 0.0f) {
          offset[axis] = 0.0f;
          continue;
        }
        const float3 q = p + axis_offsets[axis];
        const float value = noise::perlin_fbm<float4>(float4(q.x, q.y, q.z, params.phase),
                                                      detail,
                                                      params.roughness,
                                                      params.lacunarity,
                                                      true);
        offset[axis] = (value * 2.0f - 1.0f) * params.strength[axis];
      }
      positions[i] += offset * w;
    }
  });
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Push
 * \{ */

void push_deform(MutableSpan<float3> positions,
                 const Span<float3> normals,
                 const Span<float> weights,
                 const float distance)
{
  if (distance == 0.0f) {
    return;
  }
  threading::parallel_for(positions.index_range(), 4096, [&](const IndexRange range) {
    for (const int i : range) {
      positions[i] += normals[i] * (distance * weight_at(weights, i));
    }
  });
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Melt
 * \{ */

void melt_deform(MutableSpan<float3> positions,
                 const Span<float> weights,
                 const MeltParams &params)
{
  if (positions.is_empty() || params.amount == 0.0f) {
    return;
  }
  const int up = std::clamp(params.axis, 0, 2);
  const int side_a = (up + 1) % 3;
  const int side_b = (up + 2) % 3;
  const float sign = params.flip ? -1.0f : 1.0f;

  const Bounds<float3> bounds = *bounds::min_max(positions.as_span());
  const float ground = params.flip ? bounds.max[up] : bounds.min[up];
  const float height = bounds.max[up] - bounds.min[up];
  if (height <= 0.0f) {
    return;
  }
  const float3 center = math::midpoint(bounds.min, bounds.max);
  const float amount = std::clamp(params.amount, 0.0f, 1.0f);
  /* Higher solidity keeps the top up longer: the fall is eased toward the end. */
  const float exponent = 1.0f + std::clamp(params.solidity, 0.0f, 1.0f) * 3.0f;

  threading::parallel_for(positions.index_range(), 4096, [&](const IndexRange range) {
    for (const int i : range) {
      const float w = weight_at(weights, i);
      if (w == 0.0f) {
        continue;
      }
      float3 &p = positions[i];
      const float h = (p[up] - ground) * sign / height;
      if (h <= 0.0f) {
        continue;
      }
      /* Points higher up fall further; the top falls with the given exponent. */
      const float fall = amount * w * (1.0f - std::pow(1.0f - std::min(h, 1.0f), exponent));
      const float new_h = std::max(h * (1.0f - fall), 0.0f);
      p[up] = ground + new_h * height * sign;
      const float drop = h - new_h;
      const float grow = 1.0f + params.spread * drop * (1.0f - new_h);
      p[side_a] = center[side_a] + (p[side_a] - center[side_a]) * grow;
      p[side_b] = center[side_b] + (p[side_b] - center[side_b]) * grow;
    }
  });
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name FFD
 * \{ */

static float bernstein(const int n, const int i, const float t)
{
  /* Binomial coefficients for degree up to 3. */
  static const float binomial[4][4] = {
      {1, 0, 0, 0}, {1, 1, 0, 0}, {1, 2, 1, 0}, {1, 3, 3, 1}};
  return binomial[n][i] * std::pow(t, float(i)) * std::pow(1.0f - t, float(n - i));
}

float3 ffd_rest_point(const FFDParams &params, const int index)
{
  const int res = std::clamp(params.resolution, 2, 4);
  const int x = index % res;
  const int y = (index / res) % res;
  const int z = index / (res * res);
  const float3 t(float(x) / (res - 1), float(y) / (res - 1), float(z) / (res - 1));
  return params.bounds_min + (params.bounds_max - params.bounds_min) * t;
}

void ffd_deform(MutableSpan<float3> positions, const Span<float> weights, const FFDParams &params)
{
  const int res = std::clamp(params.resolution, 2, 4);
  const int count = res * res * res;
  if (params.offsets.size() < count) {
    return;
  }
  bool any = false;
  for (const int i : IndexRange(count)) {
    if (!math::is_zero(params.offsets[i])) {
      any = true;
      break;
    }
  }
  if (!any) {
    return;
  }
  const float3 size = params.bounds_max - params.bounds_min;
  const float3 inv_size(size.x != 0.0f ? 1.0f / size.x : 0.0f,
                        size.y != 0.0f ? 1.0f / size.y : 0.0f,
                        size.z != 0.0f ? 1.0f / size.z : 0.0f);
  const int degree = res - 1;

  threading::parallel_for(positions.index_range(), 1024, [&](const IndexRange range) {
    for (const int i : range) {
      const float w = weight_at(weights, i);
      if (w == 0.0f) {
        continue;
      }
      const float3 t = (positions[i] - params.bounds_min) * inv_size;
      if (params.inside_only &&
          (t.x < 0.0f || t.y < 0.0f || t.z < 0.0f || t.x > 1.0f || t.y > 1.0f || t.z > 1.0f))
      {
        continue;
      }
      /* Bernstein weights are exact for the rest lattice, so only the offsets are added. */
      const float3 tc = math::clamp(t, float3(0.0f), float3(1.0f));
      float bx[4], by[4], bz[4];
      for (int k = 0; k < res; k++) {
        bx[k] = bernstein(degree, k, tc.x);
        by[k] = bernstein(degree, k, tc.y);
        bz[k] = bernstein(degree, k, tc.z);
      }
      float3 offset(0.0f);
      for (int z = 0; z < res; z++) {
        for (int y = 0; y < res; y++) {
          const float byz = by[y] * bz[z];
          for (int x = 0; x < res; x++) {
            offset += params.offsets[(z * res + y) * res + x] * (bx[x] * byz);
          }
        }
      }
      positions[i] += offset * w;
    }
  });
}

/** \} */

}  // namespace blender::geometry
