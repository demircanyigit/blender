/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <numeric>

#include "BLI_array.hh"
#include "BLI_index_mask.hh"
#include "BLI_math_vector.h"
#include "BLI_math_vector.hh"
#include "BLI_task.hh"
#include "BLI_vector.hh"

#include "DNA_mesh_types.h"

#include "BKE_mesh.hh"

#include "GEO_uv_pack.hh"
#include "GEO_uv_parametrizer.hh"
#include "GEO_uv_smart_project.hh"

namespace blender::geometry {

/** Pick projection directions: every face whose normal is too far from all directions so far
 * adds its own, largest faces first. */
static Vector<float3> pick_directions(const Span<int> faces,
                                      const Span<float3> normals,
                                      const Span<float> areas,
                                      const float cos_limit)
{
  Array<int> order(faces.size());
  std::iota(order.begin(), order.end(), 0);
  std::stable_sort(order.begin(), order.end(), [&](const int a, const int b) {
    return areas[faces[a]] > areas[faces[b]];
  });
  Vector<float3> directions;
  for (const int i : order) {
    const float3 &n = normals[faces[i]];
    if (math::is_zero(n)) {
      continue;
    }
    bool covered = false;
    for (const float3 &d : directions) {
      if (math::dot(d, n) >= cos_limit) {
        covered = true;
        break;
      }
    }
    if (!covered) {
      directions.append(n);
    }
  }
  if (directions.is_empty()) {
    directions.append(float3(0.0f, 0.0f, 1.0f));
  }
  return directions;
}

static int closest_direction(const Span<float3> directions, const float3 &n)
{
  int best = 0;
  float best_dot = -FLT_MAX;
  for (const int i : directions.index_range()) {
    const float d = math::dot(directions[i], n);
    if (d > best_dot) {
      best_dot = d;
      best = i;
    }
  }
  return best;
}

void smart_uv_project(const Mesh &mesh,
                      const IndexMask &selection,
                      const SmartUVProjectParams &params,
                      MutableSpan<float2> r_uvs)
{
  if (selection.is_empty()) {
    return;
  }
  const Span<float3> positions = mesh.vert_positions();
  const OffsetIndices faces = mesh.faces();
  const Span<int> corner_verts = mesh.corner_verts();
  const Span<float3> normals = mesh.face_normals();

  Array<int> selected(selection.size());
  selection.to_indices(selected.as_mutable_span());
  Array<float> areas(faces.size(), 0.0f);
  selection.foreach_index(GrainSize(1024), [&](const int face) {
    areas[face] = bke::mesh::face_area_calc(positions, corner_verts.slice(faces[face]));
  });

  const float cos_limit = std::cos(std::clamp(params.angle_limit, 0.0f, 1.5707963f));
  Vector<float3> directions = pick_directions(selected, normals, areas, cos_limit);

  /* One pass to center each direction on the faces it got. */
  Array<int> face_direction(faces.size(), 0);
  {
    Array<float3> sums(directions.size(), float3(0.0f));
    for (const int face : selected) {
      const int d = closest_direction(directions, normals[face]);
      sums[d] += normals[face] * std::max(areas[face], 1e-12f);
    }
    for (const int d : directions.index_range()) {
      const float3 n = math::normalize(sums[d]);
      if (!math::is_zero(n)) {
        directions[d] = n;
      }
    }
  }
  for (const int face : selected) {
    face_direction[face] = closest_direction(directions, normals[face]);
  }

  /* A flat projection for each direction. */
  Array<float3> axis_u(directions.size());
  Array<float3> axis_v(directions.size());
  for (const int d : directions.index_range()) {
    ortho_basis_v3v3_v3(axis_u[d], axis_v[d], directions[d]);
  }
  for (const int face : selected) {
    const int d = face_direction[face];
    for (const int corner : faces[face]) {
      const float3 &p = positions[corner_verts[corner]];
      r_uvs[corner] = float2(math::dot(p, axis_u[d]), math::dot(p, axis_v[d]));
    }
  }

  if (!params.pack) {
    return;
  }

  /* Islands come from where the UVs match, so faces of different projections come apart. */
  ParamHandle *handle = new ParamHandle();
  Array<float2> uvs(mesh.corners_num);
  for (const int face_index : selected) {
    const IndexRange face = faces[face_index];
    Array<ParamKey, 16> vkeys(face.size());
    Array<bool, 16> pin(face.size(), false);
    Array<bool, 16> select(face.size(), false);
    Array<const float *, 16> co(face.size());
    Array<float *, 16> uv(face.size());
    for (const int i : IndexRange(face.size())) {
      const int corner = face[i];
      vkeys[i] = corner_verts[corner];
      co[i] = positions[corner_verts[corner]];
      uvs[corner] = r_uvs[corner];
      uv[i] = uvs[corner];
    }
    uv_parametrizer_face_add(handle,
                             face_index,
                             face.size(),
                             vkeys.data(),
                             co.data(),
                             uv.data(),
                             nullptr,
                             pin.data(),
                             select.data());
  }
  uv_parametrizer_construct_end(handle, true, true, nullptr);
  UVPackIsland_Params pack_params;
  pack_params.rotate_method = params.rotate ? ED_UVPACK_ROTATION_ANY : ED_UVPACK_ROTATION_NONE;
  pack_params.margin = params.margin;
  uv_parametrizer_pack(handle, pack_params);
  uv_parametrizer_flush(handle);
  delete handle;

  for (const int face : selected) {
    for (const int corner : faces[face]) {
      r_uvs[corner] = uvs[corner];
    }
  }
}

}  // namespace blender::geometry
