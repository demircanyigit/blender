/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup geo
 *
 * Collapse: every mesh island falls as a rigid piece and the mesh is returned in the pose where
 * the pieces came to rest. A private Bullet world is made for each solve and freed after it.
 */

#include <algorithm>
#include <cmath>

#include "DNA_mesh_types.h"

#include "BLI_array.hh"
#include "BLI_disjoint_set.hh"
#include "BLI_map.hh"
#include "BLI_math_matrix.hh"
#include "BLI_math_quaternion.hh"
#include "BLI_math_vector.hh"
#include "BLI_rand.hh"
#include "BLI_vector.hh"

#include "BKE_mesh.hh"

#include "GEO_mesh_collapse.hh"
#include "GEO_mesh_sim_cache.hh"

#ifdef WITH_BULLET
#  include "RBI_api.h"
#endif

namespace blender::geometry {

#ifdef WITH_BULLET

namespace {

struct Island {
  /** Center of mass in simulation space. */
  float3 center = float3(0.0f);
  float mass = 0.0f;
  float size = 0.0f;
  bool keep = false;
};

/** Mass and center of mass of each island, from the volume its triangles enclose. Pieces that
 * enclose no volume (open or flat) use their bounding box and the mean of their points. */
void compute_islands(const Mesh &mesh,
                     const Span<float3> positions,
                     const Span<int> island_of_vert,
                     const Span<int> verts_of_island_offsets,
                     const Span<int> verts_of_island,
                     MutableSpan<Island> islands)
{
  const int islands_num = islands.size();
  Array<double> volume(islands_num, 0.0);
  Array<double3> moment(islands_num, double3(0.0));
  const Span<int> corner_verts = mesh.corner_verts();
  for (const int3 tri : mesh.corner_tris()) {
    const int v0 = corner_verts[tri[0]];
    const float3 &a = positions[v0];
    const float3 &b = positions[corner_verts[tri[1]]];
    const float3 &c = positions[corner_verts[tri[2]]];
    const double vol6 = double(math::dot(a, math::cross(b, c)));
    const int island = island_of_vert[v0];
    volume[island] += vol6;
    moment[island] += vol6 * (double3(a) + double3(b) + double3(c));
  }

  for (const int i : IndexRange(islands_num)) {
    const Span<int> verts = verts_of_island.slice(verts_of_island_offsets[i],
                                                  verts_of_island_offsets[i + 1] -
                                                      verts_of_island_offsets[i]);
    float3 min(FLT_MAX), max(-FLT_MAX), sum(0.0f);
    for (const int v : verts) {
      min = math::min(min, positions[v]);
      max = math::max(max, positions[v]);
      sum += positions[v];
    }
    const float3 extent = math::max(max - min, float3(1e-4f));
    const float size = math::length(max - min);
    Island &island = islands[i];
    island.size = size;
    const double vol = std::abs(volume[i]) / 6.0;
    const double box_volume = double(extent.x) * extent.y * extent.z;
    if (vol > box_volume * 1e-3) {
      const float3 center = float3(moment[i] / (4.0 * volume[i]));
      /* An open piece can put the center far outside, use the mean of the points then. */
      const bool inside = math::reduce_min(center - min) >= -size * 0.01f &&
                          math::reduce_min(max - center) >= -size * 0.01f;
      island.center = inside ? center : sum / float(verts.size());
      island.mass = float(vol);
    }
    else {
      island.center = sum / float(verts.size());
      island.mass = float(box_volume);
    }
    island.mass = std::max(island.mass, 1e-6f);
  }
}

rbCollisionShape *island_shape(const Span<float3> local_positions, const float size)
{
  float3 min(FLT_MAX), max(-FLT_MAX);
  for (const float3 &p : local_positions) {
    min = math::min(min, p);
    max = math::max(max, p);
  }
  const float3 half = math::max((max - min) * 0.5f, float3(1e-4f));
  if (local_positions.size() < 4 || math::reduce_min(max - min) < size * 1e-4f) {
    /* Too few points or flat: the hull can't be made, use the box around the points. */
    rbCollisionShape *shape = RB_shape_new_box(half.x, half.y, half.z);
    RB_shape_set_margin(shape, std::min(math::reduce_min(half), 1e-3f));
    return shape;
  }
  /* The margin is taken off the hull and added back by Bullet, so pieces that touch at the start
   * don't overlap and push each other apart. */
  const float margin = std::clamp(size * 0.02f, 1e-4f, 0.04f);
  bool can_embed = true;
  rbCollisionShape *shape = RB_shape_new_convex_hull(
      reinterpret_cast<const float *>(local_positions.data()),
      sizeof(float3),
      int(local_positions.size()),
      margin,
      &can_embed);
  RB_shape_set_margin(shape, can_embed ? margin : std::max(size * 1e-3f, 1e-5f));
  return shape;
}

rbCollisionShape *ground_shape(const Mesh &ground,
                              const float4x4 &ground_to_space,
                              const float cage)
{
  const Span<int3> tris = ground.corner_tris();
  if (tris.is_empty()) {
    return nullptr;
  }
  const Span<float3> positions = ground.vert_positions();
  const Span<int> corner_verts = ground.corner_verts();
  Array<float3> space_positions(positions.size());
  for (const int i : positions.index_range()) {
    space_positions[i] = math::transform_point(ground_to_space, positions[i]);
  }
  if (cage != 0.0f) {
    /* The cage: pushed out along the normals, so the pieces land that far away. */
    Array<float3> normals(positions.size(), float3(0.0f));
    for (const int3 &tri : tris) {
      const int a = corner_verts[tri[0]];
      const int b = corner_verts[tri[1]];
      const int c = corner_verts[tri[2]];
      /* Weighted by area, so small triangles don't tip the normals. */
      const float3 n = math::cross(space_positions[b] - space_positions[a],
                                   space_positions[c] - space_positions[a]);
      normals[a] += n;
      normals[b] += n;
      normals[c] += n;
    }
    for (const int i : positions.index_range()) {
      space_positions[i] += math::normalize(normals[i]) * cage;
    }
  }
  rbMeshData *data = RB_trimesh_data_new(int(tris.size()), int(positions.size()));
  RB_trimesh_add_vertices(data,
                          reinterpret_cast<float *>(space_positions.data()),
                          int(positions.size()),
                          sizeof(float3));
  for (const int i : tris.index_range()) {
    RB_trimesh_add_triangle_indices(data,
                                    i,
                                    corner_verts[tris[i][0]],
                                    corner_verts[tris[i][1]],
                                    corner_verts[tris[i][2]]);
  }
  RB_trimesh_finish(data);
  rbCollisionShape *shape = RB_shape_new_trimesh(data);
  RB_shape_set_margin(shape, 1e-4f);
  return shape;
}

}  // namespace

#endif

static Mesh *mesh_collapse_compute(const Mesh &mesh,
                                   const MeshCollapseParams &params,
                                   const char **r_error)
{
  *r_error = nullptr;
#ifndef WITH_BULLET
  UNUSED_VARS(mesh, params);
  *r_error = "Physics is not available in this build";
  return nullptr;
#else
  const int verts_num = mesh.verts_num;
  if (verts_num == 0) {
    return nullptr;
  }

  /* Positions in simulation space. */
  const Span<float3> mesh_positions = mesh.vert_positions();
  Array<float3> positions(verts_num);
  for (const int i : IndexRange(verts_num)) {
    positions[i] = math::transform_point(params.mesh_to_space, mesh_positions[i]);
  }

  Array<int> island_of_vert(verts_num);
  int islands_num = 0;
  if (params.pieces.size() == int64_t(verts_num)) {
    /* Pieces given per point, numbered from 0 in the order they first appear. */
    Map<int, int> island_of_piece;
    for (const int v : IndexRange(verts_num)) {
      island_of_vert[v] = island_of_piece.lookup_or_add(params.pieces[v], islands_num);
      if (island_of_vert[v] == islands_num) {
        islands_num++;
      }
    }
  }
  else {
    /* Islands: points joined by edges. */
    DisjointSet<int> sets(verts_num);
    for (const int2 edge : mesh.edges()) {
      sets.join(edge[0], edge[1]);
    }
    Array<int> island_of_root(verts_num, -1);
    for (const int v : IndexRange(verts_num)) {
      const int root = sets.find_root(v);
      if (island_of_root[root] == -1) {
        island_of_root[root] = islands_num++;
      }
      island_of_vert[v] = island_of_root[root];
    }
  }
  if (islands_num > mesh_collapse_max_islands) {
    *r_error = "Too many pieces, the limit is 20000";
    return nullptr;
  }

  /* The points of each island, in order. */
  Array<int> offsets(islands_num + 1, 0);
  for (const int island : island_of_vert) {
    offsets[island + 1]++;
  }
  for (const int i : IndexRange(islands_num)) {
    offsets[i + 1] += offsets[i];
  }
  Array<int> verts_of_island(verts_num);
  {
    Array<int> fill(offsets.as_span().drop_back(1));
    for (const int v : IndexRange(verts_num)) {
      verts_of_island[fill[island_of_vert[v]]++] = v;
    }
  }

  Array<Island> islands(islands_num);
  compute_islands(mesh, positions, island_of_vert, offsets, verts_of_island, islands);
  if (!params.keep.is_empty()) {
    for (const int v : IndexRange(std::min<int>(verts_num, params.keep.size()))) {
      if (params.keep[v]) {
        islands[island_of_vert[v]].keep = true;
      }
    }
  }

  /* How high each piece is over the lowest point decides how hard it's pushed. */
  float lowest = FLT_MAX, highest = -FLT_MAX;
  for (const float3 &p : positions) {
    lowest = std::min(lowest, p.z);
    highest = std::max(highest, p.z);
  }
  const float tallest = std::max(highest - lowest, 1e-4f);
  const float lean_angle = RandomNumberGenerator(uint32_t(params.seed) * 2654435761u + 7u)
                               .get_float() *
                           6.28318530718f;

  /* The world and its bodies. */
  const float gravity[3] = {0.0f, 0.0f, -params.gravity};
  rbDynamicsWorld *world = RB_dworld_new(gravity);
  RB_dworld_set_solver_iterations(world, 20);
  RB_dworld_set_split_impulse(world, 1);

  Vector<rbCollisionShape *> shapes;
  Vector<rbRigidBody *> bodies;
  Array<rbRigidBody *> island_bodies(islands_num, nullptr);
  const float unit_quat[4] = {1.0f, 0.0f, 0.0f, 0.0f};

  for (const int i : IndexRange(islands_num)) {
    const Island &island = islands[i];
    const Span<int> verts = verts_of_island.as_span().slice(offsets[i], offsets[i + 1] - offsets[i]);
    Array<float3> local(verts.size());
    for (const int j : verts.index_range()) {
      local[j] = positions[verts[j]] - island.center;
    }
    rbCollisionShape *shape = island_shape(local, island.size);
    shapes.append(shape);
    rbRigidBody *body = RB_body_new(shape, island.center, unit_quat);
    RB_body_set_mass(body, island.keep ? 0.0f : island.mass);
    RB_body_set_friction(body, params.friction);
    RB_body_set_restitution(body, params.bounce);
    RB_body_set_damping(body, 0.04f, 0.1f);
    RB_body_set_sleep_thresh(body, 0.4f, 0.5f);
    RB_body_set_activation_state(body, true);
    if (!island.keep && params.push > 0.0f) {
      /* Lean everything one way, harder the higher up, like a column tipping over, with some
       * randomness per piece so the pile doesn't look neat. */
      RandomNumberGenerator rng(uint32_t(params.seed) * 2654435761u + uint32_t(i) * 40503u);
      const float height = (island.center.z - lowest) / tallest;
      const float speed = params.push * (0.2f + 1.8f * height) * (0.7f + 0.6f * rng.get_float());
      const float angle = lean_angle + (rng.get_float() - 0.5f) * 1.2f;
      const float3 velocity(std::cos(angle) * speed, std::sin(angle) * speed, 0.0f);
      const float3 spin = rng.get_unit_float3() * params.push;
      RB_body_set_linear_velocity(body, velocity);
      RB_body_set_angular_velocity(body, spin);
    }
    RB_dworld_add_body(world, body, 1);
    bodies.append(body);
    island_bodies[i] = body;
  }

  /* The ground: the given mesh, or a floor under the lowest point. */
  rbCollisionShape *floor = nullptr;
  if (params.ground != nullptr) {
    floor = ground_shape(*params.ground, params.ground_to_space, params.ground_cage);
  }
  if (floor == nullptr) {
    float3 min(FLT_MAX), max(-FLT_MAX);
    for (const float3 &p : positions) {
      min = math::min(min, p);
      max = math::max(max, p);
    }
    const float size = math::length(max - min);
    const float half_xy = size * 20.0f + 10.0f;
    const float half_z = size * 0.5f + 0.1f;
    floor = RB_shape_new_box(half_xy, half_xy, half_z);
    RB_shape_set_margin(floor, 1e-4f);
    const float3 center = float3((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f, min.z - half_z);
    rbRigidBody *body = RB_body_new(floor, center, unit_quat);
    RB_body_set_mass(body, 0.0f);
    RB_body_set_friction(body, params.friction);
    RB_body_set_restitution(body, params.bounce);
    RB_dworld_add_body(world, body, 1);
    bodies.append(body);
  }
  else {
    const float origin[3] = {0.0f, 0.0f, 0.0f};
    rbRigidBody *body = RB_body_new(floor, origin, unit_quat);
    RB_body_set_mass(body, 0.0f);
    RB_body_set_friction(body, params.friction);
    RB_body_set_restitution(body, params.bounce);
    RB_dworld_add_body(world, body, 1);
    bodies.append(body);
  }
  shapes.append(floor);

  /* Fall. */
  const int substeps = std::max(params.substeps, 1);
  const int frames = std::max(int(std::round(params.time * 60.0f)), 0);
  for (int frame = 0; frame < frames; frame++) {
    RB_dworld_step_simulation(world, 1.0f / 60.0f, substeps, 1.0f / (60.0f * substeps));
  }

  /* Move each island to where its body is. */
  Mesh *result = BKE_mesh_copy_for_eval(mesh);
  MutableSpan<float3> result_positions = result->vert_positions_for_write();
  const float4x4 space_to_mesh = math::invert(params.mesh_to_space);
  Array<float4x4> transforms(islands_num);
  for (const int i : IndexRange(islands_num)) {
    /* Bullet uses doubles here, so read the place and turn rather than its matrix. */
    float3 location;
    float4 rotation;
    RB_body_get_position(island_bodies[i], location);
    RB_body_get_orientation(island_bodies[i], rotation);
    const float4x4 body_matrix = math::from_loc_rot<float4x4>(
        location, math::normalize(math::Quaternion(rotation)));
    transforms[i] = space_to_mesh * body_matrix * math::from_location<float4x4>(-islands[i].center);
  }
  for (const int v : IndexRange(verts_num)) {
    const int island = island_of_vert[v];
    if (!islands[island].keep) {
      result_positions[v] = math::transform_point(transforms[island], positions[v]);
    }
  }
  result->tag_positions_changed();

  for (rbRigidBody *body : bodies) {
    RB_dworld_remove_body(world, body);
    RB_body_delete(body);
  }
  for (rbCollisionShape *shape : shapes) {
    RB_shape_delete(shape);
  }
  RB_dworld_delete(world);
  return result;
#endif
}

Mesh *mesh_collapse(const Mesh &mesh, const MeshCollapseParams &params, const char **r_error)
{
  /* Objects are evaluated again for undo and for changes to other objects. Taking the result from
   * the cache when nothing it depends on changed keeps that from simulating all over again. */
  SimCacheHasher hasher("mesh_collapse");
  hasher.add_mesh(mesh);
  hasher.add(params.time);
  hasher.add(params.gravity);
  hasher.add(params.push);
  hasher.add(params.seed);
  hasher.add(params.friction);
  hasher.add(params.bounce);
  hasher.add(params.substeps);
  hasher.add(params.mesh_to_space);
  hasher.add(params.ground != nullptr);
  if (params.ground != nullptr) {
    hasher.add_mesh_shape(*params.ground);
    hasher.add(params.ground_to_space);
    hasher.add(params.ground_cage);
  }
  hasher.add_span(params.keep);
  hasher.add_span(params.pieces);
  return sim_cache_get(
      hasher.finish(),
      [&](const char **r_compute_error) { return mesh_collapse_compute(mesh, params, r_compute_error); },
      r_error);
}

}  // namespace blender::geometry
