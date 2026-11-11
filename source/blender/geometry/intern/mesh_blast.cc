/* SPDX-FileCopyrightText: 2026 Blender Authors
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "BKE_attribute.hh"
#include "BKE_mesh.hh"
#include "BLI_array.hh"
#include "BLI_disjoint_set.hh"
#include "BLI_math_vector.hh"
#include "DNA_mesh_types.h"
#include "GEO_mesh_blast.hh"
#include "GEO_mesh_copy_selection.hh"
#include <algorithm>
#include <cmath>
#include <exception>
#include <vector>

namespace blender::geometry {
namespace {
struct Island {
  double3 minimum, maximum, origin;
  double measure = 0;
  double correction = 0;
  int faces = 0;
  bool volume_valid = true;
  void accumulate(const double value)
  {
    const double adjusted = value - correction;
    const double next = measure + adjusted;
    correction = (next - measure) - adjusted;
    measure = next;
  }
};
}  // namespace

Mesh *mesh_blast(const Mesh &mesh, const MeshBlastParams &params, std::string &message)
{
  if (mesh.verts_num == 0)
    return nullptr;
  if (!std::isfinite(params.threshold) || params.threshold < 0 || params.keep_count < 0) {
    message = "Blast threshold and island count must be finite and non-negative";
    return nullptr;
  }
  if (params.metric < BlastMetric::FaceCount || params.metric > BlastMetric::BoundsSize ||
      params.filter < BlastFilter::Threshold || params.filter > BlastFilter::ThresholdAndLargest)
  {
    message = "Unsupported Blast measure or filter";
    return nullptr;
  }
  try {
    const Span<float3> positions = mesh.vert_positions();
    const Span<int2> edges = mesh.edges();
    const OffsetIndices<int> faces = mesh.faces();
    const Span<int> corners = mesh.corner_verts();
    DisjointSet<int> components(mesh.verts_num);
    for (const int2 &edge : edges)
      components.join(edge.x, edge.y);
    Array<int> root_to_island(mesh.verts_num, -1);
    Array<int> vertex_island(mesh.verts_num);
    std::vector<Island> islands;
    for (const int vertex : positions.index_range()) {
      const float3 point = positions[vertex];
      if (params.metric != BlastMetric::FaceCount &&
          (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)))
      {
        message = "Input has non-finite coordinates; choose Face Count or repair the input";
        return nullptr;
      }
      const int root = components.find_root(vertex);
      int &index = root_to_island[root];
      if (index == -1) {
        index = int(islands.size());
        islands.push_back({double3(point), double3(point), double3(point)});
      }
      vertex_island[vertex] = index;
      Island &island = islands[index];
      if (params.metric == BlastMetric::BoundsSize) {
        for (int axis = 0; axis < 3; axis++) {
          island.minimum[axis] = std::min(island.minimum[axis], double(point[axis]));
          island.maximum[axis] = std::max(island.maximum[axis], double(point[axis]));
        }
      }
    }
    const bool volume = params.metric == BlastMetric::Volume;
    Array<int> edge_users, edge_orientation;
    if (volume) {
      edge_users.reinitialize(mesh.edges_num);
      edge_users.fill(0);
      edge_orientation.reinitialize(mesh.edges_num);
      edge_orientation.fill(0);
    }
    const Span<int> corner_edges = mesh.corner_edges();
    for (const int face : faces.index_range()) {
      const IndexRange range = faces[face];
      if (range.is_empty())
        continue;
      Island &island = islands[vertex_island[corners[range.first()]]];
      island.faces++;
      if (volume) {
        for (const int corner : range) {
          const int edge = corner_edges[corner];
          edge_users[edge]++;
          edge_orientation[edge] += corners[corner] == edges[edge].x ? 1 : -1;
        }
      }
    }
    if (params.metric == BlastMetric::Area || volume) {
      for (const int3 &tri : mesh.corner_tris()) {
        Island &island = islands[vertex_island[corners[tri.x]]];
        const double3 a = double3(positions[corners[tri.x]]);
        const double3 b = double3(positions[corners[tri.y]]);
        const double3 c = double3(positions[corners[tri.z]]);
        island.accumulate(volume ? math::dot(a - island.origin,
                                             math::cross(b - island.origin, c - island.origin)) /
                                       6.0 :
                                   math::length(math::cross(b - a, c - a)) * 0.5);
      }
    }
    if (volume) {
      for (const int edge : edges.index_range()) {
        if (edge_users[edge] != 2 || edge_orientation[edge] != 0) {
          islands[vertex_island[edges[edge].x]].volume_valid = false;
        }
      }
    }
    std::vector<int> candidates;
    Array<bool> kept(int64_t(islands.size()), false);
    int protected_count = 0;
    for (int index = 0; index < int(islands.size()); index++) {
      Island &island = islands[index];
      switch (params.metric) {
        case BlastMetric::FaceCount:
          island.measure = island.faces;
          break;
        case BlastMetric::Area:
          break;
        case BlastMetric::Volume:
          island.measure = std::abs(island.measure);
          break;
        case BlastMetric::BoundsSize: {
          const double3 size = island.maximum - island.minimum;
          island.measure = std::max({size.x, size.y, size.z});
          break;
        }
      }
      if (volume && (!island.volume_valid || island.faces == 0)) {
        kept[index] = true;
        protected_count++;
        continue;
      }
      if (params.filter == BlastFilter::Largest || island.measure >= params.threshold) {
        candidates.push_back(index);
      }
    }
    int count = int(candidates.size());
    if (params.filter != BlastFilter::Threshold) {
      count = std::min(count, params.keep_count);
      /* A total ordering makes ties deterministic by the first vertex of the island. */
      std::partial_sort(candidates.begin(),
                        candidates.begin() + count,
                        candidates.end(),
                        [&](const int a, const int b) {
                          return islands[a].measure == islands[b].measure ?
                                     a < b :
                                     islands[a].measure > islands[b].measure;
                        });
    }
    for (int i = 0; i < count; i++)
      kept[candidates[i]] = true;
    Array<bool> keep_vertices(mesh.verts_num);
    for (const int vertex : keep_vertices.index_range()) {
      const int index = vertex_island[vertex];
      const bool protected_island = volume &&
                                    (!islands[index].volume_valid || islands[index].faces == 0);
      keep_vertices[vertex] = protected_island || (kept[index] != params.invert);
    }
    if (protected_count) {
      message = "Volume filtering preserved " + std::to_string(protected_count) +
                " open, non-manifold, inconsistently oriented or face-less islands";
    }
    const std::optional<Mesh *> result = mesh_copy_selection(
        mesh, VArray<bool>::from_span(keep_vertices), bke::AttrDomain::Point);
    if (!result)
      return nullptr;
    if (*result)
      return *result;
    Mesh *empty = BKE_mesh_new_nomain(0, 0, 0, 0);
    BKE_mesh_copy_parameters_for_eval(empty, &mesh);
    return empty;
  }
  catch (const std::exception &error) {
    message = std::string("Blast: ") + error.what();
    return nullptr;
  }
}
}  // namespace blender::geometry
