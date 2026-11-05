/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <algorithm>
#include <cfloat>

#include "BLI_array.hh"
#include "BLI_atomic_disjoint_set.hh"
#include "BLI_bounds.hh"
#include "BLI_function_ref.hh"
#include "BLI_hash.h"
#include "BLI_math_vector.hh"
#include "BLI_task.hh"

#include "DNA_mesh_types.h"

#include "BKE_mesh.hh"

#include "GEO_mesh_channels.hh"

namespace blender::geometry {

static float random_01(const uint32_t a, const uint32_t b)
{
  return float(BLI_hash_int_2d(a, b)) / float(0xFFFFFFFFu);
}

/* -------------------------------------------------------------------- */
/** \name Material By Element
 * \{ */

void material_by_element(const Mesh &mesh,
                         const MaterialByElementParams &params,
                         MutableSpan<int> r_face_material_indices)
{
  const int min_index = std::max(std::min(params.min_index, params.max_index), 0);
  const int max_index = std::max(std::max(params.min_index, params.max_index), 0);
  const int index_count = max_index - min_index + 1;
  const uint32_t seed = uint32_t(params.seed);
  const auto random_index = [&](const uint32_t key) {
    return min_index + std::min(int(random_01(key, seed) * index_count), index_count - 1);
  };

  const OffsetIndices faces = mesh.faces();
  const Span<int> corner_verts = mesh.corner_verts();

  switch (params.mode) {
    case MaterialByElementMode::Random: {
      threading::parallel_for(faces.index_range(), 4096, [&](const IndexRange range) {
        for (const int face : range) {
          r_face_material_indices[face] = random_index(uint32_t(face));
        }
      });
      break;
    }
    case MaterialByElementMode::Islands: {
      AtomicDisjointSet islands(mesh.verts_num);
      for (const int face : faces.index_range()) {
        const Span<int> verts = corner_verts.slice(faces[face]);
        for (const int i : verts.index_range().drop_front(1)) {
          islands.join(verts[0], verts[i]);
        }
      }
      threading::parallel_for(faces.index_range(), 4096, [&](const IndexRange range) {
        for (const int face : range) {
          const int root = islands.find_root(corner_verts[faces[face].first()]);
          r_face_material_indices[face] = random_index(uint32_t(root));
        }
      });
      break;
    }
    case MaterialByElementMode::Cycle: {
      for (const int face : faces.index_range()) {
        r_face_material_indices[face] = min_index + face % index_count;
      }
      break;
    }
    case MaterialByElementMode::Direction: {
      const Span<float3> normals = mesh.face_normals();
      threading::parallel_for(faces.index_range(), 4096, [&](const IndexRange range) {
        for (const int face : range) {
          const float3 &n = normals[face];
          const float3 a = math::abs(n);
          int direction;
          if (a.x >= a.y && a.x >= a.z) {
            direction = n.x > 0.0f ? 0 : 1;
          }
          else if (a.y >= a.z) {
            direction = n.y > 0.0f ? 2 : 3;
          }
          else {
            direction = n.z > 0.0f ? 4 : 5;
          }
          r_face_material_indices[face] = min_index + direction % index_count;
        }
      });
      break;
    }
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Tension
 * \{ */

void mesh_tension(const Mesh &mesh,
                  const Span<float3> rest_positions,
                  const float strength,
                  MutableSpan<float> r_tension)
{
  const Span<float3> positions = mesh.vert_positions();
  const Span<int2> edges = mesh.edges();
  Array<float> sums(mesh.verts_num, 0.0f);
  Array<int> counts(mesh.verts_num, 0);
  for (const int2 &edge : edges) {
    const float rest = math::distance(rest_positions[edge[0]], rest_positions[edge[1]]);
    if (rest < 1e-8f) {
      continue;
    }
    const float current = math::distance(positions[edge[0]], positions[edge[1]]);
    const float change = current / rest - 1.0f;
    sums[edge[0]] += change;
    sums[edge[1]] += change;
    counts[edge[0]]++;
    counts[edge[1]]++;
  }
  threading::parallel_for(IndexRange(mesh.verts_num), 4096, [&](const IndexRange range) {
    for (const int vert : range) {
      r_tension[vert] = counts[vert] ? sums[vert] / counts[vert] * strength : 0.0f;
    }
  });
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Data Channel
 * \{ */

static void average_over_edges(const Mesh &mesh,
                               const FunctionRef<float(int vert, int other)> fn,
                               MutableSpan<float> r_values)
{
  Array<int> counts(mesh.verts_num, 0);
  r_values.fill(0.0f);
  for (const int2 &edge : mesh.edges()) {
    r_values[edge[0]] += fn(edge[0], edge[1]);
    r_values[edge[1]] += fn(edge[1], edge[0]);
    counts[edge[0]]++;
    counts[edge[1]]++;
  }
  for (const int vert : r_values.index_range()) {
    if (counts[vert]) {
      r_values[vert] /= counts[vert];
    }
  }
}

void data_channel(const Mesh &mesh, const DataChannelParams &params, MutableSpan<float> r_values)
{
  const Span<float3> positions = mesh.vert_positions();
  switch (params.source) {
    case DataChannelSource::PositionX:
    case DataChannelSource::PositionY:
    case DataChannelSource::PositionZ: {
      const int axis = int(params.source) - int(DataChannelSource::PositionX);
      for (const int i : positions.index_range()) {
        r_values[i] = positions[i][axis];
      }
      break;
    }
    case DataChannelSource::NormalX:
    case DataChannelSource::NormalY:
    case DataChannelSource::NormalZ: {
      const int axis = int(params.source) - int(DataChannelSource::NormalX);
      const Span<float3> normals = mesh.vert_normals();
      for (const int i : normals.index_range()) {
        r_values[i] = normals[i][axis];
      }
      break;
    }
    case DataChannelSource::Convexity: {
      const Span<float3> normals = mesh.vert_normals();
      average_over_edges(
          mesh,
          [&](const int vert, const int other) {
            const float3 dir = math::normalize(positions[other] - positions[vert]);
            return -math::dot(normals[vert], dir);
          },
          r_values);
      break;
    }
    case DataChannelSource::EdgeLength: {
      average_over_edges(
          mesh,
          [&](const int vert, const int other) {
            return math::distance(positions[vert], positions[other]);
          },
          r_values);
      break;
    }
    case DataChannelSource::FaceArea: {
      const OffsetIndices faces = mesh.faces();
      const Span<int> corner_verts = mesh.corner_verts();
      Array<int> counts(mesh.verts_num, 0);
      r_values.fill(0.0f);
      for (const int face : faces.index_range()) {
        const Span<int> verts = corner_verts.slice(faces[face]);
        const float area = bke::mesh::face_area_calc(positions, verts);
        for (const int vert : verts) {
          r_values[vert] += area;
          counts[vert]++;
        }
      }
      for (const int vert : r_values.index_range()) {
        if (counts[vert]) {
          r_values[vert] /= counts[vert];
        }
      }
      break;
    }
    case DataChannelSource::Random: {
      for (const int i : r_values.index_range()) {
        r_values[i] = random_01(uint32_t(i), uint32_t(params.seed));
      }
      break;
    }
    case DataChannelSource::DistanceFromCenter: {
      float3 center(0.0f);
      if (const std::optional<Bounds<float3>> bounds = mesh.bounds_min_max()) {
        center = bounds->center();
      }
      for (const int i : positions.index_range()) {
        r_values[i] = math::distance(positions[i], center);
      }
      break;
    }
  }

  if (params.normalize && !r_values.is_empty()) {
    float min = FLT_MAX;
    float max = -FLT_MAX;
    for (const float value : r_values) {
      min = std::min(min, value);
      max = std::max(max, value);
    }
    const float range = max - min;
    for (float &value : r_values) {
      value = range > 1e-12f ? (value - min) / range : 0.0f;
    }
  }
  for (float &value : r_values) {
    if (params.invert) {
      value = params.normalize ? 1.0f - value : -value;
    }
    if (params.power != 1.0f) {
      value = std::copysign(std::pow(std::abs(value), params.power), value);
    }
    value = value * params.multiply + params.add;
    if (params.clamp) {
      value = std::clamp(value, 0.0f, 1.0f);
    }
  }
}

/** \} */

}  // namespace blender::geometry
