/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup geo
 */

#include <algorithm>
#include <cfloat>
#include <cmath>

#include "BLI_math_base.h"
#include "BLI_array.hh"
#include "BLI_bounds.hh"
#include "BLI_kdopbvh.hh"
#include "BLI_math_matrix.hh"
#include "BLI_math_vector.hh"
#include "BLI_noise.hh"
#include "BLI_task.hh"

#include "DNA_mesh_types.h"

#include "BKE_attribute.hh"
#include "BKE_bvhutils.hh"
#include "BKE_mesh.hh"
#include "BKE_mesh_mapping.hh"

#include "GEO_mesh_materialize.hh"

namespace blender::geometry {

static constexpr float QUARTER_PI = float(M_PI) / 4.0f;

static float hash3(const float a, const float b, const float c)
{
  return noise::hash_float_to_float(float3(a, b, c));
}

struct FaceData {
  Array<float3> centers;
  Array<float3> normals;
  OffsetIndices<int> faces;
  Span<int> corner_edges;
  Span<int> corner_verts;
  Array<int> edge_offsets;
  Array<int> edge_indices;
  GroupedSpan<int> edge_to_face;
  GroupedSpan<int> vert_to_face;
  Array<int> islands;
};

static Array<int> face_islands(const Mesh &mesh, const FaceData &data)
{
  /* Union-find over faces sharing an edge. */
  Array<int> parent(mesh.faces_num);
  for (const int f : parent.index_range()) {
    parent[f] = f;
  }
  auto find = [&](int f) {
    while (parent[f] != f) {
      parent[f] = parent[parent[f]];
      f = parent[f];
    }
    return f;
  };
  for (const int e : IndexRange(mesh.edges_num)) {
    const Span<int> faces = data.edge_to_face[e];
    for (const int i : faces.index_range().drop_front(1)) {
      const int a = find(faces[0]);
      const int b = find(faces[i]);
      if (a != b) {
        parent[std::max(a, b)] = std::min(a, b);
      }
    }
  }
  Array<int> island(mesh.faces_num);
  for (const int f : island.index_range()) {
    island[f] = find(f);
  }
  return island;
}

/** Signed fold angle of each edge between two faces: positive where it folds outward. */
static Array<float> edge_folds(const Mesh &mesh, const FaceData &data)
{
  Array<float> folds(mesh.edges_num, 0.0f);
  threading::parallel_for(IndexRange(mesh.edges_num), 2048, [&](const IndexRange range) {
    for (const int e : range) {
      const Span<int> faces = data.edge_to_face[e];
      if (faces.size() != 2) {
        continue;
      }
      const float3 &n1 = data.normals[faces[0]];
      const float3 &n2 = data.normals[faces[1]];
      const float angle = std::acos(std::clamp(math::dot(n1, n2), -1.0f, 1.0f));
      const bool convex = math::dot(n1, data.centers[faces[1]] - data.centers[faces[0]]) < 0.0f;
      folds[e] = convex ? angle : -angle;
    }
  });
  return folds;
}

static float3 hemisphere_direction(const float3 &n, const float u1, const float u2)
{
  const float3 helper = std::abs(n.z) < 0.9f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
  const float3 t = math::normalize(math::cross(helper, n));
  const float3 b = math::cross(n, t);
  const float r = std::sqrt(u1);
  const float phi = 2.0f * float(M_PI) * u2;
  return math::normalize(t * (r * std::cos(phi)) + b * (r * std::sin(phi)) +
                         n * std::sqrt(std::max(0.0f, 1.0f - u1)));
}

static void compute_margins(const Mesh &mesh,
                            const MaterializeRule &rule,
                            const MeshMaterializeParams &params,
                            const FaceData &data,
                            const Span<float3> centers,
                            const Span<float3> normals,
                            MutableSpan<float> r_margin)
{
  const int faces_num = mesh.faces_num;
  r_margin.fill(-1.0f);
  switch (rule.type) {
    case MaterializeType::Curvature: {
      const Array<float> folds = edge_folds(mesh, data);
      threading::parallel_for(IndexRange(faces_num), 2048, [&](const IndexRange range) {
        for (const int f : range) {
          float best = -FLT_MAX;
          for (const int e : data.corner_edges.slice(data.faces[f])) {
            const float fold = folds[e];
            const float value = rule.curvature == MaterializeCurvature::Convex  ? fold :
                                rule.curvature == MaterializeCurvature::Concave ? -fold :
                                                                                  std::abs(fold);
            best = std::max(best, value);
          }
          r_margin[f] = (best - rule.curvature_angle) / QUARTER_PI;
        }
      });
      break;
    }
    case MaterializeType::Direction: {
      float3 dir(0.0f, 0.0f, 1.0f);
      switch (rule.direction) {
        case MaterializeDirection::Up:
        case MaterializeDirection::Sides:
          break;
        case MaterializeDirection::Down:
          dir = float3(0.0f, 0.0f, -1.0f);
          break;
        case MaterializeDirection::PosX:
          dir = float3(1.0f, 0.0f, 0.0f);
          break;
        case MaterializeDirection::NegX:
          dir = float3(-1.0f, 0.0f, 0.0f);
          break;
        case MaterializeDirection::PosY:
          dir = float3(0.0f, 1.0f, 0.0f);
          break;
        case MaterializeDirection::NegY:
          dir = float3(0.0f, -1.0f, 0.0f);
          break;
        case MaterializeDirection::Custom:
          dir = math::normalize(rule.custom_direction);
          break;
      }
      threading::parallel_for(IndexRange(faces_num), 4096, [&](const IndexRange range) {
        for (const int f : range) {
          float angle;
          if (rule.direction == MaterializeDirection::Sides) {
            angle = std::asin(std::clamp(std::abs(normals[f].z), 0.0f, 1.0f));
          }
          else {
            angle = std::acos(std::clamp(math::dot(normals[f], dir), -1.0f, 1.0f));
          }
          r_margin[f] = (rule.direction_angle - angle) / QUARTER_PI;
        }
      });
      break;
    }
    case MaterializeType::Height: {
      const int axis = std::clamp(rule.axis, 0, 2);
      float lo = FLT_MAX;
      float hi = -FLT_MAX;
      for (const float3 &c : centers) {
        lo = std::min(lo, c[axis]);
        hi = std::max(hi, c[axis]);
      }
      const float size = std::max(hi - lo, 1e-6f);
      threading::parallel_for(IndexRange(faces_num), 4096, [&](const IndexRange range) {
        for (const int f : range) {
          float v = centers[f][axis];
          float scale = size * 0.25f;
          if (rule.relative) {
            v = (v - lo) / size;
            scale = 0.25f;
          }
          const float from = std::min(rule.height_from, rule.height_to);
          const float to = std::max(rule.height_from, rule.height_to);
          r_margin[f] = std::min(v - from, to - v) / scale;
        }
      });
      break;
    }
    case MaterializeType::Slope: {
      threading::parallel_for(IndexRange(faces_num), 4096, [&](const IndexRange range) {
        for (const int f : range) {
          const float angle = std::acos(std::clamp(normals[f].z, -1.0f, 1.0f));
          const float lo = std::min(rule.slope_min, rule.slope_max);
          const float hi = std::max(rule.slope_min, rule.slope_max);
          r_margin[f] = std::min(angle - lo, hi - angle) / QUARTER_PI;
        }
      });
      break;
    }
    case MaterializeType::Noise: {
      const float3 offset = noise::hash_float_to_float3(float(rule.seed) + 0.25f) * 1000.0f;
      threading::parallel_for(IndexRange(faces_num), 1024, [&](const IndexRange range) {
        for (const int f : range) {
          const float n = noise::perlin_fbm(centers[f] * rule.noise_scale + offset,
                                            std::max(rule.noise_detail, 0.0f),
                                            0.5f,
                                            2.0f,
                                            true);
          r_margin[f] = (n - rule.noise_threshold) / 0.25f;
        }
      });
      break;
    }
    case MaterializeType::Random: {
      const float seed = float(rule.seed);
      const float object_r = hash3(float(params.object_seed), seed, 3.0f);
      threading::parallel_for(IndexRange(faces_num), 4096, [&](const IndexRange range) {
        for (const int f : range) {
          float r = object_r;
          if (rule.random == MaterializeRandom::Face) {
            r = hash3(float(f), seed, 1.0f);
          }
          else if (rule.random == MaterializeRandom::Island) {
            r = hash3(float(data.islands[f]), seed, 2.0f);
          }
          r_margin[f] = (rule.chance - r) / 0.25f;
        }
      });
      break;
    }
    case MaterializeType::Attribute:
    case MaterializeType::Texture: {
      if (rule.values.size() != faces_num) {
        break;
      }
      for (const int f : IndexRange(faces_num)) {
        r_margin[f] = (rule.values[f] - rule.threshold) / 0.25f;
      }
      break;
    }
    case MaterializeType::Proximity: {
      if (rule.distances.size() != faces_num) {
        break;
      }
      const float d = std::max(rule.distance, 1e-6f);
      for (const int f : IndexRange(faces_num)) {
        r_margin[f] = (d - rule.distances[f]) / d;
      }
      break;
    }
    case MaterializeType::Occlusion:
    case MaterializeType::Thickness: {
      const bke::BVHTreeFromMesh bvh = mesh.bvh_corner_tris();
      if (bvh.tree == nullptr) {
        break;
      }
      const Bounds<float3> bounds = *bounds::min_max(mesh.vert_positions());
      const float eps = std::max(math::distance(bounds.min, bounds.max) * 1e-5f, 1e-6f);
      const int samples = std::max(rule.samples, 1);
      const bool occlusion = rule.type == MaterializeType::Occlusion;
      const float max_dist = occlusion ? std::max(rule.occlusion_distance, 1e-6f) :
                                         std::max(rule.thickness * 4.0f, 1e-6f);
      /* Rays use the mesh's own space. */
      const Span<float3> local_normals = data.normals;
      threading::parallel_for(IndexRange(faces_num), 64, [&](const IndexRange range) {
        for (const int f : range) {
          const float3 n = occlusion ? local_normals[f] : -local_normals[f];
          const float3 origin = data.centers[f] + n * eps;
          int hits = 0;
          float total = 0.0f;
          for (const int s : IndexRange(samples)) {
            const float3 dir = hemisphere_direction(n,
                                                    hash3(float(f), float(s), 11.0f),
                                                    hash3(float(f), float(s), 12.0f));
            BVHTreeRayHit hit;
            hit.index = -1;
            hit.dist = max_dist;
            BLI_bvhtree_ray_cast(bvh.tree,
                                 origin,
                                 dir,
                                 0.0f,
                                 &hit,
                                 bvh.raycast_callback,
                                 const_cast<bke::BVHTreeFromMesh *>(&bvh));
            if (hit.index != -1) {
              hits++;
              total += hit.dist;
            }
            else {
              total += max_dist;
            }
          }
          if (occlusion) {
            const float occ = float(hits) / float(samples);
            r_margin[f] = (occ - rule.occlusion) / 0.25f;
          }
          else {
            const float thickness = total / float(samples);
            const float t = std::max(rule.thickness, 1e-6f);
            r_margin[f] = (t - thickness) / t;
          }
        }
      });
      break;
    }
  }

  if (rule.breakup > 0.0f) {
    const float3 offset = noise::hash_float_to_float3(float(rule.seed) + 0.75f) * 1000.0f;
    threading::parallel_for(IndexRange(faces_num), 1024, [&](const IndexRange range) {
      for (const int f : range) {
        const float n = noise::perlin_fbm(
            centers[f] * rule.breakup_scale + offset, 2.0f, 0.5f, 2.0f, true);
        r_margin[f] += (n - 0.5f) * 2.0f * rule.breakup;
      }
    });
  }
}

static void grow_selection(MutableSpan<bool> selection, const FaceData &data, const int steps)
{
  const bool grow = steps > 0;
  for ([[maybe_unused]] const int step : IndexRange(std::abs(steps))) {
    Array<bool> next(selection.as_span());
    threading::parallel_for(selection.index_range(), 2048, [&](const IndexRange range) {
      for (const int f : range) {
        if (selection[f] == grow) {
          continue;
        }
        /* A face changes when any face sharing a point is the other way. */
        for (const int vert : data.corner_verts.slice(data.faces[f])) {
          for (const int other : data.vert_to_face[vert]) {
            if (selection[other] == grow) {
              next[f] = grow;
              break;
            }
          }
          if (next[f] == grow) {
            break;
          }
        }
      }
    });
    selection.copy_from(next);
  }
}

static void drop_small_islands(MutableSpan<bool> selection,
                               const Mesh &mesh,
                               const FaceData &data,
                               const int min_faces)
{
  Array<int> parent(selection.size());
  for (const int f : parent.index_range()) {
    parent[f] = f;
  }
  auto find = [&](int f) {
    while (parent[f] != f) {
      parent[f] = parent[parent[f]];
      f = parent[f];
    }
    return f;
  };
  for (const int e : IndexRange(mesh.edges_num)) {
    const Span<int> faces = data.edge_to_face[e];
    for (const int i : faces.index_range().drop_front(1)) {
      if (!selection[faces[0]] || !selection[faces[i]]) {
        continue;
      }
      const int a = find(faces[0]);
      const int b = find(faces[i]);
      if (a != b) {
        parent[std::max(a, b)] = std::min(a, b);
      }
    }
  }
  Array<int> count(selection.size(), 0);
  for (const int f : selection.index_range()) {
    if (selection[f]) {
      count[find(f)]++;
    }
  }
  for (const int f : selection.index_range()) {
    if (selection[f] && count[find(f)] < min_faces) {
      selection[f] = false;
    }
  }
}

void mesh_materialize(Mesh &mesh, const MeshMaterializeParams &params)
{
  const int faces_num = mesh.faces_num;
  if (faces_num == 0 || params.rules.is_empty()) {
    return;
  }
  const Span<float3> positions = mesh.vert_positions();
  const Span<int> corner_verts = mesh.corner_verts();

  FaceData data;
  data.faces = mesh.faces();
  data.corner_edges = mesh.corner_edges();
  data.corner_verts = corner_verts;
  data.normals = Array<float3>(mesh.face_normals());
  data.centers = Array<float3>(faces_num);
  threading::parallel_for(IndexRange(faces_num), 4096, [&](const IndexRange range) {
    for (const int f : range) {
      float3 sum(0.0f);
      for (const int v : corner_verts.slice(data.faces[f])) {
        sum += positions[v];
      }
      data.centers[f] = sum / float(data.faces[f].size());
    }
  });
  data.edge_to_face = bke::mesh::build_edge_to_face_map(
      data.faces, data.corner_edges, mesh.edges_num, data.edge_offsets, data.edge_indices);
  data.vert_to_face = mesh.vert_to_face_map();
  data.islands = face_islands(mesh, data);

  /* World space versions for rules that ask for them. */
  Array<float3> world_centers;
  Array<float3> world_normals;
  auto ensure_world = [&]() {
    if (!world_centers.is_empty()) {
      return;
    }
    world_centers = Array<float3>(faces_num);
    world_normals = Array<float3>(faces_num);
    const float3x3 normal_matrix = math::transpose(
        math::invert(float3x3(params.object_to_world)));
    for (const int f : IndexRange(faces_num)) {
      world_centers[f] = math::transform_point(params.object_to_world, data.centers[f]);
      world_normals[f] = math::normalize(normal_matrix * data.normals[f]);
    }
  };

  bke::MutableAttributeAccessor attributes = mesh.attributes_for_write();
  bke::SpanAttributeWriter<int> material_index =
      attributes.lookup_or_add_for_write_span<int>("material_index", bke::AttrDomain::Face);
  const Array<int> original(material_index.span.as_span());
  Array<int> picked(faces_num, -1);

  Array<float> margin(faces_num);
  Array<bool> selection(faces_num);
  for (const int rule_i : params.rules.index_range()) {
    const MaterializeRule &rule = params.rules[rule_i];
    Span<float3> centers = data.centers;
    Span<float3> normals = data.normals;
    if (rule.global) {
      ensure_world();
      centers = world_centers;
      normals = world_normals;
    }
    compute_margins(mesh, rule, params, data, centers, normals, margin);
    for (const int f : IndexRange(faces_num)) {
      selection[f] = (margin[f] > 0.0f) != rule.invert;
    }
    if (rule.grow != 0) {
      grow_selection(selection, data, rule.grow);
    }
    if (rule.only_on >= 0) {
      for (const int f : IndexRange(faces_num)) {
        selection[f] &= original[f] == rule.only_on;
      }
    }
    if (rule.min_island > 1) {
      drop_small_islands(selection, mesh, data, rule.min_island);
    }
    for (const int f : IndexRange(faces_num)) {
      if (selection[f]) {
        material_index.span[f] = rule.material;
        picked[f] = rule_i;
      }
    }
  }
  material_index.finish();

  if (params.rule_id) {
    bke::SpanAttributeWriter<int> writer = attributes.lookup_or_add_for_write_only_span<int>(
        *params.rule_id, bke::AttrDomain::Face);
    if (writer) {
      writer.span.copy_from(picked);
      writer.finish();
    }
  }
}

Array<float> materialize_face_distances(const Mesh &mesh,
                                        const Mesh &target,
                                        const float4x4 &target_to_mesh)
{
  const int faces_num = mesh.faces_num;
  Array<float> distances(faces_num, FLT_MAX);
  const bke::BVHTreeFromMesh bvh = target.bvh_corner_tris();
  if (bvh.tree == nullptr) {
    return distances;
  }
  const float4x4 mesh_to_target = math::invert(target_to_mesh);
  /* Distances are measured in the target's space and scaled back by the average scale. */
  const float3 scale = math::to_scale(target_to_mesh);
  const float back = (std::abs(scale.x) + std::abs(scale.y) + std::abs(scale.z)) / 3.0f;
  const Span<float3> positions = mesh.vert_positions();
  const OffsetIndices<int> faces = mesh.faces();
  const Span<int> corner_verts = mesh.corner_verts();
  threading::parallel_for(IndexRange(faces_num), 256, [&](const IndexRange range) {
    for (const int f : range) {
      float3 center(0.0f);
      for (const int v : corner_verts.slice(faces[f])) {
        center += positions[v];
      }
      center /= float(faces[f].size());
      const float3 co = math::transform_point(mesh_to_target, center);
      BVHTreeNearest nearest;
      nearest.index = -1;
      nearest.dist_sq = FLT_MAX;
      BLI_bvhtree_find_nearest(bvh.tree,
                               co,
                               &nearest,
                               bvh.nearest_callback,
                               const_cast<bke::BVHTreeFromMesh *>(&bvh));
      if (nearest.index != -1) {
        distances[f] = std::sqrt(nearest.dist_sq) * back;
      }
    }
  });
  return distances;
}

}  // namespace blender::geometry
