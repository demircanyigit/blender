/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <algorithm>

#include "BLI_array.hh"
#include "BLI_math_base.hh"
#include "BLI_math_constants.h"
#include "BLI_math_euler.hh"
#include "BLI_math_matrix.hh"
#include "BLI_math_vector.hh"
#include "BLI_task.hh"

#include "GEO_uvw_map.hh"

namespace blender::geometry {

float4x4 uvw_map_gizmo_matrix(const UVWMapParams &params)
{
  float3 size = params.size;
  for (int i = 0; i < 3; i++) {
    if (std::abs(size[i]) < 1e-6f) {
      size[i] = 1e-6f;
    }
  }
  return math::from_loc_rot_scale<float4x4>(
      params.center, math::EulerXYZ(params.rotation), size);
}

static float angle_u(const float3 &p)
{
  return std::atan2(p.y, p.x) / (2.0f * float(M_PI)) + 0.5f;
}

/** Keep the faces that cross the seam of a wrapped projection from spanning the whole map. */
static void fix_seam(MutableSpan<float2> face_uvs)
{
  float min_u = FLT_MAX;
  float max_u = -FLT_MAX;
  for (const float2 &uv : face_uvs) {
    min_u = std::min(min_u, uv.x);
    max_u = std::max(max_u, uv.x);
  }
  if (max_u - min_u > 0.5f) {
    for (float2 &uv : face_uvs) {
      if (uv.x < 0.5f) {
        uv.x += 1.0f;
      }
    }
  }
}

static float3 face_normal_local(const Span<float3> local_positions, const Span<int> verts)
{
  float3 normal(0.0f);
  /* Newell's method, which works for any polygon. */
  for (const int i : verts.index_range()) {
    const float3 &a = local_positions[verts[i]];
    const float3 &b = local_positions[verts[(i + 1) % verts.size()]];
    normal.x += (a.y - b.y) * (a.z + b.z);
    normal.y += (a.z - b.z) * (a.x + b.x);
    normal.z += (a.x - b.x) * (a.y + b.y);
  }
  return math::normalize(normal);
}

static void face_uvs_own(const Span<float3> local_positions,
                         const Span<int> verts,
                         MutableSpan<float2> r_uvs)
{
  if (verts.size() == 4) {
    r_uvs[0] = {0.0f, 0.0f};
    r_uvs[1] = {1.0f, 0.0f};
    r_uvs[2] = {1.0f, 1.0f};
    r_uvs[3] = {0.0f, 1.0f};
    return;
  }
  /* Project onto the plane of the face and fit to the unit square. */
  const float3 normal = face_normal_local(local_positions, verts);
  const float3 &p0 = local_positions[verts[0]];
  const float3 &p1 = local_positions[verts[1 % verts.size()]];
  float3 axis_u = p1 - p0;
  if (math::is_zero(axis_u)) {
    axis_u = math::abs(normal.x) < 0.9f ? float3(1, 0, 0) : float3(0, 1, 0);
  }
  axis_u = math::normalize(axis_u - normal * math::dot(axis_u, normal));
  const float3 axis_v = math::cross(normal, axis_u);
  float2 min(FLT_MAX);
  float2 max(-FLT_MAX);
  for (const int i : verts.index_range()) {
    const float3 d = local_positions[verts[i]] - p0;
    r_uvs[i] = {math::dot(d, axis_u), math::dot(d, axis_v)};
    min = math::min(min, r_uvs[i]);
    max = math::max(max, r_uvs[i]);
  }
  const float2 extent = math::max(max - min, float2(1e-6f));
  for (float2 &uv : r_uvs) {
    uv = (uv - min) / extent;
  }
}

void uvw_map(const Span<float3> positions,
             const OffsetIndices<int> faces,
             const Span<int> corner_verts,
             const UVWMapParams &params,
             MutableSpan<float2> r_uvs)
{
  const float4x4 to_gizmo = math::invert(uvw_map_gizmo_matrix(params));
  Array<float3> local_positions(positions.size());
  threading::parallel_for(positions.index_range(), 4096, [&](const IndexRange range) {
    for (const int i : range) {
      local_positions[i] = math::transform_point(to_gizmo, positions[i]);
    }
  });

  threading::parallel_for(faces.index_range(), 1024, [&](const IndexRange range) {
    for (const int face : range) {
      const IndexRange face_corners = faces[face];
      const Span<int> verts = corner_verts.slice(face_corners);
      MutableSpan<float2> face_uvs = r_uvs.slice(face_corners);
      switch (params.type) {
        case UVWMapType::Planar: {
          for (const int i : verts.index_range()) {
            const float3 &p = local_positions[verts[i]];
            face_uvs[i] = {p.x + 0.5f, p.y + 0.5f};
          }
          break;
        }
        case UVWMapType::Cylindrical: {
          const float3 normal = face_normal_local(local_positions, verts);
          if (params.cap && std::abs(normal.z) > 0.7071f) {
            for (const int i : verts.index_range()) {
              const float3 &p = local_positions[verts[i]];
              face_uvs[i] = {p.x + 0.5f, p.y + 0.5f};
            }
            break;
          }
          for (const int i : verts.index_range()) {
            const float3 &p = local_positions[verts[i]];
            face_uvs[i] = {angle_u(p), p.z + 0.5f};
          }
          fix_seam(face_uvs);
          break;
        }
        case UVWMapType::Spherical: {
          for (const int i : verts.index_range()) {
            const float3 &p = local_positions[verts[i]];
            const float v = std::atan2(p.z, math::length(float2(p.x, p.y))) / float(M_PI) + 0.5f;
            face_uvs[i] = {angle_u(p), v};
          }
          fix_seam(face_uvs);
          break;
        }
        case UVWMapType::Box: {
          const float3 normal = face_normal_local(local_positions, verts);
          const float3 a = math::abs(normal);
          for (const int i : verts.index_range()) {
            const float3 &p = local_positions[verts[i]];
            float2 uv;
            if (a.x >= a.y && a.x >= a.z) {
              uv = {normal.x > 0.0f ? p.y : -p.y, p.z};
            }
            else if (a.y >= a.z) {
              uv = {normal.y > 0.0f ? -p.x : p.x, p.z};
            }
            else {
              uv = {normal.z > 0.0f ? p.x : -p.x, p.y};
            }
            face_uvs[i] = uv + float2(0.5f);
          }
          break;
        }
        case UVWMapType::Face: {
          face_uvs_own(local_positions, verts, face_uvs);
          break;
        }
      }
      for (float2 &uv : face_uvs) {
        if (params.flip_u) {
          uv.x = 1.0f - uv.x;
        }
        if (params.flip_v) {
          uv.y = 1.0f - uv.y;
        }
        uv = uv * params.tile + params.offset;
      }
    }
  });
}

}  // namespace blender::geometry
