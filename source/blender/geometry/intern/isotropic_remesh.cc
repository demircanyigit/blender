/* SPDX-FileCopyrightText: 2024 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <algorithm>
#include <array>
#include <cfloat>
#include <exception>
#include <limits>
#include <vector>

#include "BLI_array.hh"
#include "BLI_index_mask.hh"
#include "BLI_kdopbvh.hh"
#include "BLI_math_base.h"
#include "BLI_math_geom.h"
#include "BLI_math_vector.hh"
#include "BLI_math_vector_types.hh"
#include "BLI_offset_indices.hh"
#include "BLI_string_ref.hh"
#include "BLI_task.hh"
#include "BLI_vector.hh"

#include "DNA_mesh_types.h"

#include "BKE_attribute.hh"
#include "BKE_bvhutils.hh"
#include "BKE_mesh.h"
#include "BKE_mesh.hh"
#include "BKE_mesh_sample.hh"

#include "GEO_isotropic_remesh.hh"

#include "pmp_mesh_conversion.hh"

/* Vendored Polygon Mesh Processing Library (extern/pmp). */
#include "pmp/algorithms/features.h"
#include "pmp/algorithms/remeshing.h"
#include "pmp/exceptions.h"
#include "pmp/surface_mesh.h"

namespace blender::geometry {

/* -------------------------------------------------------------------------- */
/** \name Input conversion (Blender #Mesh -> #pmp::SurfaceMesh)
 * \{ */

int build_surface_mesh(const Mesh &mesh,
                       const VArray<bool> &hard_edges,
                       const VArray<bool> &vert_selection,
                       pmp::SurfaceMesh &surface)
{
  const Span<float3> positions = mesh.vert_positions();
  const Span<int> corner_verts = mesh.corner_verts();
  const Span<int3> corner_tris = mesh.corner_tris();

  const auto to_point = [&](const int vert) {
    const float3 &p = positions[vert];
    return pmp::Point(p.x, p.y, p.z);
  };

  int detached_tris = 0;
  Array<pmp::Vertex> vert_map(positions.size());
  for (const int3 &tri : corner_tris) {
    const int3 verts(corner_verts[tri[0]], corner_verts[tri[1]], corner_verts[tri[2]]);
    /* Skip topologically degenerate triangles, which have a repeated vertex. */
    if (verts[0] == verts[1] || verts[1] == verts[2] || verts[0] == verts[2]) {
      continue;
    }
    for (const int i : IndexRange(3)) {
      if (!vert_map[verts[i]].is_valid()) {
        vert_map[verts[i]] = surface.add_vertex(to_point(verts[i]));
      }
    }
    try {
      surface.add_triangle(vert_map[verts[0]], vert_map[verts[1]], vert_map[verts[2]]);
    }
    catch (const pmp::TopologyException &) {
      detached_tris++;
      surface.add_triangle(surface.add_vertex(to_point(verts[0])),
                           surface.add_vertex(to_point(verts[1])),
                           surface.add_vertex(to_point(verts[2])));
    }
  }

  if (hard_edges) {
    auto vert_is_feature = surface.vertex_property<bool>("v:feature", false);
    auto edge_is_feature = surface.edge_property<bool>("e:feature", false);
    const Span<int2> edges = mesh.edges();
    for (const int edge : edges.index_range()) {
      if (!hard_edges[edge]) {
        continue;
      }
      const pmp::Vertex v0 = vert_map[edges[edge][0]];
      const pmp::Vertex v1 = vert_map[edges[edge][1]];
      if (!v0.is_valid() || !v1.is_valid()) {
        continue;
      }
      const pmp::Edge surface_edge = surface.find_edge(v0, v1);
      if (surface_edge.is_valid()) {
        edge_is_feature[surface_edge] = true;
        vert_is_feature[v0] = true;
        vert_is_feature[v1] = true;
      }
    }
  }

  if (vert_selection) {
    auto vert_is_selected = surface.vertex_property<bool>("v:selected", false);
    for (const int vert : positions.index_range()) {
      if (vert_map[vert].is_valid()) {
        vert_is_selected[vert_map[vert]] = vert_selection[vert];
      }
    }
  }
  return detached_tris;
}

static float mean_edge_length(const pmp::SurfaceMesh &surface)
{
  double sum = 0.0;
  for (const pmp::Edge edge : surface.edges()) {
    sum += pmp::distance(surface.position(surface.vertex(edge, 0)),
                         surface.position(surface.vertex(edge, 1)));
  }
  return surface.n_edges() > 0 ? float(sum / surface.n_edges()) : 0.0f;
}

/** \} */

/* -------------------------------------------------------------------------- */
/** \name Output conversion (#pmp::SurfaceMesh -> Blender #Mesh)
 * \{ */

Mesh *surface_mesh_to_mesh(pmp::SurfaceMesh &surface, const Mesh &src_mesh)
{
  surface.garbage_collection();

  Vector<float3> positions;
  Vector<std::array<int, 3>> tris;

  /* Only vertices used by a face are kept, so loose vertices never leak into the result. */
  Array<int> vert_map(surface.vertices_size(), -1);
  const auto output_index = [&](const pmp::Vertex vertex) -> int {
    if (vert_map[vertex.idx()] == -1) {
      vert_map[vertex.idx()] = positions.size();
      const pmp::Point &p = surface.position(vertex);
      positions.append(float3(p[0], p[1], p[2]));
    }
    return vert_map[vertex.idx()];
  };

  for (const pmp::Face face : surface.faces()) {
    std::array<int, 3> tri;
    int corner = 0;
    for (const pmp::Vertex vertex : surface.vertices(face)) {
      tri[corner++] = output_index(vertex);
    }
    tris.append(tri);
  }

  if (tris.is_empty() || positions.is_empty()) {
    return nullptr;
  }

  const int verts_num = positions.size();
  const int faces_num = tris.size();
  const int corners_num = faces_num * 3;

  Mesh *mesh = bke::mesh_new_no_attributes(verts_num, 0, faces_num, corners_num);
  BKE_mesh_copy_parameters_for_eval(mesh, &src_mesh);

  /* Uniform triangle fan: every face has exactly three corners. */
  MutableSpan<int> face_offsets = mesh->face_offsets_for_write();
  threading::parallel_for(IndexRange(faces_num + 1), 4096, [&](const IndexRange range) {
    for (const int face : range) {
      face_offsets[face] = face * 3;
    }
  });

  bke::MutableAttributeAccessor attributes = mesh->attributes_for_write();
  attributes.add<int>(".corner_vert", bke::AttrDomain::Corner, bke::AttributeInitConstruct());
  attributes.add<float3>("position", bke::AttrDomain::Point, bke::AttributeInitConstruct());

  MutableSpan<float3> mesh_positions = mesh->vert_positions_for_write();
  mesh_positions.copy_from(positions);

  MutableSpan<int> mesh_corner_verts = mesh->corner_verts_for_write();
  threading::parallel_for(IndexRange(faces_num), 4096, [&](const IndexRange range) {
    for (const int face : range) {
      mesh_corner_verts[face * 3 + 0] = tris[face][0];
      mesh_corner_verts[face * 3 + 1] = tris[face][1];
      mesh_corner_verts[face * 3 + 2] = tris[face][2];
    }
  });

  bke::mesh_calc_edges(*mesh, false, false);
  mesh->tag_positions_changed();

  return mesh;
}

/** \} */

/* -------------------------------------------------------------------------- */
/** \name Attribute transfer (nearest-surface resampling)
 * \{ */

static bool topology_attribute(const StringRef name)
{
  return name == "position" || name == ".corner_vert" || name == ".corner_edge" ||
         name == ".edge_verts";
}

/* Resample the input's point/face/corner attributes onto the remeshed result. For each output
 * vertex the nearest point on the original surface gives a source triangle and barycentric
 * weights; face attributes use the nearest source face at each output face centroid. Edge-domain
 * attributes have no meaningful mapping after a full retopology and are intentionally dropped. */
void transfer_attributes_from_surface(const Mesh &src_mesh,
                                      Mesh &dst_mesh,
                                      const bke::AttributeFilter &attribute_filter,
                                      const bool face_aware_corners)
{
  bool has_attributes = false;
  src_mesh.attributes().foreach_attribute([&](const bke::AttributeIter &iter) {
    has_attributes |= !topology_attribute(iter.name) && iter.domain != bke::AttrDomain::Edge &&
                      iter.data_type != bke::AttrType::String && !attribute_filter.allow_skip(iter.name);
  });
  if (!has_attributes) {
    return;
  }
  const Span<float3> src_positions = src_mesh.vert_positions();
  const Span<int> src_corner_verts = src_mesh.corner_verts();
  const Span<int3> src_corner_tris = src_mesh.corner_tris();
  const Span<int> src_tri_faces = src_mesh.corner_tri_faces();
  if (src_corner_tris.is_empty()) {
    return;
  }

  bke::BVHTreeFromMesh bvh = src_mesh.bvh_corner_tris();
  if (bvh.tree == nullptr) {
    return;
  }

  const Span<float3> dst_positions = dst_mesh.vert_positions();
  const OffsetIndices<int> dst_faces = dst_mesh.faces();
  const Span<int> dst_corner_verts = dst_mesh.corner_verts();
  const int dst_verts_num = dst_mesh.verts_num;
  const int dst_faces_num = dst_mesh.faces_num;
  const int dst_corners_num = dst_mesh.corners_num;

  /* Per output vertex: nearest source triangle and barycentric weights. */
  Array<int> vert_tri(dst_verts_num);
  Array<float3> vert_bary(dst_verts_num);
  threading::parallel_for(IndexRange(dst_verts_num), 2048, [&](const IndexRange range) {
    for (const int i : range) {
      BVHTreeNearest nearest;
      nearest.dist_sq = FLT_MAX;
      nearest.index = -1;
      BLI_bvhtree_find_nearest(
          bvh.tree, dst_positions[i], &nearest, bvh.nearest_callback, &bvh);
      if (nearest.index < 0) {
        vert_tri[i] = 0;
        vert_bary[i] = float3(1.0f, 0.0f, 0.0f);
        continue;
      }
      vert_tri[i] = nearest.index;
      const int3 &tri = src_corner_tris[nearest.index];
      float weights[3];
      interp_weights_tri_v3(weights,
                            src_positions[src_corner_verts[tri[0]]],
                            src_positions[src_corner_verts[tri[1]]],
                            src_positions[src_corner_verts[tri[2]]],
                            nearest.co);
      vert_bary[i] = float3(weights[0], weights[1], weights[2]);
    }
  });

  /* Per output face: nearest source triangle at the face centroid. */
  Array<int> face_tri(dst_faces_num);
  threading::parallel_for(IndexRange(dst_faces_num), 2048, [&](const IndexRange range) {
    for (const int face : range) {
      float3 centroid(0.0f);
      for (const int corner : dst_faces[face]) {
        centroid += dst_positions[dst_corner_verts[corner]];
      }
      centroid /= float(dst_faces[face].size());
      BVHTreeNearest nearest;
      nearest.dist_sq = FLT_MAX;
      nearest.index = -1;
      BLI_bvhtree_find_nearest(bvh.tree, centroid, &nearest, bvh.nearest_callback, &bvh);
      face_tri[face] = std::max(nearest.index, 0);
    }
  });

  /* Per output corner: reuse the owning vertex's nearest-surface result. */
  Array<int> corner_tri(dst_corners_num);
  Array<float3> corner_bary(dst_corners_num);
  threading::parallel_for(IndexRange(dst_corners_num), 4096, [&](const IndexRange range) {
    for (const int corner : range) {
      const int vert = dst_corner_verts[corner];
      corner_tri[corner] = vert_tri[vert];
      corner_bary[corner] = vert_bary[vert];
    }
  });

  if (face_aware_corners) {
    /* Query just inside the destination face to disambiguate UV/color seams at shared positions.
     * Sample the actual corner position on that chosen source triangle, not the biased query. */
    threading::parallel_for(IndexRange(dst_faces_num), 1024, [&](const IndexRange range) {
      for (const int face : range) {
        float3 center(0);
        for (const int corner : dst_faces[face]) {
          center += dst_positions[dst_corner_verts[corner]];
        }
        center /= float(dst_faces[face].size());
        for (const int corner : dst_faces[face]) {
          const float3 position = dst_positions[dst_corner_verts[corner]];
          const float3 query = math::interpolate(position, center, 1e-4f);
          BVHTreeNearest nearest;
          nearest.dist_sq = FLT_MAX;
          nearest.index = -1;
          BLI_bvhtree_find_nearest(bvh.tree, query, &nearest, bvh.nearest_callback, &bvh);
          if (nearest.index < 0) {
            continue;
          }
          const int3 tri = src_corner_tris[nearest.index];
          const float3 a = src_positions[src_corner_verts[tri.x]];
          const float3 b = src_positions[src_corner_verts[tri.y]];
          const float3 c = src_positions[src_corner_verts[tri.z]];
          float closest[3], weights[3];
          closest_on_tri_to_point_v3(closest, position, a, b, c);
          interp_weights_tri_v3(weights, a, b, c, closest);
          corner_tri[corner] = nearest.index;
          corner_bary[corner] = float3(weights);
        }
      }
    });
  }

  const bke::AttributeAccessor src_attributes = src_mesh.attributes();
  bke::MutableAttributeAccessor dst_attributes = dst_mesh.attributes_for_write();

  const IndexMask verts_mask(dst_verts_num);
  const IndexMask faces_mask(dst_faces_num);
  const IndexMask corners_mask(dst_corners_num);

  src_attributes.foreach_attribute([&](const bke::AttributeIter &iter) {
    if (topology_attribute(iter.name)) {
      return;
    }
    /* Strings are the only attribute type nearest-surface sampling cannot handle. */
    if (iter.data_type == bke::AttrType::String) {
      return;
    }
    /* Edge topology is fully rebuilt, so edge attributes cannot be mapped. */
    if (iter.domain == bke::AttrDomain::Edge) {
      return;
    }
    if (attribute_filter.allow_skip(iter.name)) {
      return;
    }
    const bke::GAttributeReader src = iter.get();
    if (!src) {
      return;
    }

    switch (iter.domain) {
      case bke::AttrDomain::Point: {
        bke::GSpanAttributeWriter dst = dst_attributes.lookup_or_add_for_write_only_span(
            iter.name, bke::AttrDomain::Point, iter.data_type);
        if (!dst) {
          return;
        }
        bke::mesh_surface_sample::sample_point_attribute(
            src_corner_verts, src_corner_tris, vert_tri, vert_bary, src.varray, verts_mask, dst.span);
        dst.finish();
        break;
      }
      case bke::AttrDomain::Face: {
        bke::GSpanAttributeWriter dst = dst_attributes.lookup_or_add_for_write_only_span(
            iter.name, bke::AttrDomain::Face, iter.data_type);
        if (!dst) {
          return;
        }
        bke::mesh_surface_sample::sample_face_attribute(
            src_tri_faces, face_tri, src.varray, faces_mask, dst.span);
        dst.finish();
        break;
      }
      case bke::AttrDomain::Corner: {
        bke::GSpanAttributeWriter dst = dst_attributes.lookup_or_add_for_write_only_span(
            iter.name, bke::AttrDomain::Corner, iter.data_type);
        if (!dst) {
          return;
        }
        bke::mesh_surface_sample::sample_corner_attribute(
            src_corner_tris, corner_tri, corner_bary, src.varray, corners_mask, dst.span);
        dst.finish();
        break;
      }
      default:
        break;
    }
  });
}

/** \} */

/* -------------------------------------------------------------------------- */
/** \name Public entry point
 * \{ */

Mesh *isotropic_remesh(const Mesh &mesh,
                       const IsotropicRemeshParams &params,
                       const bke::AttributeFilter &attribute_filter,
                       const std::function<bool(float progress)> &progress_fn,
                       int *r_detached_tris)
{
  pmp::SurfaceMesh surface;
  const int detached_tris = build_surface_mesh(mesh, params.hard_edges, {}, surface);
  if (r_detached_tris != nullptr) {
    *r_detached_tris = detached_tris;
  }
  if (surface.n_faces() == 0) {
    return nullptr;
  }

  if (params.preserve_sharp_edges) {
    pmp::detect_features(surface, RAD2DEGF(std::max(params.sharp_angle, 1.0e-4f)));
  }
  if (params.preserve_boundary) {
    pmp::detect_boundary(surface);
  }

  const unsigned int iterations = uint(std::max(params.iterations, 0));

  pmp::RemeshingProgress iteration_callback;
  if (progress_fn) {
    iteration_callback = [&](const uint current, const uint total) -> bool {
      return progress_fn(total > 0 ? float(current) / float(total) : 1.0f);
    };
  }

  try {
    if (params.mode == IsotropicRemeshMode::Adaptive) {
      /* Zero lengths would never stop splitting, so keep the range positive and ordered. */
      const float max_length = std::max(params.max_edge_length, 1.0e-5f);
      const float min_length = std::clamp(params.min_edge_length, 1.0e-5f, max_length);
      pmp::adaptive_remeshing(surface,
                              min_length,
                              max_length,
                              std::max(params.adaptive_tolerance, 1.0e-7f),
                              iterations,
                              true,
                              iteration_callback);
    }
    else {
      const float target_length = params.target_edge_length > 0.0f ? params.target_edge_length :
                                                                     mean_edge_length(surface);
      /* The remesher settles about 10% below the length it is given. Compensate, so that edges
       * average the requested length. */
      const float length_scale = 1.1f;
      pmp::uniform_remeshing(
          surface, target_length * length_scale, iterations, true, iteration_callback);
    }
  }
  catch (const std::exception &) {
    return nullptr;
  }

  Mesh *result = surface_mesh_to_mesh(surface, mesh);
  if (result == nullptr) {
    return nullptr;
  }

  if (params.transfer_attributes) {
    transfer_attributes_from_surface(mesh, *result, attribute_filter);
  }

  return result;
}

/** \} */

}  // namespace blender::geometry
