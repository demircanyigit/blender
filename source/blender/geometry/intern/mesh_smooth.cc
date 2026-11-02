/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <algorithm>

#include "BLI_array.hh"
#include "BLI_index_mask.hh"
#include "BLI_math_vector.hh"
#include "BLI_offset_indices.hh"
#include "BLI_task.hh"
#include "BLI_virtual_array.hh"

#include "BKE_bvhutils.hh"
#include "BKE_lib_id.hh"
#include "BKE_mesh.hh"
#include "BKE_mesh_mapping.hh"

#include "DNA_mesh_types.h"

#include "GEO_mesh_smooth.hh"

namespace blender::geometry {

namespace {

/** The neighbors of every point, and the selected points that are allowed to move. */
struct MoveData {
  Array<int> vert_to_edge_offsets;
  Array<int> vert_to_edge_indices;
  GroupedSpan<int> vert_to_edge;
  Array<bool> pinned;
  Array<float> factors;
  IndexMaskMemory memory;
  IndexMask mask;
};

}  // namespace

static void calc_move_data(const Mesh &mesh,
                           const IndexMask &selection,
                           const VArray<float> &factors,
                           const VArray<bool> &hard_edges,
                           const bool preserve_boundary,
                           MoveData &data)
{
  const Span<int2> edges = mesh.edges();
  data.vert_to_edge = bke::mesh::build_vert_to_edge_map(
      edges, mesh.verts_num, data.vert_to_edge_offsets, data.vert_to_edge_indices);

  data.pinned = Array<bool>(mesh.verts_num, false);
  for (const int edge : edges.index_range()) {
    if (hard_edges[edge]) {
      data.pinned[edges[edge][0]] = true;
      data.pinned[edges[edge][1]] = true;
    }
  }
  if (preserve_boundary) {
    Array<int> edge_face_counts(mesh.edges_num, 0);
    for (const int edge : mesh.corner_edges()) {
      edge_face_counts[edge]++;
    }
    for (const int edge : edges.index_range()) {
      if (edge_face_counts[edge] == 1) {
        data.pinned[edges[edge][0]] = true;
        data.pinned[edges[edge][1]] = true;
      }
    }
  }

  data.factors = Array<float>(mesh.verts_num, 0.0f);
  selection.foreach_index(GrainSize(4096), [&](const int vert) {
    data.factors[vert] = std::clamp(factors[vert], 0.0f, 1.0f);
  });
  data.mask = IndexMask::from_predicate(selection, GrainSize(4096), data.memory, [&](const int i) {
    return !data.pinned[i] && data.factors[i] > 0.0f && !data.vert_to_edge[i].is_empty();
  });
}

static float3 neighbor_average(const Span<int2> edges,
                               const Span<int> vert_edges,
                               const Span<float3> positions,
                               const int vert)
{
  float3 sum(0.0f);
  for (const int edge : vert_edges) {
    sum += positions[bke::mesh::edge_other_vert(edges[edge], vert)];
  }
  return sum / float(vert_edges.size());
}

void mesh_smooth(Mesh &mesh,
                 const IndexMask &selection,
                 const VArray<float> &factors,
                 const VArray<bool> &hard_edges,
                 const MeshSmoothMethod method,
                 const MeshSmoothParams &params)
{
  MoveData data;
  calc_move_data(mesh, selection, factors, hard_edges, params.preserve_boundary, data);
  if (data.mask.is_empty() || params.iterations <= 0) {
    return;
  }

  const Span<int2> edges = mesh.edges();
  MutableSpan<float3> positions = mesh.vert_positions_for_write();
  Array<float3> previous(positions.as_span());

  const auto step = [&](const FunctionRef<float(float)> strength) {
    previous.as_mutable_span().copy_from(positions);
    data.mask.foreach_index(GrainSize(1024), [&](const int i) {
      const float3 average = neighbor_average(edges, data.vert_to_edge[i], previous, i);
      positions[i] = previous[i] + (average - previous[i]) * strength(data.factors[i]);
    });
  };

  for ([[maybe_unused]] const int iteration : IndexRange(params.iterations)) {
    if (method == MeshSmoothMethod::Average) {
      step([](const float factor) { return factor; });
    }
    else {
      /* Taubin smoothing: a shrinking step followed by a slightly larger growing step, which
       * removes bumps without making the whole mesh smaller. */
      step([](const float factor) { return factor * 0.5f; });
      step([](const float factor) {
        const float shrink = factor * 0.5f;
        return -shrink / (1.0f - 0.1f * shrink);
      });
    }
  }
  mesh.tag_positions_changed();
}

void mesh_relax(Mesh &mesh,
                const IndexMask &selection,
                const VArray<float> &factors,
                const VArray<bool> &hard_edges,
                const bool keep_on_surface,
                const MeshSmoothParams &params)
{
  MoveData data;
  calc_move_data(mesh, selection, factors, hard_edges, params.preserve_boundary, data);
  if (data.mask.is_empty() || params.iterations <= 0) {
    return;
  }

  /* The surface to stay on. The copy shares its data with the mesh until the mesh is changed. */
  Mesh *surface = keep_on_surface ? BKE_mesh_copy_for_eval(mesh) : nullptr;
  bke::BVHTreeFromMesh surface_tree;
  if (surface != nullptr) {
    surface_tree = surface->bvh_corner_tris();
  }

  const Span<int2> edges = mesh.edges();
  Array<float3> previous(mesh.verts_num);
  for ([[maybe_unused]] const int iteration : IndexRange(params.iterations)) {
    const Span<float3> normals = mesh.vert_normals();
    previous.as_mutable_span().copy_from(mesh.vert_positions());
    MutableSpan<float3> positions = mesh.vert_positions_for_write();
    data.mask.foreach_index(GrainSize(256), [&](const int i) {
      const float3 average = neighbor_average(edges, data.vert_to_edge[i], previous, i);
      /* Only slide along the surface, so that the shape stays the same. */
      float3 offset = average - previous[i];
      offset -= normals[i] * math::dot(offset, normals[i]);
      float3 position = previous[i] + offset * data.factors[i];
      if (surface_tree.tree != nullptr) {
        BVHTreeNearest nearest;
        nearest.dist_sq = FLT_MAX;
        BLI_bvhtree_find_nearest(surface_tree.tree,
                                 position,
                                 &nearest,
                                 surface_tree.nearest_callback,
                                 const_cast<bke::BVHTreeFromMesh *>(&surface_tree));
        if (nearest.index != -1) {
          position = nearest.co;
        }
      }
      positions[i] = position;
    });
    mesh.tag_positions_changed();
  }

  if (surface != nullptr) {
    BKE_id_free(nullptr, surface);
  }
}

}  // namespace blender::geometry
