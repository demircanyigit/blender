/* SPDX-FileCopyrightText: 2026 Blender Authors
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "decimate_backend_types.hh"

#ifdef WITH_DECIMATE_CGAL
/* This adapter uses double coordinates and does not need exact arithmetic libraries. */
#  define CGAL_DO_NOT_USE_BOOST_MP
#  define CGAL_NO_GMP 1
#  define CGAL_NO_MPFR 1
#  include <CGAL/Simple_cartesian.h>
#  include <CGAL/Surface_mesh.h>
#  include <CGAL/Surface_mesh_simplification/Edge_collapse_visitor_base.h>
#  include <CGAL/Surface_mesh_simplification/Policies/Edge_collapse/GarlandHeckbert_plane_and_line_policies.h>
#  include <CGAL/Surface_mesh_simplification/edge_collapse.h>
#  include <CGAL/boost/graph/helpers.h>
#  include <exception>
#  include <optional>
#endif

namespace blender::geometry::decimation {
#ifdef WITH_DECIMATE_CGAL
namespace {
using Kernel = CGAL::Simple_cartesian<double>;
using Surface = CGAL::Surface_mesh<Kernel::Point_3>;
using Vertex = Surface::Vertex_index;
using VertexLocks = Surface::Property_map<Vertex, bool>;
namespace SMS = CGAL::Surface_mesh_simplification;

struct FaceCountStop {
  const Surface *surface;
  size_t target;
  template<class Cost, class Profile>
  bool operator()(const Cost &, const Profile &, size_t, size_t) const
  {
    return surface->number_of_faces() <= target;
  }
};

template<class Placement> struct LockedPlacement {
  Placement base;
  VertexLocks locked;
  template<class Profile>
  std::optional<typename Profile::Point> operator()(const Profile &profile) const
  {
    const bool a = locked[profile.v0()], b = locked[profile.v1()];
    if (a && b) {
      return std::nullopt;
    }
    if (a) {
      return profile.p0();
    }
    if (b) {
      return profile.p1();
    }
    return base(profile);
  }
};

struct LockVisitor : SMS::Edge_collapse_visitor_base<Surface> {
  VertexLocks locked;
  bool collapsing_locked = false;
  explicit LockVisitor(VertexLocks map) : locked(map) {}
  void OnCollapsing(const Profile &profile, const std::optional<Point> &)
  {
    collapsing_locked = locked[profile.v0()] || locked[profile.v1()];
  }
  void OnCollapsed(const Profile &, const vertex_descriptor &vertex)
  {
    locked[vertex] = collapsing_locked;
  }
};
}  // namespace
#endif

TriangleResult simplify_cgal(const TriangleInput &input, const DecimateBackendParams &params)
{
  TriangleResult result;
#ifdef WITH_DECIMATE_CGAL
  try {
    Surface surface;
    std::vector<Vertex> vertices;
    vertices.reserve(input.positions.size());
    VertexLocks locked = surface.add_property_map<Vertex, bool>("v:locked", false).first;
    for (size_t i = 0; i < input.positions.size(); i++) {
      const float3 p = input.positions[i];
      const Vertex vertex = surface.add_vertex(Kernel::Point_3(p.x, p.y, p.z));
      vertices.push_back(vertex);
      locked[vertex] = input.locked[i] != 0;
    }
    for (const int3 &tri : input.triangles) {
      if (surface.add_face(vertices[tri.x], vertices[tri.y], vertices[tri.z]) ==
          Surface::null_face())
      {
        result.message =
            "CGAL requires consistently oriented manifold triangles; input was left unchanged";
        return result;
      }
    }
    if (!CGAL::is_valid_polygon_mesh(surface) || !CGAL::is_triangle_mesh(surface)) {
      result.message = "CGAL input is not a valid triangle surface";
      return result;
    }
    auto constrained =
        surface.add_property_map<Surface::Edge_index, bool>("e:locked", false).first;
    for (const auto edge : surface.edges()) {
      const auto halfedge = surface.halfedge(edge);
      constrained[edge] = locked[surface.source(halfedge)] && locked[surface.target(halfedge)];
    }
    SMS::GarlandHeckbert_plane_and_line_policies<Surface, Kernel> policy(surface);
    LockedPlacement<decltype(policy.get_placement())> placement{policy.get_placement(), locked};
    LockVisitor visitor(locked);
    SMS::edge_collapse(surface,
                       FaceCountStop{&surface, size_t(params.target_faces)},
                       CGAL::parameters::get_cost(policy.get_cost())
                           .get_placement(placement)
                           .edge_is_constrained_map(constrained)
                           .visitor(visitor));
    std::vector<int> remap(surface.num_vertices(), -1);
    for (const auto face : surface.faces()) {
      int3 triangle;
      int k = 0;
      for (const Vertex vertex : CGAL::vertices_around_face(surface.halfedge(face), surface)) {
        if (k >= 3) {
          result.message = "CGAL produced a non-triangle face";
          return result;
        }
        if (remap[vertex.idx()] < 0) {
          const auto &p = surface.point(vertex);
          remap[vertex.idx()] = result.positions.size();
          result.positions.emplace_back(float(p.x()), float(p.y()), float(p.z()));
        }
        triangle[k++] = remap[vertex.idx()];
      }
      if (k == 3) {
        result.triangles.push_back(triangle);
      }
    }
    result.success = true;
  }
  catch (const std::exception &error) {
    result.message = std::string("CGAL: ") + error.what();
  }
#else
  result.message = "CGAL support was disabled in this build";
#endif
  return result;
}
}  // namespace blender::geometry::decimation
