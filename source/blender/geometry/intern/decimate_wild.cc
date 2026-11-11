/* SPDX-FileCopyrightText: 2026 Blender Authors
 * SPDX-FileCopyrightText: 2026 Evan Suma Rosenberg (MIT reference implementation)
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Native geometry-only adaptation of the MIT reference listed in
 * extern/wild_qem_reference/README.blender. See LICENSE.txt there for its notice.
 * This experimental backend does not implement the paper's texture rebaking pipeline. */

#include "BLI_disjoint_set.hh"
#include "BLI_kdtree.hh"
#include "decimate_backend_types.hh"
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <Eigen/LU>
#include <algorithm>
#include <array>
#include <cmath>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace blender::geometry::decimation {
namespace {
using Vec = Eigen::Vector3d;
using Quadric = Eigen::Matrix4d;
uint64_t key(int a, int b)
{
  if (a > b)
    std::swap(a, b);
  return uint64_t(uint32_t(a)) << 32 | uint32_t(b);
}
struct FaceHash {
  size_t operator()(const std::array<int, 3> &v) const
  {
    return size_t(v[0]) * 73856093u ^ size_t(v[1]) * 19349663u ^ size_t(v[2]) * 83492791u;
  }
};
std::array<int, 3> face_key(const int3 &t)
{
  std::array<int, 3> result{t.x, t.y, t.z};
  std::sort(result.begin(), result.end());
  return result;
}
struct Vertex {
  Vec position;
  Quadric quadric = Quadric::Zero();
  double mass = 0;
  std::unordered_set<int> faces, virtual_neighbors;
  unsigned int version = 0;
  bool active = true, locked = false;
};
struct Face {
  int3 vertices;
  bool active = true;
};
struct Candidate {
  int a, b;
  unsigned int va, vb;
  Vec position;
  double cost;
  bool operator<(const Candidate &other) const
  {
    if (cost != other.cost)
      return cost > other.cost;
    if (a != other.a)
      return a > other.a;
    return b > other.b;
  }
};

class WildMesh {
  const DecimateBackendParams &params;
  std::vector<Vertex> vertices;
  std::vector<Face> faces;
  std::unordered_map<uint64_t, std::unordered_set<int>> edges;
  std::unordered_map<std::array<int, 3>, int, FaceHash> face_ids;
  std::priority_queue<Candidate> queue;
  int face_count = 0;

  void link_face(int f)
  {
    const int3 t = faces[f].vertices;
    for (int k = 0; k < 3; k++) {
      vertices[t[k]].faces.insert(f);
      edges[key(t[k], t[(k + 1) % 3])].insert(f);
    }
    face_ids[face_key(t)] = f;
  }
  void unlink_face(int f)
  {
    const int3 t = faces[f].vertices;
    for (int k = 0; k < 3; k++) {
      vertices[t[k]].faces.erase(f);
      edges[key(t[k], t[(k + 1) % 3])].erase(f);
    }
    auto entry = face_ids.find(face_key(t));
    if (entry != face_ids.end() && entry->second == f)
      face_ids.erase(entry);
  }
  std::unordered_set<int> neighbors(int a) const
  {
    std::unordered_set<int> result = vertices[a].virtual_neighbors;
    for (int f : vertices[a].faces) {
      const int3 t = faces[f].vertices;
      for (int k = 0; k < 3; k++)
        if (t[k] != a)
          result.insert(t[k]);
    }
    return result;
  }
  Quadric boundary_quadric(int a, int b) const
  {
    const Vec p = vertices[a].position, s = vertices[b].position - p;
    const Vec t = p.cross(vertices[b].position);
    Eigen::Matrix<double, 3, 4> matrix;
    matrix << 0, -s.z(), s.y(), t.x(), s.z(), 0, -s.x(), t.y(), -s.y(), s.x(), 0, t.z();
    return 0.5 * matrix.transpose() * matrix;
  }
  double quality(const Vec &a, const Vec &b, const Vec &c) const
  {
    const double length = (b - a).squaredNorm() + (c - b).squaredNorm() + (a - c).squaredNorm();
    return length > 0 ? 3.4641016151377546 * (b - a).cross(c - a).norm() / length : 0;
  }
  bool valid(const Candidate &candidate) const
  {
    std::unordered_set<int> affected = vertices[candidate.a].faces;
    affected.insert(vertices[candidate.b].faces.begin(), vertices[candidate.b].faces.end());
    for (int f : affected) {
      const int3 t = faces[f].vertices;
      bool a = false, b = false;
      for (int k = 0; k < 3; k++) {
        a |= t[k] == candidate.a;
        b |= t[k] == candidate.b;
      }
      if (a && b)
        continue;
      const Vec old[3] = {vertices[t.x].position, vertices[t.y].position, vertices[t.z].position};
      Vec next[3] = {old[0], old[1], old[2]};
      for (int k = 0; k < 3; k++) {
        if (t[k] == candidate.a || t[k] == candidate.b)
          next[k] = candidate.position;
      }
      const Vec old_n = (old[1] - old[0]).cross(old[2] - old[0]);
      const Vec new_n = (next[1] - next[0]).cross(next[2] - next[0]);
      if (new_n.squaredNorm() == 0 || old_n.dot(new_n) <= 0)
        return false;
      const double q = quality(next[0], next[1], next[2]);
      if (q < 0.03 && q < quality(old[0], old[1], old[2]))
        return false;
    }
    return true;
  }
  void enqueue(int a, int b)
  {
    if (a > b)
      std::swap(a, b);
    if (a == b || !vertices[a].active || !vertices[b].active ||
        (vertices[a].locked && vertices[b].locked))
      return;
    auto physical = edges.find(key(a, b));
    if ((physical == edges.end() || physical->second.empty()) &&
        !vertices[a].virtual_neighbors.contains(b))
      return;
    Quadric q = vertices[a].quadric + vertices[b].quadric;
    std::unordered_set<uint64_t> seen;
    for (int v : {a, b}) {
      for (int n : neighbors(v)) {
        const uint64_t e = key(v, n);
        if (e == key(a, b) || !seen.insert(e).second)
          continue;
        auto found = edges.find(e);
        if (found != edges.end() && !found->second.empty() && found->second.size() != 2) {
          q += boundary_quadric(v, n);
        }
      }
    }
    const Vec middle = (vertices[a].position + vertices[b].position) * .5;
    Vec best = middle;
    auto error = [&](const Vec &p) {
      const Eigen::Vector4d h(p.x(), p.y(), p.z(), 1);
      return std::max(0.0, h.dot(q * h));
    };
    if (vertices[a].locked || vertices[b].locked) {
      best = vertices[vertices[a].locked ? a : b].position;
    }
    else {
      Eigen::FullPivLU<Eigen::Matrix3d> solver(q.topLeftCorner<3, 3>());
      if (solver.isInvertible()) {
        Vec solved = solver.solve(-q.topRightCorner<3, 1>());
        if (solved.allFinite() &&
            (solved - middle).norm() <=
                std::max((vertices[a].position - vertices[b].position).norm() * 8, 1e-12))
          best = solved;
      }
      for (const Vec &p : {vertices[a].position, vertices[b].position, middle}) {
        if (error(p) < error(best))
          best = p;
      }
    }
    const double cost = error(best);
    if (!best.allFinite() || !std::isfinite(cost))
      return;
    if (params.error_limit > 0 && std::sqrt(cost / std::max(vertices[a].mass + vertices[b].mass,
                                                            1e-30)) > params.error_limit)
      return;
    Candidate candidate{a, b, vertices[a].version, vertices[b].version, best, cost};
    if (valid(candidate))
      queue.push(candidate);
  }
  void rebuild_queue()
  {
    queue = {};
    for (size_t i = 0; i < vertices.size(); i++) {
      if (!vertices[i].active)
        continue;
      for (int n : neighbors(i))
        if (int(i) < n)
          enqueue(i, n);
    }
  }
  void collapse(const Candidate &candidate)
  {
    const int a = candidate.a, b = candidate.b;
    std::unordered_set<int> affected_faces = vertices[a].faces;
    affected_faces.insert(vertices[b].faces.begin(), vertices[b].faces.end());
    std::unordered_set<int> affected_vertices;
    for (int f : affected_faces) {
      for (int k = 0; k < 3; k++)
        affected_vertices.insert(faces[f].vertices[k]);
      unlink_face(f);
    }
    vertices[a].position = candidate.position;
    vertices[a].quadric += vertices[b].quadric;
    vertices[a].mass += vertices[b].mass;
    vertices[a].locked |= vertices[b].locked;
    vertices[b].active = false;
    for (int n : vertices[b].virtual_neighbors) {
      vertices[n].virtual_neighbors.erase(b);
      if (n != a && vertices[n].active) {
        vertices[n].virtual_neighbors.insert(a);
        vertices[a].virtual_neighbors.insert(n);
        affected_vertices.insert(n);
      }
    }
    vertices[a].virtual_neighbors.erase(a);
    vertices[a].virtual_neighbors.erase(b);
    vertices[b].virtual_neighbors.clear();
    std::vector<int> ordered(affected_faces.begin(), affected_faces.end());
    std::sort(ordered.begin(), ordered.end());
    for (int f : ordered) {
      int3 &t = faces[f].vertices;
      for (int k = 0; k < 3; k++)
        if (t[k] == b)
          t[k] = a;
      if (t.x == t.y || t.y == t.z || t.z == t.x || face_ids.contains(face_key(t))) {
        faces[f].active = false;
        face_count--;
      }
      else
        link_face(f);
    }
    for (int v : affected_vertices)
      vertices[v].version++;
    for (int v : affected_vertices) {
      if (!vertices[v].active)
        continue;
      for (int n : neighbors(v))
        enqueue(v, n);
    }
  }

 public:
  WildMesh(const TriangleInput &input, const DecimateBackendParams &settings) : params(settings)
  {
    vertices.resize(input.positions.size());
    for (size_t i = 0; i < vertices.size(); i++) {
      const float3 p = input.positions[i];
      vertices[i].position = Vec(p.x, p.y, p.z);
      vertices[i].locked = input.locked[i];
    }
    for (const int3 &t : input.triangles) {
      const int f = faces.size();
      faces.push_back({t, true});
      link_face(f);
      Vec normal = (vertices[t.y].position - vertices[t.x].position)
                       .cross(vertices[t.z].position - vertices[t.x].position);
      const double area = normal.norm() * .5;
      if (area <= 0)
        continue;
      normal.normalize();
      Eigen::Vector4d plane(
          normal.x(), normal.y(), normal.z(), -normal.dot(vertices[t.x].position));
      const Quadric q = (area / 3) * plane * plane.transpose();
      for (int k = 0; k < 3; k++) {
        vertices[t[k]].quadric += q;
        vertices[t[k]].mass += area / 3;
      }
    }
    face_count = faces.size();
    if (params.virtual_distance > 0) {
      DisjointSet<int> components(vertices.size());
      for (const int3 &t : input.triangles) {
        components.join(t.x, t.y);
        components.join(t.y, t.z);
      }
      KDTree_3d *tree = kdtree_3d_new(vertices.size());
      for (size_t i = 0; i < vertices.size(); i++)
        kdtree_3d_insert(tree, i, input.positions[i]);
      kdtree_3d_balance(tree);
      for (size_t i = 0; i < vertices.size(); i++) {
        KDTreeNearest_3d nearest[24];
        const int count = kdtree_3d_find_nearest_n(tree, input.positions[i], nearest, 24);
        int added = 0;
        for (int j = 0; j < count && added < 8; j++) {
          const int other = nearest[j].index;
          if (nearest[j].dist <= params.virtual_distance &&
              components.find_root(i) != components.find_root(other))
          {
            vertices[i].virtual_neighbors.insert(other);
            vertices[other].virtual_neighbors.insert(i);
            added++;
          }
        }
      }
      kdtree_3d_free(tree);
    }
  }
  TriangleResult run()
  {
    rebuild_queue();
    while (face_count > params.target_faces && !queue.empty()) {
      const Candidate candidate = queue.top();
      queue.pop();
      if (!vertices[candidate.a].active || !vertices[candidate.b].active ||
          vertices[candidate.a].version != candidate.va ||
          vertices[candidate.b].version != candidate.vb)
        continue;
      if (!valid(candidate))
        continue;
      collapse(candidate);
      if (queue.size() > std::max<size_t>(edges.size() * 8, 10000))
        rebuild_queue();
    }
    TriangleResult result;
    std::vector<int> remap(vertices.size(), -1);
    for (const Face &face : faces) {
      if (!face.active)
        continue;
      int3 t;
      for (int k = 0; k < 3; k++) {
        const int v = face.vertices[k];
        if (remap[v] < 0) {
          remap[v] = result.positions.size();
          const Vec p = vertices[v].position;
          result.positions.emplace_back(p.x(), p.y(), p.z());
        }
        t[k] = remap[v];
      }
      result.triangles.push_back(t);
    }
    result.success = true;
    return result;
  }
};
}  // namespace

TriangleResult simplify_wild(const TriangleInput &input, const DecimateBackendParams &params)
{
  return WildMesh(input, params).run();
}
}  // namespace blender::geometry::decimation
