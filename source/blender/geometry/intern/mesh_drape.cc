/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup geo
 *
 * Drape: the mesh falls as cloth until it rests on colliders, and is returned in that shape.
 *
 * A position based solver with small steps: every step moves the points with their speed and
 * gravity, then pulls the edges back to their length, then pushes the points out of what they
 * touch. Stretching is held by the mesh edges and the diagonals of its faces, folding by the
 * distance between the far corners of neighboring triangles.
 */

#include <algorithm>
#include <cmath>

#include "DNA_mesh_types.h"

#include "BLI_array.hh"
#include "BLI_kdopbvh.hh"
#include "BLI_map.hh"
#include "BLI_math_matrix.hh"
#include "BLI_math_vector.hh"
#include "BLI_offset_indices.hh"
#include "BLI_ordered_edge.hh"
#include "BLI_task.hh"
#include "BLI_vector.hh"

#include "BKE_bvhutils.hh"
#include "BKE_lib_id.hh"
#include "BKE_mesh.hh"

#include "GEO_mesh_drape.hh"

namespace blender::geometry {

namespace {

struct DistanceConstraint {
  int a;
  int b;
  float rest;
};

/** The far corners of the (up to) two triangles on each side of a triangle edge. */
struct EdgeSides {
  int first = -1;
  int second = -1;
};

void solve_distance(MutableSpan<float3> positions,
                    const Span<float> inv_mass,
                    const Span<DistanceConstraint> constraints,
                    MutableSpan<float> lambdas,
                    const float alpha)
{
  for (const int i : constraints.index_range()) {
    const DistanceConstraint &c = constraints[i];
    const float w = inv_mass[c.a] + inv_mass[c.b];
    if (w == 0.0f) {
      continue;
    }
    const float3 delta = positions[c.a] - positions[c.b];
    const float length = math::length(delta);
    if (length < 1e-9f) {
      continue;
    }
    const float3 dir = delta / length;
    const float error = length - c.rest;
    const float d_lambda = (-error - alpha * lambdas[i]) / (w + alpha);
    lambdas[i] += d_lambda;
    positions[c.a] += dir * (d_lambda * inv_mass[c.a]);
    positions[c.b] -= dir * (d_lambda * inv_mass[c.b]);
  }
}

/** Points closer than `radius` that aren't joined by a constraint push each other apart. Uses a
 * hashed grid of cells of the size of the radius. */
void solve_self_collision(MutableSpan<float3> positions,
                          const Span<float> inv_mass,
                          const Span<int> neighbor_offsets_data,
                          const Span<int> neighbors,
                          const float radius)
{
  const OffsetIndices<int> neighbor_offsets(neighbor_offsets_data);
  const int n = positions.size();
  const int table_size = std::max(2 * n, 1);
  const float inv_cell = 1.0f / radius;
  auto cell_of = [&](const float3 &p) {
    return int3(int(std::floor(p.x * inv_cell)),
                int(std::floor(p.y * inv_cell)),
                int(std::floor(p.z * inv_cell)));
  };
  auto hash = [&](const int3 &cell) {
    const uint32_t h = (uint32_t(cell.x) * 92837111u) ^ (uint32_t(cell.y) * 689287499u) ^
                       (uint32_t(cell.z) * 283923481u);
    return int(h % uint32_t(table_size));
  };

  Array<int> cell_start(table_size + 1, 0);
  Array<int> point_hash(n);
  for (const int i : IndexRange(n)) {
    point_hash[i] = hash(cell_of(positions[i]));
    cell_start[point_hash[i] + 1]++;
  }
  for (const int i : IndexRange(table_size)) {
    cell_start[i + 1] += cell_start[i];
  }
  Array<int> entries(n);
  {
    Array<int> fill(cell_start.as_span().drop_back(1));
    for (const int i : IndexRange(n)) {
      entries[fill[point_hash[i]]++] = i;
    }
  }

  const float radius_sq = radius * radius;
  for (const int i : IndexRange(n)) {
    if (inv_mass[i] == 0.0f) {
      continue;
    }
    const int3 cell = cell_of(positions[i]);
    const Span<int> own_neighbors = neighbors.slice(neighbor_offsets[i]);
    /* Neighboring cells can share a bucket, visit each bucket once. */
    int visited[27];
    int visited_num = 0;
    for (int x = -1; x <= 1; x++) {
      for (int y = -1; y <= 1; y++) {
        for (int z = -1; z <= 1; z++) {
          const int h = hash(cell + int3(x, y, z));
          if (std::find(visited, visited + visited_num, h) != visited + visited_num) {
            continue;
          }
          visited[visited_num++] = h;
          for (const int j : entries.as_span().slice(cell_start[h], cell_start[h + 1] - cell_start[h])) {
            if (j <= i && inv_mass[j] != 0.0f) {
              /* Each free pair once; pinned partners are visited from this side only. */
              continue;
            }
            const float3 delta = positions[i] - positions[j];
            const float dist_sq = math::length_squared(delta);
            if (dist_sq >= radius_sq || dist_sq < 1e-14f) {
              continue;
            }
            if (own_neighbors.contains(j)) {
              continue;
            }
            const float dist = std::sqrt(dist_sq);
            const float w = inv_mass[i] + inv_mass[j];
            const float3 push = delta * ((radius - dist) / (dist * w));
            positions[i] += push * inv_mass[i];
            positions[j] -= push * inv_mass[j];
          }
        }
      }
    }
  }
}

}  // namespace

Mesh *mesh_drape(const Mesh &mesh, const MeshDrapeParams &params, const char **r_error)
{
  *r_error = nullptr;
  const int verts_num = mesh.verts_num;
  if (verts_num == 0) {
    return nullptr;
  }
  if (verts_num > mesh_drape_max_verts) {
    *r_error = "Too many points, the limit is one million";
    return nullptr;
  }

  const Span<float3> mesh_positions = mesh.vert_positions();
  Array<float3> positions(verts_num);
  for (const int i : IndexRange(verts_num)) {
    positions[i] = math::transform_point(params.mesh_to_space, mesh_positions[i]);
  }
  Array<float> inv_mass(verts_num, 1.0f);
  if (!params.pin.is_empty()) {
    for (const int i : IndexRange(std::min<int>(verts_num, params.pin.size()))) {
      if (params.pin[i]) {
        inv_mass[i] = 0.0f;
      }
    }
  }

  /* Constraints: the mesh edges and triangle edges hold the length, the far corners of
   * neighboring triangles hold the folds. */
  const Span<int> corner_verts = mesh.corner_verts();
  const Span<int3> tris = mesh.corner_tris();
  const float scale = 1.0f - std::clamp(params.shrink, -1.0f, 0.95f);
  Map<OrderedEdge, EdgeSides> tri_edges;
  for (const int3 &tri : tris) {
    const int v[3] = {corner_verts[tri[0]], corner_verts[tri[1]], corner_verts[tri[2]]};
    for (int k = 0; k < 3; k++) {
      EdgeSides &sides = tri_edges.lookup_or_add_default(OrderedEdge(v[k], v[(k + 1) % 3]));
      const int far = v[(k + 2) % 3];
      if (sides.first == -1) {
        sides.first = far;
      }
      else if (sides.second == -1) {
        sides.second = far;
      }
    }
  }
  auto rest_of = [&](const int a, const int b) {
    return math::distance(positions[a], positions[b]) * scale;
  };
  Vector<DistanceConstraint> stretch;
  Vector<DistanceConstraint> bend;
  for (const auto item : tri_edges.items()) {
    const OrderedEdge &edge = item.key;
    stretch.append({edge.v_low, edge.v_high, rest_of(edge.v_low, edge.v_high)});
    if (item.value.second != -1 && item.value.first != item.value.second) {
      bend.append(
          {item.value.first, item.value.second, rest_of(item.value.first, item.value.second)});
    }
  }
  for (const int2 &edge : mesh.edges()) {
    if (!tri_edges.contains(OrderedEdge(edge[0], edge[1]))) {
      /* Loose edges, like the strings of a tent. */
      stretch.append({edge[0], edge[1], rest_of(edge[0], edge[1])});
    }
  }

  float edge_sum = 0.0f;
  for (const DistanceConstraint &c : stretch) {
    edge_sum += c.rest;
  }
  const float mean_edge = stretch.is_empty() ? 0.01f : edge_sum / stretch.size();

  /* Neighbors don't push each other apart in self collision. */
  Array<int> neighbor_offsets_data;
  Array<int> neighbors;
  float self_radius = 0.0f;
  if (params.self_collision) {
    self_radius = std::max(params.thickness * 2.0f, mean_edge * 0.6f);
    neighbor_offsets_data.reinitialize(verts_num + 1);
    neighbor_offsets_data.fill(0);
    for (const Span<DistanceConstraint> list : {stretch.as_span(), bend.as_span()}) {
      for (const DistanceConstraint &c : list) {
        neighbor_offsets_data[c.a + 1]++;
        neighbor_offsets_data[c.b + 1]++;
      }
    }
    for (const int i : IndexRange(verts_num)) {
      neighbor_offsets_data[i + 1] += neighbor_offsets_data[i];
    }
    neighbors.reinitialize(neighbor_offsets_data.last());
    Array<int> fill(neighbor_offsets_data.as_span().drop_back(1));
    for (const Span<DistanceConstraint> list : {stretch.as_span(), bend.as_span()}) {
      for (const DistanceConstraint &c : list) {
        neighbors[fill[c.a]++] = c.b;
        neighbors[fill[c.b]++] = c.a;
      }
    }
  }

  /* The collider in simulation space. */
  Mesh *collider = nullptr;
  bke::BVHTreeFromMesh collider_bvh;
  if (params.collider != nullptr && params.collider->faces_num > 0) {
    collider = BKE_mesh_copy_for_eval(*params.collider);
    MutableSpan<float3> collider_positions = collider->vert_positions_for_write();
    for (float3 &p : collider_positions) {
      p = math::transform_point(params.collider_to_space, p);
    }
    collider->tag_positions_changed();
    collider_bvh = collider->bvh_corner_tris();
  }

  float floor_z = FLT_MAX;
  if (params.floor) {
    for (const float3 &p : positions) {
      floor_z = std::min(floor_z, p.z);
    }
    if (collider != nullptr) {
      for (const float3 &p : collider->vert_positions()) {
        floor_z = std::min(floor_z, p.z);
      }
    }
  }

  /* Stiffness and bending map to compliance on a log scale: 1 is fully stiff. */
  const float stiffness = std::clamp(params.stiffness, 0.0f, 1.0f);
  const float bending = std::clamp(params.bending, 0.0f, 1.0f);
  const float stretch_compliance = stiffness >= 1.0f ? 0.0f :
                                                       std::pow(10.0f, -6.0f + 4.0f * (1.0f - stiffness));
  const float bend_compliance = bending >= 1.0f ? 0.0f : std::pow(10.0f, 1.0f - 6.0f * bending);

  const int substeps = std::max(params.substeps, 1);
  const int frames = std::max(int(std::round(params.time * 60.0f)), 0);
  const float dt = 1.0f / (60.0f * float(substeps));
  const float stretch_alpha = stretch_compliance / (dt * dt);
  const float bend_alpha = bend_compliance / (dt * dt);
  const float damping = std::exp(-1.0f * dt);
  const float thickness = std::max(params.thickness, 0.0f);
  const float friction = std::clamp(params.friction, 0.0f, 1.0f);
  const float3 gravity(0.0f, 0.0f, -params.gravity);

  Array<float3> velocities(verts_num, float3(0.0f));
  Array<float3> previous(verts_num);
  Array<float> stretch_lambdas(stretch.size());
  Array<float> bend_lambdas(bend.size());
  Array<float3> pressure_push;
  float mean_area = 1.0f;
  if (params.pressure != 0.0f) {
    pressure_push.reinitialize(verts_num);
    float area_sum = 0.0f;
    for (const int3 &tri : tris) {
      const float3 &a = positions[corner_verts[tri[0]]];
      const float3 &b = positions[corner_verts[tri[1]]];
      const float3 &c = positions[corner_verts[tri[2]]];
      area_sum += math::length(math::cross(b - a, c - a)) * 0.5f;
    }
    mean_area = std::max(area_sum / float(verts_num), 1e-12f);
  }

  /* Push a point out of what it touches, keeping the side it came from, and take friction off
   * the sliding along the surface. */
  auto push_out = [&](float3 &p, const float3 &from, const float3 &normal_in, const float3 &on) {
    const float side = math::dot(from - on, normal_in) >= 0.0f ? 1.0f : -1.0f;
    const float3 normal = normal_in * side;
    const float depth = math::dot(p - on, normal);
    if (depth >= thickness) {
      return;
    }
    p += normal * (thickness - depth);
    const float3 moved = p - from;
    const float3 slide = moved - normal * math::dot(moved, normal);
    p -= slide * friction;
  };

  for (int frame = 0; frame < frames; frame++) {
    for (int step = 0; step < substeps; step++) {
      if (!pressure_push.is_empty()) {
        pressure_push.fill(float3(0.0f));
        for (const int3 &tri : tris) {
          const int a = corner_verts[tri[0]], b = corner_verts[tri[1]], c = corner_verts[tri[2]];
          /* Half the cross product is the area times the normal. */
          const float3 push = math::cross(positions[b] - positions[a], positions[c] - positions[a]) /
                              6.0f;
          pressure_push[a] += push;
          pressure_push[b] += push;
          pressure_push[c] += push;
        }
      }
      threading::parallel_for(IndexRange(verts_num), 4096, [&](const IndexRange range) {
        for (const int i : range) {
          previous[i] = positions[i];
          if (inv_mass[i] == 0.0f) {
            continue;
          }
          float3 acceleration = gravity;
          if (!pressure_push.is_empty()) {
            acceleration += pressure_push[i] * (params.pressure * 20.0f / mean_area);
          }
          velocities[i] = (velocities[i] + acceleration * dt) * damping;
          positions[i] += velocities[i] * dt;
        }
      });

      stretch_lambdas.fill(0.0f);
      bend_lambdas.fill(0.0f);
      for (int iteration = 0; iteration < 2; iteration++) {
        solve_distance(positions, inv_mass, stretch, stretch_lambdas, stretch_alpha);
        solve_distance(positions, inv_mass, bend, bend_lambdas, bend_alpha);
      }

      if (params.self_collision) {
        solve_self_collision(positions, inv_mass, neighbor_offsets_data, neighbors, self_radius);
      }

      threading::parallel_for(IndexRange(verts_num), 1024, [&](const IndexRange range) {
        for (const int i : range) {
          if (inv_mass[i] == 0.0f) {
            continue;
          }
          float3 &p = positions[i];
          if (collider != nullptr) {
            const float reach = thickness + math::distance(p, previous[i]) + mean_edge * 0.5f;
            BVHTreeNearest nearest;
            nearest.index = -1;
            nearest.dist_sq = reach * reach;
            BLI_bvhtree_find_nearest(collider_bvh.tree,
                                     p,
                                     &nearest,
                                     collider_bvh.nearest_callback,
                                     &collider_bvh);
            if (nearest.index != -1) {
              push_out(p, previous[i], float3(nearest.no), float3(nearest.co));
            }
          }
          if (params.floor && p.z < floor_z + thickness) {
            push_out(p, previous[i], float3(0.0f, 0.0f, 1.0f), float3(p.x, p.y, floor_z));
          }
          velocities[i] = (p - previous[i]) / dt;
        }
      });
    }
  }

  if (collider != nullptr) {
    collider_bvh = {};
    BKE_id_free(nullptr, collider);
  }

  Mesh *result = BKE_mesh_copy_for_eval(mesh);
  MutableSpan<float3> result_positions = result->vert_positions_for_write();
  const float4x4 space_to_mesh = math::invert(params.mesh_to_space);
  for (const int i : IndexRange(verts_num)) {
    if (inv_mass[i] != 0.0f) {
      result_positions[i] = math::transform_point(space_to_mesh, positions[i]);
    }
  }
  result->tag_positions_changed();
  return result;
}

}  // namespace blender::geometry
