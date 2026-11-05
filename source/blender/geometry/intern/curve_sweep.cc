/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <array>
#include <cfloat>

#include "DNA_mesh_types.h"

#include "BLI_math_rotation.h"
#include "BLI_math_vector.hh"
#include "BLI_task.hh"

#include "BKE_attribute.hh"
#include "BKE_curve_to_mesh.hh"
#include "BKE_curves.hh"
#include "BKE_mesh.hh"

#include "GEO_curve_sweep.hh"

namespace blender::geometry {

static bke::CurvesGeometry make_poly_profile(const int points_num, const bool cyclic)
{
  bke::CurvesGeometry profile(points_num, 1);
  MutableSpan<int> offsets = profile.offsets_for_write();
  offsets.first() = 0;
  offsets.last() = points_num;
  profile.fill_curve_types(CURVE_TYPE_POLY);
  profile.cyclic_for_write().first() = cyclic;
  return profile;
}

static bke::CurvesGeometry make_round_tube(const int columns, const float radius)
{
  const int points_num = std::max(columns, 3);
  bke::CurvesGeometry profile = make_poly_profile(points_num, true);
  MutableSpan<float3> positions = profile.positions_for_write();
  for (const int i : positions.index_range()) {
    const float angle = float(M_PI) * 2.0f * float(i) / float(points_num);
    positions[i] = float3(std::cos(angle), std::sin(angle), 0.0f) * radius;
  }
  return profile;
}

static bke::CurvesGeometry make_square_tube(const int columns, const float width)
{
  const int side_segments = std::max((columns + 2) / 4, 1);
  bke::CurvesGeometry profile = make_poly_profile(side_segments * 4, true);
  MutableSpan<float3> positions = profile.positions_for_write();
  const float half = width / 2.0f;
  const std::array<float2, 5> corners = {
      float2(-half, -half), float2(half, -half), float2(half, half), float2(-half, half),
      float2(-half, -half)};
  for (const int side : IndexRange(4)) {
    for (const int i : IndexRange(side_segments)) {
      const float2 position = math::interpolate(
          corners[side], corners[side + 1], float(i) / float(side_segments));
      positions[side * side_segments + i] = float3(position, 0.0f);
    }
  }

  /* Only the corners are sharp, so that the sides can stay smooth along a bent curve. */
  bke::SpanAttributeWriter<bool> sharp_edges =
      profile.attributes_for_write().lookup_or_add_for_write_span<bool>("sharp_edge",
                                                                        bke::AttrDomain::Point);
  for (const int side : IndexRange(4)) {
    sharp_edges.span[side * side_segments] = true;
  }
  sharp_edges.finish();
  return profile;
}

static bke::CurvesGeometry make_ribbon(const int columns, const float width)
{
  const int segments = std::max(columns, 1);
  bke::CurvesGeometry profile = make_poly_profile(segments + 1, false);
  MutableSpan<float3> positions = profile.positions_for_write();
  for (const int i : positions.index_range()) {
    positions[i] = float3((float(i) / float(segments) - 0.5f) * width, 0.0f, 0.0f);
  }
  return profile;
}

void sweep_add_roll_and_twist(bke::CurvesGeometry &curves,
                              const VArray<float> &roll,
                              const VArray<float> &twist)
{
  if (roll.get_if_single() == 0.0f && twist.get_if_single() == 0.0f) {
    return;
  }
  const OffsetIndices<int> points_by_curve = curves.points_by_curve();
  const Span<float3> positions = curves.positions();
  const VArray<bool> cyclic = curves.cyclic();
  MutableSpan<float> tilts = curves.tilt_for_write();
  threading::parallel_for(curves.curves_range(), 256, [&](const IndexRange range) {
    Vector<float> lengths;
    for (const int curve : range) {
      const IndexRange points = points_by_curve[curve];
      lengths.resize(points.size());
      float total = 0.0f;
      for (const int i : points.index_range()) {
        lengths[i] = total;
        const int next = (i + 1 < points.size()) ? i + 1 : (cyclic[curve] ? 0 : i);
        total += math::distance(positions[points[i]], positions[points[next]]);
      }
      for (const int i : points.index_range()) {
        const int point = points[i];
        const float factor = total > 0.0f ? lengths[i] / total : 0.0f;
        tilts[point] += roll[point] + twist[point] * factor;
      }
    }
  });
  curves.tag_normals_changed();
}

/** How far along the points each segment starts, plus the end of the last one. */
static Array<float> calc_accumulated_lengths(const Span<float3> positions, const int segments_num)
{
  Array<float> lengths(segments_num + 1);
  lengths.first() = 0.0f;
  for (const int i : IndexRange(segments_num)) {
    lengths[i + 1] = lengths[i] + math::distance(positions[i],
                                                 positions[(i + 1) % positions.size()]);
  }
  return lengths;
}

/** The number of quads along a curve, the same as #bke::curve_to_mesh_sweep makes them. */
static int main_segments_num(const int points_num, const bool cyclic)
{
  if (points_num == 0) {
    return 0;
  }
  return bke::curves::segments_num(points_num, points_num > 2 && cyclic);
}

/**
 * Fill the attributes that need to know which corner came from which point of the two curves. This
 * follows the order of the corners of #bke::curve_to_mesh_sweep.
 */
static void fill_sweep_attributes(const bke::CurvesGeometry &main,
                                  const bke::CurvesGeometry &profile,
                                  const bool fill_caps,
                                  const bool length_based,
                                  const std::optional<std::string> &uv_id,
                                  const std::optional<std::string> &caps_id,
                                  Mesh &mesh)
{
  if (!uv_id && !caps_id) {
    return;
  }
  bke::MutableAttributeAccessor attributes = mesh.attributes_for_write();
  bke::SpanAttributeWriter<float2> uvs;
  bke::SpanAttributeWriter<bool> caps;
  if (uv_id) {
    uvs = attributes.lookup_or_add_for_write_only_span<float2>(*uv_id, bke::AttrDomain::Corner);
  }
  if (caps_id) {
    caps = attributes.lookup_or_add_for_write_span<bool>(*caps_id, bke::AttrDomain::Face);
  }

  const OffsetIndices<int> main_points_by_curve = main.evaluated_points_by_curve();
  const OffsetIndices<int> profile_points_by_curve = profile.evaluated_points_by_curve();
  const Span<float3> main_positions = main.evaluated_positions();
  const Span<float3> profile_positions = profile.evaluated_positions();
  const VArraySpan<bool> main_cyclic = main.cyclic();
  const VArraySpan<bool> profile_cyclic = profile.cyclic();

  int corner_offset = 0;
  int face_offset = 0;
  for (const int i_main : main_points_by_curve.index_range()) {
    const IndexRange main_points = main_points_by_curve[i_main];
    const int main_segments = main_segments_num(main_points.size(), main_cyclic[i_main]);
    const Array<float> main_lengths = calc_accumulated_lengths(main_positions.slice(main_points),
                                                               main_segments);
    for (const int i_profile : profile_points_by_curve.index_range()) {
      const IndexRange profile_points = profile_points_by_curve[i_profile];
      if (profile_points.is_empty()) {
        continue;
      }
      const Span<float3> positions = profile_positions.slice(profile_points);
      const bool cyclic = profile_cyclic[i_profile];
      const int profile_segments = bke::curves::segments_num(profile_points.size(), cyclic);
      const Array<float> profile_lengths = calc_accumulated_lengths(positions, profile_segments);
      const int tube_faces = main_segments * profile_segments;
      const bool has_caps = fill_caps && !main_cyclic[i_main] && cyclic && profile_segments > 2;

      if (uvs) {
        const float profile_total = profile_lengths.last();
        const float main_total = main_lengths.last();
        const float u_scale = profile_total > 0.0f ? 1.0f / profile_total : 0.0f;
        const float v_divisor = (length_based && profile_total > 0.0f) ? profile_total :
                                                                         main_total;
        const float v_scale = v_divisor > 0.0f ? 1.0f / v_divisor : 0.0f;
        for (const int ring : IndexRange(main_segments)) {
          const float v = main_lengths[ring] * v_scale;
          const float v_next = main_lengths[ring + 1] * v_scale;
          for (const int i : IndexRange(profile_segments)) {
            const float u = profile_lengths[i] * u_scale;
            const float u_next = profile_lengths[i + 1] * u_scale;
            MutableSpan<float2> quad = uvs.span.slice(
                corner_offset + (ring * profile_segments + i) * 4, 4);
            quad[0] = float2(u, v);
            quad[1] = float2(u_next, v);
            quad[2] = float2(u_next, v_next);
            quad[3] = float2(u, v_next);
          }
        }
        if (has_caps) {
          /* Fit the shape into the UV square. */
          float3 min(FLT_MAX), max(-FLT_MAX);
          for (const float3 &position : positions) {
            math::min_max(position, min, max);
          }
          const float size = std::max(max.x - min.x, max.y - min.y);
          const float scale = size > 0.0f ? 1.0f / size : 0.0f;
          const int cap_start = corner_offset + tube_faces * 4;
          for (const int i : IndexRange(profile_segments)) {
            const float2 first = (positions[profile_segments - i - 1].xy() - min.xy()) * scale;
            const float2 last = (positions[i].xy() - min.xy()) * scale;
            uvs.span[cap_start + i] = first;
            uvs.span[cap_start + profile_segments + i] = last;
          }
        }
      }
      if (caps && has_caps) {
        caps.span[face_offset + tube_faces] = true;
        caps.span[face_offset + tube_faces + 1] = true;
      }

      corner_offset += tube_faces * 4 + (has_caps ? profile_segments * 2 : 0);
      face_offset += tube_faces + (has_caps ? 2 : 0);
    }
  }

  uvs.finish();
  caps.finish();
}

bke::CurvesGeometry sweep_shape_profile(const SweepShape shape,
                                        const int columns,
                                        const float radius,
                                        const float width)
{
  switch (shape) {
    case SweepShape::SquareTube:
      return make_square_tube(columns, width);
    case SweepShape::Ribbon:
      return make_ribbon(columns, width);
    case SweepShape::RoundTube:
    case SweepShape::CrossSection:
      break;
  }
  return make_round_tube(columns, radius);
}

Mesh *curve_sweep(const bke::CurvesGeometry &main,
                  const bke::CurvesGeometry &profile,
                  const VArray<float> &scales,
                  const bool fill_caps,
                  const bool length_based_uvs,
                  const SweepOutputs &outputs,
                  const bke::AttributeFilter &attribute_filter)
{
  Mesh *mesh = bke::curve_to_mesh_sweep(main, profile, scales, fill_caps, attribute_filter);
  if (mesh != nullptr) {
    fill_sweep_attributes(
        main, profile, fill_caps, length_based_uvs, outputs.uv_map, outputs.caps, *mesh);
  }
  return mesh;
}

}  // namespace blender::geometry
