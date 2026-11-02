/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <algorithm>
#include <array>
#include <cmath>

#include "BLI_array.hh"
#include "BLI_bounds_types.hh"
#include "BLI_kdtree.hh"
#include "BLI_math_vector.hh"
#include "BLI_task.hh"
#include "BLI_utildefines.h"
#include "BLI_vector.hh"

#include "BKE_attribute.hh"
#include "BKE_mesh.hh"

#include "DNA_mesh_types.h"

#include "GEO_mesh_voronoi_fracture.hh"

#include "mesh_boolean_manifold.hh"

namespace blender::geometry {

namespace {

struct CellFace {
  Vector<double3, 8> verts;
  /* Index in #CellBuilder::normals. */
  int plane;
};

/**
 * A convex polyhedron that is made smaller by cutting off the part in front of a plane.
 */
class CellBuilder {
 public:
  Vector<CellFace, 16> faces;
  Vector<double3, 16> normals;

  CellBuilder(const double3 &min, const double3 &max)
  {
    const auto corner = [&](const int x, const int y, const int z) {
      return double3(x ? max.x : min.x, y ? max.y : min.y, z ? max.z : min.z);
    };
    this->add_box_face({-1, 0, 0}, {corner(0, 0, 0), corner(0, 0, 1), corner(0, 1, 1), corner(0, 1, 0)});
    this->add_box_face({1, 0, 0}, {corner(1, 0, 0), corner(1, 1, 0), corner(1, 1, 1), corner(1, 0, 1)});
    this->add_box_face({0, -1, 0}, {corner(0, 0, 0), corner(1, 0, 0), corner(1, 0, 1), corner(0, 0, 1)});
    this->add_box_face({0, 1, 0}, {corner(0, 1, 0), corner(0, 1, 1), corner(1, 1, 1), corner(1, 1, 0)});
    this->add_box_face({0, 0, -1}, {corner(0, 0, 0), corner(0, 1, 0), corner(1, 1, 0), corner(1, 0, 0)});
    this->add_box_face({0, 0, 1}, {corner(0, 0, 1), corner(1, 0, 1), corner(1, 1, 1), corner(0, 1, 1)});
  }

  /** Keep the part where `dot(normal, position) <= offset`. Returns false if nothing changed. */
  bool clip(const double3 &normal, const double offset, const double epsilon)
  {
    Vector<double3, 16> cap;
    Vector<CellFace, 16> new_faces;
    bool changed = false;
    for (const CellFace &face : faces) {
      Vector<double3, 8> kept;
      bool face_changed = false;
      for (const int i : face.verts.index_range()) {
        const double3 &a = face.verts[i];
        const double3 &b = face.verts[(i + 1) % face.verts.size()];
        const double dist_a = math::dot(normal, a) - offset;
        const double dist_b = math::dot(normal, b) - offset;
        const bool inside_a = dist_a <= epsilon;
        const bool inside_b = dist_b <= epsilon;
        if (inside_a) {
          kept.append(a);
        }
        else {
          face_changed = true;
        }
        if (inside_a != inside_b) {
          const double factor = std::clamp(dist_a / (dist_a - dist_b), 0.0, 1.0);
          const double3 crossing = a + (b - a) * factor;
          kept.append(crossing);
          cap.append(crossing);
        }
      }
      changed |= face_changed;
      if (kept.size() >= 3) {
        new_faces.append({std::move(kept), face.plane});
      }
    }
    if (!changed) {
      return false;
    }
    faces = std::move(new_faces);
    this->add_cap(cap, normal, epsilon);
    return true;
  }

  double max_distance(const double3 &position) const
  {
    double result = 0.0;
    for (const CellFace &face : faces) {
      for (const double3 &vert : face.verts) {
        result = std::max(result, math::distance_squared(vert, position));
      }
    }
    return std::sqrt(result);
  }

 private:
  void add_box_face(const double3 &normal, const std::array<double3, 4> &verts)
  {
    CellFace face;
    face.verts.extend(verts.data(), int64_t(verts.size()));
    face.plane = normals.append_and_get_index(normal);
    faces.append(std::move(face));
  }

  /** Close the hole left by a cut. The crossing points are only known in a random order. */
  void add_cap(Span<double3> points, const double3 &normal, const double epsilon)
  {
    if (points.size() < 3) {
      return;
    }
    double3 center(0.0);
    for (const double3 &point : points) {
      center += point;
    }
    center /= double(points.size());

    /* Any direction in the plane works, so use the one that is the farthest from the normal. */
    const double3 helper = std::abs(normal.x) < 0.5 ? double3(1, 0, 0) : double3(0, 1, 0);
    const double3 axis_u = math::normalize(math::cross(normal, helper));
    const double3 axis_v = math::cross(normal, axis_u);

    Array<std::pair<double, double3>> sorted(points.size());
    for (const int i : points.index_range()) {
      const double3 offset = points[i] - center;
      sorted[i] = {std::atan2(math::dot(offset, axis_v), math::dot(offset, axis_u)), points[i]};
    }
    std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) {
      return a.first < b.first;
    });

    CellFace face;
    for (const auto &[angle, point] : sorted) {
      if (face.verts.is_empty() || math::distance(face.verts.last(), point) > epsilon) {
        face.verts.append(point);
      }
    }
    if (face.verts.size() > 1 && math::distance(face.verts.last(), face.verts.first()) <= epsilon)
    {
      face.verts.pop_last();
    }
    if (face.verts.size() >= 3) {
      face.plane = normals.append_and_get_index(normal);
      faces.append(std::move(face));
    }
  }
};

}  // namespace

/**
 * Find the Voronoi cell of one point: start with a box around the mesh, and cut it with the plane
 * halfway to the other points, starting at the closest one. Once a point is more than twice the
 * size of the cell away, it can't change the cell any more, and so can't any point after it.
 */
static boolean::FractureCell calc_cell(const int index,
                                       const Span<float3> points,
                                       const KDTree_3d &tree,
                                       const double3 &box_min,
                                       const double3 &box_max,
                                       const double gap,
                                       const double epsilon)
{
  boolean::FractureCell result;
  result.point_index = index;

  const double3 point(points[index]);
  CellBuilder cell(box_min, box_max);
  double max_distance = cell.max_distance(point);

  int checked = 0;
  int query_size = std::min(32, int(points.size()));
  Array<KDTreeNearest_3d> nearest;
  bool done = false;
  while (!done) {
    nearest.reinitialize(query_size);
    const int found = kdtree_find_nearest_n<float3>(
        &tree, points[index], nearest.data(), uint(query_size));
    for (const int i : IndexRange(checked, found - checked)) {
      const int other_index = nearest[i].index;
      if (other_index == index) {
        continue;
      }
      const double3 to_other = double3(points[other_index]) - point;
      const double distance = math::length(to_other);
      if (distance <= epsilon) {
        /* Points in the same place would have the same cell, so only the first one gets it. */
        if (other_index < index) {
          return result;
        }
        continue;
      }
      const double half_distance = (distance - gap) * 0.5;
      if (half_distance >= max_distance) {
        done = true;
        break;
      }
      const double3 normal = to_other / distance;
      if (cell.clip(normal, math::dot(normal, point) + half_distance, epsilon)) {
        if (cell.faces.is_empty()) {
          return result;
        }
        max_distance = cell.max_distance(point);
      }
    }
    checked = found;
    if (found < query_size || query_size == points.size()) {
      break;
    }
    query_size = int(std::min(int64_t(query_size) * 2, points.size()));
  }

  for (const CellFace &face : cell.faces) {
    result.points.extend(face.verts);
    result.normals.append(cell.normals[face.plane]);
  }
  return result;
}

static Vector<boolean::FractureCell> calc_cells(const Mesh &mesh,
                                                const Span<float3> points,
                                                const float cut_gap)
{
  const Bounds<float3> bounds = *mesh.bounds_min_max();
  const double size = math::length(double3(bounds.max) - double3(bounds.min));
  /* Cuts of this box would look like part of the mesh, so it should not touch the mesh. */
  const double3 margin(size * 0.01 + 1e-4);
  const double3 box_min = double3(bounds.min) - margin;
  const double3 box_max = double3(bounds.max) + margin;
  const double epsilon = size * 1e-10;

  KDTree_3d *tree = kdtree_3d_new(points.size());
  for (const int i : points.index_range()) {
    kdtree_3d_insert(tree, i, points[i]);
  }
  kdtree_3d_balance(tree);

  Array<boolean::FractureCell> cells(points.size());
  threading::parallel_for(points.index_range(), 16, [&](const IndexRange range) {
    for (const int i : range) {
      cells[i] = calc_cell(i, points, *tree, box_min, box_max, cut_gap, epsilon);
    }
  });
  kdtree_3d_free(tree);

  Vector<boolean::FractureCell> result;
  for (boolean::FractureCell &cell : cells) {
    if (!cell.points.is_empty()) {
      result.append(std::move(cell));
    }
  }
  return result;
}

Mesh *mesh_voronoi_fracture(const Mesh &mesh,
                            const Span<float3> cell_points,
                            const MeshVoronoiFractureParams &params,
                            MeshVoronoiFractureError *r_error)
{
  *r_error = MeshVoronoiFractureError::NoError;
#ifdef WITH_MANIFOLD
  const Vector<boolean::FractureCell> cells = calc_cells(mesh, cell_points, params.cut_gap);

  Array<int> vert_pieces;
  Vector<int> piece_cells;
  Array<bool> interior_faces;
  boolean::BooleanError error = boolean::BooleanError::NoError;
  Mesh *result = boolean::mesh_fracture_manifold(
      mesh, cells, params.separate_islands, vert_pieces, piece_cells, interior_faces, &error);
  if (result == nullptr) {
    *r_error = error == boolean::BooleanError::NonManifold ?
                   MeshVoronoiFractureError::NonManifold :
                   MeshVoronoiFractureError::UnknownError;
    return nullptr;
  }

  bke::MutableAttributeAccessor attributes = result->attributes_for_write();
  if (params.piece_index_id) {
    bke::SpanAttributeWriter<int> attribute = attributes.lookup_or_add_for_write_only_span<int>(
        *params.piece_index_id, bke::AttrDomain::Point);
    attribute.span.copy_from(vert_pieces);
    attribute.finish();
  }
  if (params.cell_index_id) {
    bke::SpanAttributeWriter<int> attribute = attributes.lookup_or_add_for_write_only_span<int>(
        *params.cell_index_id, bke::AttrDomain::Point);
    threading::parallel_for(attribute.span.index_range(), 4096, [&](const IndexRange range) {
      for (const int i : range) {
        attribute.span[i] = piece_cells[vert_pieces[i]];
      }
    });
    attribute.finish();
  }
  if (params.interior_id) {
    bke::SpanAttributeWriter<bool> attribute = attributes.lookup_or_add_for_write_only_span<bool>(
        *params.interior_id, bke::AttrDomain::Face);
    attribute.span.copy_from(interior_faces);
    attribute.finish();
  }
  return result;
#else
  UNUSED_VARS(mesh, cell_points, params);
  *r_error = MeshVoronoiFractureError::SolverNotAvailable;
  return nullptr;
#endif
}

}  // namespace blender::geometry
