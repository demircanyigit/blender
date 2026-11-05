/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <algorithm>

#include "BLI_array.hh"
#include "BLI_math_vector.hh"
#include "BLI_task.hh"

#include "BKE_attribute.hh"

#include "GEO_curve_modifiers.hh"
#include "GEO_fillet_curves.hh"
#include "GEO_interpolate_curves.hh"
#include "GEO_resample_curves.hh"
#include "GEO_smooth_curves.hh"

namespace blender::geometry {

bke::CurvesGeometry normalize_curves(const bke::CurvesGeometry &curves,
                                     const NormalizeCurvesMode mode,
                                     const float length,
                                     const int count)
{
  const IndexMask all(curves.curves_num());
  if (mode == NormalizeCurvesMode::Count) {
    return resample_to_count(curves, all, VArray<int>::from_single(std::max(count, 1), curves.curves_num()));
  }
  return resample_to_length(
      curves, all, VArray<float>::from_single(std::max(length, 1e-4f), curves.curves_num()));
}

void relax_curves(bke::CurvesGeometry &curves,
                  const int iterations,
                  const float factor,
                  const bool keep_ends,
                  const bool keep_shape)
{
  if (iterations <= 0 || factor <= 0.0f) {
    return;
  }
  smooth_curve_positions(curves,
                         IndexMask(curves.curves_num()),
                         VArray<bool>::from_single(true, curves.points_num()),
                         iterations,
                         std::min(factor, 1.0f),
                         !keep_ends,
                         keep_shape);
}

bke::CurvesGeometry chamfer_curves(const bke::CurvesGeometry &curves,
                                   const float radius,
                                   const int segments,
                                   const bool limit_radius)
{
  return fillet_curves_poly(curves,
                            IndexMask(curves.curves_num()),
                            VArray<float>::from_single(std::max(radius, 0.0f), curves.points_num()),
                            VArray<int>::from_single(std::max(segments, 0), curves.points_num()),
                            limit_radius,
                            bke::AttributeFilter::default_filter());
}

void morph_curves(bke::CurvesGeometry &curves,
                  const bke::CurvesGeometry &target,
                  const float factor)
{
  const int curves_num = std::min(curves.curves_num(), target.curves_num());
  if (curves_num == 0 || factor == 0.0f) {
    return;
  }
  const OffsetIndices points_by_curve = curves.points_by_curve();
  const OffsetIndices target_points_by_curve = target.points_by_curve();
  const Span<float3> target_positions = target.positions();
  const VArray<bool> target_cyclic = target.cyclic();
  MutableSpan<float3> positions = curves.positions_for_write();

  threading::parallel_for(IndexRange(curves_num), 64, [&](const IndexRange range) {
    Array<int> indices;
    Array<float> factors;
    for (const int curve : range) {
      const IndexRange points = points_by_curve[curve];
      const IndexRange target_points = target_points_by_curve[curve];
      if (points.is_empty() || target_points.is_empty()) {
        continue;
      }
      indices.reinitialize(points.size());
      factors.reinitialize(points.size());
      const Span<float3> src = target_positions.slice(target_points);
      sample_curve_padded(src, target_cyclic[curve], indices, factors);
      for (const int i : points.index_range()) {
        const int a = indices[i];
        const int b = std::min(a + 1, int(src.size()) - 1);
        const int b_wrapped = (a + 1 < src.size()) ? a + 1 : (target_cyclic[curve] ? 0 : b);
        const float3 sample = math::interpolate(src[a], src[b_wrapped], factors[i]);
        positions[points[i]] = math::interpolate(positions[points[i]], sample, factor);
      }
    }
  });
  curves.tag_positions_changed();
}

}  // namespace blender::geometry
