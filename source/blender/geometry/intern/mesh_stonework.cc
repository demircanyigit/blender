/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup geo
 *
 * Stonework: whole stones stacked into a mesh, one at a time, the way a mason builds a wall.
 *
 * The space to fill is cut into a grid of small cells. Each column of cells has a floor (the
 * lowest cell inside the space), a ceiling, and a current top, where the stones placed so far end.
 * For every new stone a few stones are tried in a few turns. Each is dropped onto the tops at
 * every spot near the lowest part of the wall, and the spot is scored: low is good, gaps under
 * the stone are bad, a top level with its neighbors is good. Stones that would tip over, or that
 * do not fit under the ceiling, are skipped. When nothing fits any more, smaller stones fill
 * what is left.
 */

#include <algorithm>
#include <climits>
#include <cmath>
#include <memory>
#include <queue>

#include "BLI_array.hh"
#include "BLI_color.hh"
#include "BLI_map.hh"
#include "BLI_math_base.h"
#include "BLI_math_base.hh"
#include "BLI_math_color.h"
#include "BLI_memory_counter.hh"
#include "BLI_math_matrix.hh"
#include "BLI_math_vector.hh"
#include "BLI_noise.hh"
#include "BLI_rand.hh"
#include "BLI_task.hh"
#include "BLI_utildefines.h"
#include "BLI_vector.hh"

#include "DNA_mesh_types.h"

#include "BKE_attribute.hh"
#include "BKE_geometry_set.hh"
#include "BKE_lib_id.hh"
#include "BKE_mesh.hh"
#include "BKE_mesh_remesh_voxel.hh"

#include "GEO_isotropic_remesh.hh"
#include "GEO_join_geometries.hh"
#include "GEO_mesh_boolean.hh"
#include "GEO_mesh_divide.hh"
#include "GEO_mesh_primitive_cuboid.hh"
#include "GEO_mesh_sim_cache.hh"
#include "GEO_mesh_stonework.hh"

namespace blender::geometry {

namespace stonework {

struct Rng {
  RandomNumberGenerator rng;
  explicit Rng(const uint32_t seed) : rng(seed) {}
  float f()
  {
    return rng.get_float();
  }
  float s()
  {
    return rng.get_float() * 2.0f - 1.0f;
  }
  float range(const float a, const float b)
  {
    return a + (b - a) * rng.get_float();
  }
  int irange(const int a, const int b)
  {
    return a + rng.get_int32(b - a + 1);
  }
  float normal()
  {
    const float u1 = std::max(rng.get_float(), 1e-7f);
    const float u2 = rng.get_float();
    return std::sqrt(-2.0f * std::log(u1)) * std::cos(2.0f * float(M_PI) * u2);
  }
  float3 unit()
  {
    return math::normalize(float3(normal(), normal(), normal()) + float3(1e-6f, 0.0f, 0.0f));
  }
};

static float hash01(const uint32_t a, const uint32_t b, const uint32_t c)
{
  return float(noise::hash(a, b, c)) / 4294967295.0f;
}

/* -------------------------------------------------------------------- */
/** \name Stone Shapes
 * \{ */

/** A stone mesh around its own center. */
struct Stone {
  Vector<float3> verts;
  /** Face sizes, and their points one face after another. */
  Vector<int> face_sizes;
  Vector<int> corner_verts;
  Vector<int3> tris;
};

static Stone icosphere(const int level)
{
  Stone s;
  const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
  const float3 base[12] = {{-1, t, 0},
                           {1, t, 0},
                           {-1, -t, 0},
                           {1, -t, 0},
                           {0, -1, t},
                           {0, 1, t},
                           {0, -1, -t},
                           {0, 1, -t},
                           {t, 0, -1},
                           {t, 0, 1},
                           {-t, 0, -1},
                           {-t, 0, 1}};
  for (const float3 &v : base) {
    s.verts.append(math::normalize(v));
  }
  Vector<int3> tris = {{0, 11, 5}, {0, 5, 1},  {0, 1, 7},   {0, 7, 10}, {0, 10, 11},
                       {1, 5, 9},  {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
                       {3, 9, 4},  {3, 4, 2},  {3, 2, 6},   {3, 6, 8},  {3, 8, 9},
                       {4, 9, 5},  {2, 4, 11}, {6, 2, 10},  {8, 6, 7},  {9, 8, 1}};
  for (int l = 0; l < level; l++) {
    Map<uint64_t, int> mids;
    auto mid = [&](const int a, const int b) {
      const uint64_t key = (uint64_t(std::min(a, b)) << 32) | uint64_t(std::max(a, b));
      return mids.lookup_or_add_cb(key, [&]() {
        s.verts.append(math::normalize(s.verts[a] + s.verts[b]));
        return int(s.verts.size()) - 1;
      });
    };
    Vector<int3> next;
    for (const int3 &tri : tris) {
      const int a = mid(tri[0], tri[1]);
      const int b = mid(tri[1], tri[2]);
      const int c = mid(tri[2], tri[0]);
      next.append({tri[0], a, c});
      next.append({tri[1], b, a});
      next.append({tri[2], c, b});
      next.append({a, b, c});
    }
    tris = std::move(next);
  }
  for (const int3 &tri : tris) {
    s.face_sizes.append(3);
    s.corner_verts.append(tri[0]);
    s.corner_verts.append(tri[1]);
    s.corner_verts.append(tri[2]);
  }
  s.tris = std::move(tris);
  return s;
}

/** Flattens the points past a plane onto it: a broken, flat face. */
static void chip(Stone &s, float3 normal, const float depth)
{
  normal = math::normalize(normal);
  float support = -1e30f;
  for (const float3 &v : s.verts) {
    support = std::max(support, math::dot(normal, v));
  }
  const float d = support * depth;
  for (float3 &v : s.verts) {
    const float k = math::dot(normal, v) - d;
    if (k > 0.0f) {
      v -= normal * k;
    }
  }
}

static float sign_pow(const float x, const float e)
{
  return std::copysign(std::pow(std::abs(x), e), x);
}

enum class Kind : int8_t { Round, Angular, Blocky, Slab, Wedge, Upright };

static Kind pick_kind(Rng &rng, const StoneworkShape shape)
{
  struct W {
    Kind kind;
    float w;
  };
  Vector<W> list;
  switch (shape) {
    case StoneworkShape::Round:
      return Kind::Round;
    case StoneworkShape::Angular:
      return Kind::Angular;
    case StoneworkShape::Blocks:
      return Kind::Blocky;
    case StoneworkShape::Slabs:
      return Kind::Slab;
    case StoneworkShape::Wedges:
      return Kind::Wedge;
    case StoneworkShape::Mixed:
      list = {{Kind::Angular, 3}, {Kind::Blocky, 2}, {Kind::Slab, 1}, {Kind::Wedge, 2},
              {Kind::Round, 1}};
      break;
    case StoneworkShape::DryWall:
      list = {{Kind::Blocky, 4}, {Kind::Slab, 3}, {Kind::Wedge, 2}, {Kind::Upright, 1},
              {Kind::Angular, 1}};
      break;
  }
  float total = 0.0f;
  for (const W &w : list) {
    total += w.w;
  }
  float r = rng.f() * total;
  for (const W &w : list) {
    if (r < w.w) {
      return w.kind;
    }
    r -= w.w;
  }
  return list.last().kind;
}

static void aspect_range(const StoneworkShape shape, float &lo, float &hi)
{
  switch (shape) {
    case StoneworkShape::Round:
      lo = 1.1f, hi = 2.2f;
      break;
    case StoneworkShape::Angular:
      lo = 1.1f, hi = 2.4f;
      break;
    case StoneworkShape::Blocks:
      lo = 1.3f, hi = 2.6f;
      break;
    case StoneworkShape::Slabs:
      lo = 3.0f, hi = 6.0f;
      break;
    case StoneworkShape::Wedges:
      lo = 1.2f, hi = 2.4f;
      break;
    case StoneworkShape::Mixed:
      lo = 1.2f, hi = 3.0f;
      break;
    case StoneworkShape::DryWall:
      lo = 1.4f, hi = 3.5f;
      break;
  }
}

/** A built-in stone. #size is about half its middle width. */
static Stone make_stone(Rng &rng, const Kind kind, const float size, const float aspect,
                        const StoneworkParams &params)
{
  Stone s = icosphere(std::clamp(params.detail, 1, 4));
  const float3 off(rng.f() * 100.0f, rng.f() * 100.0f, rng.f() * 100.0f);
  const float flat = 1.25f - 0.75f * std::clamp(params.flatness, 0.0f, 1.0f);
  const float sa = std::sqrt(aspect);
  float3 half(size * sa, size * rng.range(0.7f, 1.05f), size / sa * flat);
  if (kind == Kind::Upright) {
    half = float3(size / sa * flat, size * rng.range(0.7f, 1.0f), size * sa);
  }
  const bool boxy = ELEM(kind, Kind::Blocky, Kind::Slab, Kind::Wedge, Kind::Upright);
  /* A small skew so no two blocks are square. */
  const float3x3 skew = float3x3(float3(1.0f, rng.s() * 0.08f, rng.s() * 0.05f),
                                 float3(rng.s() * 0.08f, 1.0f, rng.s() * 0.05f),
                                 float3(rng.s() * 0.05f, rng.s() * 0.05f, 1.0f));
  for (float3 &v : s.verts) {
    float3 p = v;
    if (boxy) {
      p = float3(sign_pow(v.x, 0.3f), sign_pow(v.y, 0.3f), sign_pow(v.z, 0.3f));
      p = skew * p;
    }
    else if (kind == Kind::Round) {
      p *= 1.0f + 0.35f * noise::perlin_signed(v * 1.2f + off);
    }
    v = p * half;
  }
  switch (kind) {
    case Kind::Round: {
      const int n = rng.irange(1, 3);
      for (int i = 0; i < n; i++) {
        chip(s, rng.unit(), rng.range(0.7f, 0.9f));
      }
      break;
    }
    case Kind::Angular: {
      const int n = rng.irange(6, 10);
      for (int i = 0; i < n; i++) {
        chip(s, rng.unit(), rng.range(0.55f, 0.85f));
      }
      break;
    }
    default: {
      /* Broken corners and edges. */
      for (const float sx : {-1.0f, 1.0f}) {
        for (const float sz : {-1.0f, 1.0f}) {
          if (rng.f() < 0.6f) {
            chip(s,
                 float3(sx / half.x, rng.s() * 0.6f / half.y, sz * rng.range(0.6f, 1.6f) / half.z),
                 rng.range(0.82f, 0.95f));
          }
        }
      }
      if (kind == Kind::Slab) {
        for (const float sx : {-1.0f, 1.0f}) {
          chip(s, float3(sx / half.x, rng.s() * 0.5f / half.y, rng.s() * 0.7f / half.z),
               rng.range(0.82f, 0.97f));
        }
      }
      if (kind == Kind::Wedge) {
        const float sx = rng.f() < 0.5f ? -1.0f : 1.0f;
        const float sz = rng.f() < 0.5f ? -1.0f : 1.0f;
        chip(s, float3(sx * rng.range(0.5f, 1.5f) / half.x, 0.0f, sz / half.z),
             rng.range(0.35f, 0.7f));
      }
      if (rng.f() < 0.5f) {
        chip(s, rng.unit(), rng.range(0.85f, 0.97f));
      }
      break;
    }
  }
  /* Bumps. */
  const float rough = std::max(params.roughness, 0.0f) * 0.12f * size;
  if (rough > 0.0f) {
    for (float3 &v : s.verts) {
      const float3 dir = math::normalize(v + float3(1e-6f));
      v += dir * rough * noise::perlin_signed(v / size * 2.5f + off * 1.7f);
    }
  }
  return s;
}

/** Each loose part of the meshes is one stone, around its own center. */
static Vector<Stone> custom_stones(const Span<const Mesh *> meshes)
{
  Vector<Stone> out;
  for (const Mesh *mesh : meshes) {
    if (mesh == nullptr || mesh->faces_num == 0) {
      continue;
    }
    const Span<float3> positions = mesh->vert_positions();
    const OffsetIndices<int> faces = mesh->faces();
    const Span<int> corner_verts = mesh->corner_verts();
    /* Loose parts by the points their faces share. */
    Array<int> parent(mesh->verts_num);
    for (const int i : parent.index_range()) {
      parent[i] = i;
    }
    auto find = [&](int i) {
      while (parent[i] != i) {
        parent[i] = parent[parent[i]];
        i = parent[i];
      }
      return i;
    };
    for (const int f : faces.index_range()) {
      const Span<int> fv = corner_verts.slice(faces[f]);
      for (const int i : fv.index_range().drop_front(1)) {
        const int a = find(fv[0]);
        const int b = find(fv[i]);
        if (a != b) {
          parent[a] = b;
        }
      }
    }
    Map<int, int> part_of_root;
    Vector<Vector<int>> part_faces;
    for (const int f : faces.index_range()) {
      const int root = find(corner_verts[faces[f].first()]);
      const int part = part_of_root.lookup_or_add_cb(root, [&]() {
        part_faces.append({});
        return int(part_faces.size()) - 1;
      });
      part_faces[part].append(f);
    }
    for (const Vector<int> &fs : part_faces) {
      Stone s;
      Map<int, int> local;
      for (const int f : fs) {
        const Span<int> fv = corner_verts.slice(faces[f]);
        s.face_sizes.append(fv.size());
        for (const int v : fv) {
          const int l = local.lookup_or_add_cb(v, [&]() {
            s.verts.append(positions[v]);
            return int(s.verts.size()) - 1;
          });
          s.corner_verts.append(l);
        }
        const int first = s.corner_verts.size() - fv.size();
        for (int i = 1; i + 1 < fv.size(); i++) {
          s.tris.append({s.corner_verts[first], s.corner_verts[first + i],
                         s.corner_verts[first + i + 1]});
        }
      }
      float3 lo(1e30f), hi(-1e30f);
      for (const float3 &v : s.verts) {
        lo = math::min(lo, v);
        hi = math::max(hi, v);
      }
      const float3 center = (lo + hi) * 0.5f;
      for (float3 &v : s.verts) {
        v -= center;
      }
      if (!s.tris.is_empty()) {
        out.append(std::move(s));
      }
    }
  }
  return out;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Grid
 * \{ */

/**
 * Calls #fn(ix, iy, z) for every column whose center is under the triangle, with the triangle's
 * height there. Coordinates are in cells.
 */
template<typename Fn>
static void raster_columns(
    const float3 &a, const float3 &b, const float3 &c, const int nx, const int ny, const Fn &fn)
{
  const float d = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
  if (std::abs(d) < 1e-12f) {
    return;
  }
  const int x0 = std::max(int(std::ceil(std::min({a.x, b.x, c.x}) - 0.5f)), 0);
  const int x1 = std::min(int(std::floor(std::max({a.x, b.x, c.x}) - 0.5f)), nx - 1);
  const int y0 = std::max(int(std::ceil(std::min({a.y, b.y, c.y}) - 0.5f)), 0);
  const int y1 = std::min(int(std::floor(std::max({a.y, b.y, c.y}) - 0.5f)), ny - 1);
  for (int iy = y0; iy <= y1; iy++) {
    /* Slightly off the center, so no column runs exactly along an edge. */
    const float py = float(iy) + 0.5f + 1.3e-4f;
    for (int ix = x0; ix <= x1; ix++) {
      const float px = float(ix) + 0.5f + 0.7e-4f;
      const float w1 = ((px - a.x) * (c.y - a.y) - (c.x - a.x) * (py - a.y)) / d;
      const float w2 = ((b.x - a.x) * (py - a.y) - (px - a.x) * (b.y - a.y)) / d;
      const float w0 = 1.0f - w1 - w2;
      if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) {
        continue;
      }
      fn(ix, iy, w0 * a.z + w1 * b.z + w2 * c.z);
    }
  }
}

/** A stone in one pose, cut into columns of cells. */
struct Pose {
  int stone = 0;
  float3x3 rot = float3x3::identity();
  float scale = 1.0f;
  /** Corner of the posed stone's box, in its own space. */
  float3 lo;
  int w = 0, d = 0;
  Vector<int2> cols;
  Vector<int> bottom, top;
  /** Columns just around the stone. */
  Vector<int2> ring;
  float2 com;
  int height = 0;
  bool ok = false;
};

static Pose make_pose(const Stone &stone, const int index, const float3x3 &rot, const float scale,
                      const float cell)
{
  Pose pose;
  pose.stone = index;
  pose.rot = rot;
  pose.scale = scale;
  Array<float3> p(stone.verts.size());
  float3 lo(1e30f), hi(-1e30f);
  for (const int i : p.index_range()) {
    p[i] = rot * (stone.verts[i] * scale);
    lo = math::min(lo, p[i]);
    hi = math::max(hi, p[i]);
  }
  pose.lo = lo;
  pose.w = int(std::ceil((hi.x - lo.x) / cell)) + 1;
  pose.d = int(std::ceil((hi.y - lo.y) / cell)) + 1;
  if (pose.w > 400 || pose.d > 400) {
    return pose;
  }
  for (float3 &v : p) {
    v = (v - lo) / cell;
  }
  Array<float> zmin(pose.w * pose.d, 1e30f), zmax(pose.w * pose.d, -1e30f);
  for (const int3 &t : stone.tris) {
    raster_columns(p[t[0]], p[t[1]], p[t[2]], pose.w, pose.d, [&](int ix, int iy, float z) {
      const int i = iy * pose.w + ix;
      zmin[i] = std::min(zmin[i], z);
      zmax[i] = std::max(zmax[i], z);
    });
  }
  Array<bool> filled(pose.w * pose.d, false);
  float sx = 0.0f, sy = 0.0f, sw = 0.0f;
  for (int iy = 0; iy < pose.d; iy++) {
    for (int ix = 0; ix < pose.w; ix++) {
      const int i = iy * pose.w + ix;
      if (zmax[i] - zmin[i] < 0.5f) {
        continue;
      }
      const int b = int(std::floor(zmin[i] + 0.5f));
      const int t = std::max(int(std::floor(zmax[i] + 0.5f)), b + 1);
      filled[i] = true;
      pose.cols.append({ix, iy});
      pose.bottom.append(b);
      pose.top.append(t);
      pose.height = std::max(pose.height, t);
      const float wgt = float(t - b);
      sx += (float(ix) + 0.5f) * wgt;
      sy += (float(iy) + 0.5f) * wgt;
      sw += wgt;
    }
  }
  if (pose.cols.size() < 2) {
    return pose;
  }
  pose.com = float2(sx, sy) / sw;
  for (int iy = -1; iy <= pose.d; iy++) {
    for (int ix = -1; ix <= pose.w; ix++) {
      const bool inside = ix >= 0 && iy >= 0 && ix < pose.w && iy < pose.d;
      if (inside && filled[iy * pose.w + ix]) {
        continue;
      }
      bool next = false;
      for (const int2 o : {int2(1, 0), int2(-1, 0), int2(0, 1), int2(0, -1)}) {
        const int jx = ix + o.x;
        const int jy = iy + o.y;
        if (jx >= 0 && jy >= 0 && jx < pose.w && jy < pose.d && filled[jy * pose.w + jx]) {
          next = true;
        }
      }
      if (next) {
        pose.ring.append({ix, iy});
      }
    }
  }
  pose.ok = true;
  return pose;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Packing
 * \{ */

struct Placed {
  int stone;
  float3x3 rot;
  float scale;
  float3 offset;
};

static float3x3 rot_z(const float a)
{
  const float c = std::cos(a), s = std::sin(a);
  return float3x3(float3(c, s, 0.0f), float3(-s, c, 0.0f), float3(0.0f, 0.0f, 1.0f));
}

static float3x3 rot_x_pi()
{
  return float3x3(float3(1.0f, 0.0f, 0.0f), float3(0.0f, -1.0f, 0.0f), float3(0.0f, 0.0f, -1.0f));
}

struct MeshFree {
  void operator()(Mesh *mesh) const
  {
    if (mesh != nullptr) {
      BKE_id_free(nullptr, mesh);
    }
  }
};
using OwnedMesh = std::unique_ptr<Mesh, MeshFree>;

/** Moves the points inward by #depth, the same distance from every face. */
static void inset_mesh(Mesh &mesh, const float depth)
{
  if (depth <= 0.0f || mesh.faces_num == 0) {
    return;
  }
  const Array<float3> vn(mesh.vert_normals());
  const Span<float3> fn = mesh.face_normals();
  const OffsetIndices<int> faces = mesh.faces();
  const Span<int> corner_verts = mesh.corner_verts();
  Array<float> min_dot(mesh.verts_num, 1.0f);
  for (const int f : faces.index_range()) {
    for (const int v : corner_verts.slice(faces[f])) {
      min_dot[v] = std::min(min_dot[v], math::dot(vn[v], fn[f]));
    }
  }
  MutableSpan<float3> positions = mesh.vert_positions_for_write();
  for (const int v : positions.index_range()) {
    positions[v] -= vn[v] * (depth / std::max(min_dot[v], 0.3f));
  }
  mesh.tag_positions_changed();
}

/** The filled shape pulled in by the mortar depth, and made dense. */
static OwnedMesh make_mortar(const Mesh &mesh,
                             const StoneworkParams &params,
                             const float3 &lo,
                             const float3 &hi,
                             std::string &r_message)
{
  OwnedMesh base;
  if (params.fill == StoneworkFill::Box) {
    base = OwnedMesh(create_cuboid_mesh(hi - lo, 2, 2, 2));
    const float3 center = (lo + hi) * 0.5f;
    for (float3 &p : base->vert_positions_for_write()) {
      p += center;
    }
    base->tag_positions_changed();
  }
  else {
    base = OwnedMesh(BKE_mesh_copy_for_eval(mesh));
  }
  inset_mesh(*base, params.mortar_depth);

  const float extent = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z});
  switch (params.mortar_remesh) {
    case StoneworkRemesh::Isotropic: {
      IsotropicRemeshParams remesh;
      remesh.target_edge_length = std::max(params.mortar_resolution, extent / 1000.0f);
      remesh.preserve_boundary = true;
      remesh.preserve_sharp_edges = true;
      remesh.sharp_angle = 0.5235988f;
      remesh.transfer_attributes = false;
      Mesh *result = isotropic_remesh(*base, remesh);
      return result ? OwnedMesh(result) : std::move(base);
    }
    case StoneworkRemesh::Voxel: {
      Mesh *result = BKE_mesh_remesh_voxel(
          base.get(), std::max(params.mortar_resolution, extent / 1500.0f), 0.0f, 0.0f, nullptr);
      return result ? OwnedMesh(result) : std::move(base);
    }
    case StoneworkRemesh::Divide: {
      MeshDivideParams divide;
      divide.size = float3(std::max(params.mortar_resolution, extent / 1000.0f));
      std::string message;
      Mesh *result = mesh_divide(*base, divide, message);
      if (!message.empty()) {
        r_message += message + " ";
      }
      return result ? OwnedMesh(result) : std::move(base);
    }
    case StoneworkRemesh::None:
      break;
  }
  return base;
}

}  // namespace stonework

static Mesh *mesh_stonework_compute(const Mesh &mesh,
                                    const StoneworkParams &params,
                                    int *r_stones_num,
                                    std::string *r_message)
{
  using namespace stonework;
  if (r_stones_num) {
    *r_stones_num = 0;
  }
  if (mesh.verts_num == 0 || params.stone_size <= 0.0f) {
    return nullptr;
  }
  Rng rng(noise::hash(uint32_t(params.seed), 4021u));
  const float size = params.stone_size;

  /* Your own stones, or a pool of built-in ones made as they are needed. */
  Vector<Stone> stones = custom_stones(params.custom_stones);
  const bool custom = !stones.is_empty();
  const int custom_num = stones.size();

  /* The grid. */
  const Span<float3> positions = mesh.vert_positions();
  float3 mlo(1e30f), mhi(-1e30f);
  for (const float3 &p : positions) {
    mlo = math::min(mlo, p);
    mhi = math::max(mhi, p);
  }
  const float poke = std::max(params.poke_out, 0.0f);
  float cell = size * 0.2f;
  {
    const float3 ext = mhi - mlo + float3(2.0f * (poke + size * 0.2f));
    const double cells = double(ext.x / cell) * double(ext.y / cell) * double(ext.z / cell);
    if (cells > 6.0e6) {
      cell *= float(std::cbrt(cells / 6.0e6));
    }
  }
  const float3 origin = mlo - float3(poke + cell);
  const int nx = std::max(int(std::ceil((mhi.x - mlo.x + 2.0f * (poke + cell)) / cell)), 1);
  const int ny = std::max(int(std::ceil((mhi.y - mlo.y + 2.0f * (poke + cell)) / cell)), 1);
  const int nz = std::max(int(std::ceil((mhi.z - mlo.z + 2.0f * (poke + cell)) / cell)), 1);
  const int64_t cells_num = int64_t(nx) * ny * nz;
  auto at = [&](const int x, const int y, const int z) {
    return (int64_t(z) * ny + y) * nx + x;
  };

  /* Cells inside the mesh, by the heights where the faces cross each column. */
  Array<uint8_t> inside(cells_num, 0);
  if (params.fill == StoneworkFill::Box) {
    for (int z = 0; z < nz; z++) {
      for (int y = 0; y < ny; y++) {
        for (int x = 0; x < nx; x++) {
          const float3 c = origin + (float3(x, y, z) + 0.5f) * cell;
          if (c.x >= mlo.x && c.y >= mlo.y && c.z >= mlo.z && c.x <= mhi.x && c.y <= mhi.y &&
              c.z <= mhi.z)
          {
            inside[at(x, y, z)] = 1;
          }
        }
      }
    }
  }
  else {
    Array<Vector<float>> hits(nx * ny);
    const OffsetIndices<int> faces = mesh.faces();
    const Span<int> corner_verts = mesh.corner_verts();
    auto grid_pos = [&](const int v) { return (positions[v] - origin) / cell; };
    for (const int f : faces.index_range()) {
      const Span<int> fv = corner_verts.slice(faces[f]);
      for (int i = 1; i + 1 < fv.size(); i++) {
        raster_columns(grid_pos(fv[0]),
                       grid_pos(fv[i]),
                       grid_pos(fv[i + 1]),
                       nx,
                       ny,
                       [&](int ix, int iy, float z) { hits[iy * nx + ix].append(z); });
      }
    }
    threading::parallel_for(IndexRange(nx * ny), 256, [&](const IndexRange range) {
      for (const int col : range) {
        Vector<float> &zs = hits[col];
        std::sort(zs.begin(), zs.end());
        for (int i = 0; i + 1 < zs.size(); i += 2) {
          const int z0 = std::max(int(std::ceil(zs[i] - 0.5f)), 0);
          const int z1 = std::min(int(std::floor(zs[i + 1] - 0.5f)), nz - 1);
          for (int z = z0; z <= z1; z++) {
            inside[int64_t(z) * nx * ny + col] = 1;
          }
        }
      }
    });
  }

  /* Distance in cells to the nearest cell of the other kind, up to a limit. */
  auto distance_from = [&](const bool from_inside, const int limit) {
    Array<uint8_t> dist(cells_num, 255);
    std::queue<int64_t> queue;
    for (int64_t i = 0; i < cells_num; i++) {
      if (bool(inside[i]) == from_inside) {
        dist[i] = 0;
        queue.push(i);
      }
    }
    while (!queue.empty()) {
      const int64_t i = queue.front();
      queue.pop();
      if (dist[i] >= limit) {
        continue;
      }
      const int x = int(i % nx);
      const int y = int((i / nx) % ny);
      const int z = int(i / (int64_t(nx) * ny));
      const int3 ns[6] = {{x + 1, y, z}, {x - 1, y, z}, {x, y + 1, z},
                          {x, y - 1, z}, {x, y, z + 1}, {x, y, z - 1}};
      for (const int3 &n : ns) {
        if (n.x < 0 || n.y < 0 || n.z < 0 || n.x >= nx || n.y >= ny || n.z >= nz) {
          continue;
        }
        const int64_t j = at(n.x, n.y, n.z);
        if (dist[j] == 255) {
          dist[j] = dist[i] + 1;
          queue.push(j);
        }
      }
    }
    return dist;
  };

  Array<uint8_t> allowed(cells_num, 0);
  if (params.fill == StoneworkFill::Surface) {
    /* One layer of stones: cells inside, near the outside. */
    const int depth = std::clamp(int(std::round(size * 2.6f / cell)), 1, 250);
    const Array<uint8_t> dist = distance_from(false, depth + 1);
    for (int64_t i = 0; i < cells_num; i++) {
      allowed[i] = inside[i] && dist[i] <= depth;
    }
  }
  else {
    allowed.as_mutable_span().copy_from(inside);
  }
  if (poke > 0.0f) {
    const int limit = std::clamp(int(std::round(poke / cell)), 1, 250);
    const Array<uint8_t> dist = distance_from(true, limit + 1);
    for (int64_t i = 0; i < cells_num; i++) {
      if (!inside[i] && dist[i] <= limit) {
        allowed[i] = 1;
      }
    }
  }

  /* Each column's floor and ceiling: the lowest run of allowed cells. */
  const int cols_num = nx * ny;
  Array<int> floor(cols_num, -1), ceil(cols_num, -1), surf(cols_num, -1);
  for (int col = 0; col < cols_num; col++) {
    for (int z = 0; z < nz; z++) {
      if (allowed[int64_t(z) * cols_num + col]) {
        if (floor[col] < 0) {
          floor[col] = z;
        }
        ceil[col] = z + 1;
      }
      else if (floor[col] >= 0) {
        break;
      }
    }
    surf[col] = floor[col];
  }

  /* The pool of stones to pick from. */
  float aspect_lo, aspect_hi;
  aspect_range(params.shape, aspect_lo, aspect_hi);
  const float size_var = std::clamp(params.size_variation, 0.0f, 1.0f);
  float phase_scale = 1.0f;
  struct Entry {
    int stone;
    float scale;
  };
  auto new_entry = [&]() -> Entry {
    const float k = std::clamp(std::exp(rng.normal() * size_var), 0.45f, 2.2f) * phase_scale;
    if (custom) {
      return {rng.irange(0, custom_num - 1), k};
    }
    const Kind kind = pick_kind(rng, params.shape);
    stones.append(
        make_stone(rng, kind, size * k, rng.range(aspect_lo, aspect_hi), params));
    return {int(stones.size()) - 1, 1.0f};
  };
  Vector<Entry> pool;
  for (int i = 0; i < 12; i++) {
    pool.append(new_entry());
  }

  Vector<Placed> placed;
  const int max_stones = std::max(params.max_stones, 1);
  int phase = 0;
  const int stride = (int64_t(nx) * ny > 40000) ? 2 : 1;
  uint32_t step = 0;
  /* Low columns no stone of this size fits into. They no longer hold the rest back, and are
   * opened again for the smaller stones. */
  Array<bool> stuck(cols_num, false);
  int placed_at_reset = 0;
  /* Which stone each cell belongs to, for closing the gaps afterwards. */
  const float close = std::clamp(params.close_gaps, 0.0f, 1.0f);
  Array<int> owner(close > 0.0f ? cells_num : 0, -1);
  while (int(placed.size()) < max_stones) {
    step++;
    int lowest = INT_MAX;
    for (int col = 0; col < cols_num; col++) {
      if (floor[col] >= 0 && surf[col] < ceil[col] - 1 && !stuck[col]) {
        lowest = std::min(lowest, surf[col]);
      }
    }
    const int band = std::max(4, int(std::round(2.5f * size * phase_scale / cell)));
    if (lowest == INT_MAX) {
      /* Every column left was stepped over. Look at them again while stones still get placed,
       * since they may fit higher up. */
      if (int(placed.size()) > placed_at_reset) {
        placed_at_reset = int(placed.size());
        stuck.fill(false);
        continue;
      }
      if (params.fillers && phase < 2) {
        phase++;
        phase_scale = phase == 1 ? 0.55f : 0.33f;
        for (Entry &e : pool) {
          e = new_entry();
        }
        stuck.fill(false);
        continue;
      }
      break;
    }

    /* A few stones, each turned along and across, the right way up and upside down. */
    Vector<Pose> poses;
    Vector<int> pose_entry;
    const int tries = 4;
    for (int t = 0; t < tries; t++) {
      const int e = rng.irange(0, int(pool.size()) - 1);
      const float jitter = rng.s() * 0.12f;
      for (const float yaw : {0.0f, float(M_PI) * 0.5f}) {
        for (const bool flip : {false, true}) {
          float3x3 rot = rot_z(yaw + jitter + (rng.f() < 0.5f ? float(M_PI) : 0.0f));
          if (flip) {
            rot = rot * rot_x_pi();
          }
          Pose pose = make_pose(stones[pool[e].stone], pool[e].stone, rot, pool[e].scale, cell);
          if (pose.ok) {
            poses.append(std::move(pose));
            pose_entry.append(e);
          }
        }
      }
    }

    struct Best {
      float score = 1e30f;
      int x = 0, y = 0, land = 0;
    };
    Array<Best> bests(poses.size());
    threading::parallel_for(poses.index_range(), 1, [&](const IndexRange range) {
      for (const int pi : range) {
        const Pose &pose = poses[pi];
        Best best;
        for (int y = 0; y + pose.d <= ny; y += stride) {
          for (int x = 0; x + pose.w <= nx; x += stride) {
            const int cc = (y + pose.d / 2) * nx + x + pose.w / 2;
            if (floor[cc] < 0 || surf[cc] > lowest + band) {
              continue;
            }
            int land = INT_MIN;
            bool blocked = false;
            for (const int k : pose.cols.index_range()) {
              const int col = (y + pose.cols[k].y) * nx + x + pose.cols[k].x;
              if (floor[col] < 0) {
                blocked = true;
                break;
              }
              land = std::max(land, surf[col] - pose.bottom[k]);
            }
            if (blocked) {
              continue;
            }
            int gaps = 0;
            int sx0 = INT_MAX, sx1 = INT_MIN, sy0 = INT_MAX, sy1 = INT_MIN;
            for (const int k : pose.cols.index_range()) {
              const int col = (y + pose.cols[k].y) * nx + x + pose.cols[k].x;
              if (land + pose.top[k] > ceil[col] || land + pose.bottom[k] < floor[col]) {
                blocked = true;
                break;
              }
              const int under = land + pose.bottom[k] - surf[col];
              gaps += under;
              if (under <= 1) {
                sx0 = std::min(sx0, pose.cols[k].x);
                sx1 = std::max(sx1, pose.cols[k].x);
                sy0 = std::min(sy0, pose.cols[k].y);
                sy1 = std::max(sy1, pose.cols[k].y);
              }
            }
            if (blocked || sx1 < 0) {
              continue;
            }
            /* It must not tip: its weight has to be over what holds it up. */
            const float m = 0.5f;
            if (pose.com.x < float(sx0) - m || pose.com.x > float(sx1 + 1) + m ||
                pose.com.y < float(sy0) - m || pose.com.y > float(sy1 + 1) + m)
            {
              continue;
            }
            /* A level top with its neighbors. */
            float match = 0.0f;
            int match_n = 0;
            const int top = land + pose.height;
            for (const int2 &r : pose.ring) {
              const int rx = x + r.x;
              const int ry = y + r.y;
              if (rx < 0 || ry < 0 || rx >= nx || ry >= ny) {
                continue;
              }
              const int col = ry * nx + rx;
              if (floor[col] < 0) {
                continue;
              }
              match += float(std::min(std::abs(top - surf[col]), 12));
              match_n++;
            }
            if (match_n > 0) {
              match /= float(match_n);
            }
            const float noise = hash01(step, uint32_t(pi), uint32_t(y * nx + x)) * 0.6f;
            const float score = float(land) + 2.5f * float(gaps) / float(pose.cols.size()) +
                                0.25f * match + noise;
            if (score < best.score) {
              best.score = score;
              best.x = x;
              best.y = y;
              best.land = land;
            }
          }
        }
        bests[pi] = best;
      }
    });
    int bi = -1;
    for (const int pi : bests.index_range()) {
      if (bests[pi].score < 1e29f && (bi < 0 || bests[pi].score < bests[bi].score)) {
        bi = pi;
      }
    }
    if (bi < 0) {
      /* Nothing fits this low: step over the lowest columns. Once all are stepped over, the
       * smaller stones take their turn. */
      /* The stones tried did not fit anywhere: swap them for new ones, so the pool never fills
       * up with stones too big for what is left. */
      for (const int e : pose_entry) {
        pool[e] = new_entry();
      }
      const int limit = lowest + std::max(band / 3, 1);
      for (int col = 0; col < cols_num; col++) {
        if (floor[col] >= 0 && surf[col] <= limit) {
          stuck[col] = true;
        }
      }
      continue;
    }
    const Pose &pose = poses[bi];
    const Best &best = bests[bi];
    for (const int k : pose.cols.index_range()) {
      const int col = (best.y + pose.cols[k].y) * nx + best.x + pose.cols[k].x;
      surf[col] = std::max(surf[col], best.land + pose.top[k]);
      if (!owner.is_empty()) {
        for (int z = best.land + pose.bottom[k]; z < best.land + pose.top[k]; z++) {
          if (z >= 0 && z < nz) {
            owner[int64_t(z) * cols_num + col] = int(placed.size());
          }
        }
      }
    }
    placed.append({pose.stone,
                   pose.rot,
                   pose.scale,
                   origin + float3(best.x, best.y, best.land) * cell - pose.lo});
    pool[pose_entry[bi]] = new_entry();
  }

  if (placed.is_empty()) {
    return nullptr;
  }

  /* Each stone grows into the free cells around it, meeting its neighbors halfway. */
  const int reach = std::clamp(int(std::round(close * size / cell)), 1, 250);
  if (!owner.is_empty()) {
    Array<uint8_t> dist(cells_num, 255);
    std::queue<int64_t> queue;
    for (int64_t i = 0; i < cells_num; i++) {
      if (owner[i] >= 0) {
        dist[i] = 0;
        queue.push(i);
      }
    }
    while (!queue.empty()) {
      const int64_t i = queue.front();
      queue.pop();
      if (dist[i] >= reach) {
        continue;
      }
      const int x = int(i % nx);
      const int y = int((i / nx) % ny);
      const int z = int(i / (int64_t(nx) * ny));
      const int3 ns[6] = {{x + 1, y, z}, {x - 1, y, z}, {x, y + 1, z},
                          {x, y - 1, z}, {x, y, z + 1}, {x, y, z - 1}};
      for (const int3 &n : ns) {
        if (n.x < 0 || n.y < 0 || n.z < 0 || n.x >= nx || n.y >= ny || n.z >= nz) {
          continue;
        }
        const int64_t j = at(n.x, n.y, n.z);
        if (dist[j] == 255 && owner[j] < 0 && allowed[j]) {
          owner[j] = owner[i];
          dist[j] = dist[i] + 1;
          queue.push(j);
        }
      }
    }
  }
  auto owner_at = [&](const float3 &p) {
    const float3 g = (p - origin) / cell;
    const int x = int(std::floor(g.x));
    const int y = int(std::floor(g.y));
    const int z = int(std::floor(g.z));
    if (x < 0 || y < 0 || z < 0 || x >= nx || y >= ny || z >= nz) {
      return -1;
    }
    return owner[at(x, y, z)];
  };

  /* Where each stone's points end up. */
  Array<int> vert_start(placed.size() + 1);
  vert_start[0] = 0;
  for (const int pi : placed.index_range()) {
    vert_start[pi + 1] = vert_start[pi] + stones[placed[pi].stone].verts.size();
  }
  Array<float3> final_pos(vert_start.last());
  const float half_gap = std::max(params.gap, 0.0f) * 0.5f;
  const float shrink = half_gap + cell * 0.4f;
  threading::parallel_for(placed.index_range(), 8, [&](const IndexRange range) {
    for (const int pi : range) {
      const Placed &p = placed[pi];
      const Stone &s = stones[p.stone];
      MutableSpan<float3> out = final_pos.as_mutable_span().slice(vert_start[pi],
                                                                    s.verts.size());
      float3 lo(1e30f), hi(-1e30f);
      for (const int i : s.verts.index_range()) {
        out[i] = p.rot * (s.verts[i] * p.scale) + p.offset;
        lo = math::min(lo, out[i]);
        hi = math::max(hi, out[i]);
      }
      const float3 center = (lo + hi) * 0.5f;
      const float room = std::min({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z}) * 0.25f;
      const float k = std::min(shrink, room);
      /* A random size per stone, around its center. */
      const float scale = math::interpolate(
          params.scale_min, params.scale_max, hash01(uint32_t(pi), uint32_t(params.seed), 31u));
      auto apply_scale = [&]() {
        if (scale != 1.0f) {
          for (float3 &v : out) {
            v = center + (v - center) * scale;
          }
        }
      };
      if (owner.is_empty()) {
        /* Pulled in a little to leave the gap. */
        for (float3 &v : out) {
          const float3 dir = v - center;
          const float len = math::length(dir);
          if (len > 1e-6f) {
            v -= dir / len * k;
          }
        }
        apply_scale();
        continue;
      }
      /* Point normals. */
      Array<float3> vn(s.verts.size(), float3(0.0f));
      int c = 0;
      for (const int size : s.face_sizes) {
        float3 n(0.0f);
        for (int j = 0; j < size; j++) {
          const float3 &a = out[s.corner_verts[c + j]];
          const float3 &b = out[s.corner_verts[c + (j + 1) % size]];
          n += math::cross(a, b);
        }
        for (int j = 0; j < size; j++) {
          vn[s.corner_verts[c + j]] += n;
        }
        c += size;
      }
      for (const int i : vn.index_range()) {
        const float len = math::length(vn[i]);
        vn[i] = len > 1e-12f ? vn[i] / len : math::normalize(out[i] - center + float3(1e-6f));
      }
      /* How far each point can go before it leaves the stone's cells. */
      const float step = cell * 0.25f;
      const float max_t = float(reach + 1) * cell;
      Array<float> limit(s.verts.size());
      for (const int i : s.verts.index_range()) {
        float t_ok = -1.0f;
        for (float t = 0.0f; t <= max_t; t += step) {
          if (owner_at(out[i] + vn[i] * t) != pi) {
            break;
          }
          t_ok = t;
        }
        limit[i] = t_ok < 0.0f ? -k : std::max(-k, t_ok + step * 0.5f - half_gap - cell * 0.1f);
      }
      /* The stone grows by its six sides, each as far as most of its points allow, so its faces
       * stay flat and its shape stays whole. */
      float3 sides[6];
      float side_grow[6];
      for (int a = 0; a < 3; a++) {
        const float3 axis = math::normalize(p.rot[a]);
        sides[a * 2] = axis;
        sides[a * 2 + 1] = -axis;
      }
      Vector<float> facing;
      for (int d = 0; d < 6; d++) {
        facing.clear();
        for (const int i : s.verts.index_range()) {
          if (math::dot(vn[i], sides[d]) > 0.5f) {
            facing.append(limit[i]);
          }
        }
        if (facing.is_empty()) {
          side_grow[d] = -k;
          continue;
        }
        const int q = int(facing.size()) / 4;
        std::nth_element(facing.begin(), facing.begin() + q, facing.end());
        side_grow[d] = std::max(facing[q], -k);
      }
      for (const int i : s.verts.index_range()) {
        float3 move(0.0f);
        for (int d = 0; d < 6; d++) {
          const float w = std::clamp((math::dot(vn[i], sides[d]) - 0.2f) / 0.5f, 0.0f, 1.0f);
          move += sides[d] * (side_grow[d] * w);
        }
        out[i] += move;
      }
      apply_scale();
    }
  });

  /* Stones as a mesh: all of them, or one at a time for merging. */
  auto stones_mesh = [&](const IndexRange range, Vector<int> &r_face_stone) {
    int faces_num = 0, corners_num = 0;
    for (const int pi : range) {
      const Stone &s = stones[placed[pi].stone];
      faces_num += s.face_sizes.size();
      corners_num += s.corner_verts.size();
    }
    const int v_first = vert_start[range.first()];
    const int verts_num = vert_start[range.one_after_last()] - v_first;
    Mesh *result = bke::mesh_new_no_attributes(verts_num, 0, faces_num, corners_num);
    result->vert_positions_for_write().copy_from(final_pos.as_span().slice(v_first, verts_num));
    MutableSpan<int> out_offsets = result->face_offsets_for_write();
    MutableSpan<int> out_corners = result->corner_verts_for_write();
    r_face_stone.clear();
    int f0 = 0, c0 = 0;
    for (const int pi : range) {
      const Stone &s = stones[placed[pi].stone];
      const int v0 = vert_start[pi] - v_first;
      for (const int i : s.face_sizes.index_range()) {
        out_offsets[f0 + i] = c0;
        r_face_stone.append(pi);
        c0 += s.face_sizes[i];
      }
      for (const int i : s.corner_verts.index_range()) {
        out_corners[c0 - s.corner_verts.size() + i] = v0 + s.corner_verts[i];
      }
      f0 += s.face_sizes.size();
    }
    out_offsets.last() = c0;
    bke::mesh_calc_edges(*result, false, false);
    bke::mesh_sharp_edges_set_from_angle(*result, 0.6108652f);
    return result;
  };

  /* The attributes, per face by stone. -1 is the mortar. */
  const bool use_materials = params.material_index > 0 || params.mortar_material_index > 0;
  auto write_attributes = [&](Mesh &m, const Span<int> face_stone) {
    bke::MutableAttributeAccessor attributes = m.attributes_for_write();
    if (use_materials) {
      bke::SpanAttributeWriter<int> material = attributes.lookup_or_add_for_write_only_span<int>(
          "material_index", bke::AttrDomain::Face);
      for (const int f : face_stone.index_range()) {
        material.span[f] = face_stone[f] < 0 ? params.mortar_material_index :
                                               params.material_index;
      }
      material.finish();
    }
    if (params.stone_id_id) {
      bke::SpanAttributeWriter<int> ids = attributes.lookup_or_add_for_write_only_span<int>(
          *params.stone_id_id, bke::AttrDomain::Face);
      ids.span.copy_from(face_stone);
      ids.finish();
    }
    if (params.random_id) {
      bke::SpanAttributeWriter<float> rnd = attributes.lookup_or_add_for_write_only_span<float>(
          *params.random_id, bke::AttrDomain::Face);
      for (const int f : face_stone.index_range()) {
        rnd.span[f] = face_stone[f] < 0 ?
                          0.0f :
                          hash01(uint32_t(face_stone[f]), uint32_t(params.seed), 77u);
      }
      rnd.finish();
    }
    if (params.mortar_id) {
      bke::SpanAttributeWriter<bool> is_mortar =
          attributes.lookup_or_add_for_write_only_span<bool>(*params.mortar_id,
                                                             bke::AttrDomain::Face);
      for (const int f : face_stone.index_range()) {
        is_mortar.span[f] = face_stone[f] < 0;
      }
      is_mortar.finish();
    }
    if (params.color_id) {
      /* The stone per face, so the color is written after merging, one color per face. */
      bke::SpanAttributeWriter<int> stone = attributes.lookup_or_add_for_write_only_span<int>(
          ".stonework_stone", bke::AttrDomain::Face);
      stone.span.copy_from(face_stone);
      stone.finish();
    }
  };

  std::string message;
  OwnedMesh mortar;
  if (params.mortar) {
    mortar = make_mortar(mesh, params, mlo, mhi, message);
    if (mortar) {
      Array<int> mortar_faces(mortar->faces_num, -1);
      write_attributes(*mortar, mortar_faces);
    }
  }

  Mesh *result = nullptr;
  if (params.merge) {
    /* Every stone on its own, so stones that touch are joined too. */
    Vector<OwnedMesh> parts;
    if (mortar) {
      parts.append(std::move(mortar));
    }
    Vector<int> face_stone;
    for (const int pi : placed.index_range()) {
      OwnedMesh part(stones_mesh(IndexRange(pi, 1), face_stone));
      write_attributes(*part, face_stone);
      parts.append(std::move(part));
    }
    Vector<const Mesh *> meshes;
    for (const OwnedMesh &part : parts) {
      meshes.append(part.get());
    }
    const Array<float4x4> transforms(meshes.size(), float4x4::identity());
    boolean::BooleanOpParameters op;
    op.boolean_mode = boolean::Operation::Union;
    boolean::BooleanError error = boolean::BooleanError::NoError;
    result = boolean::mesh_boolean(
        meshes, transforms, {}, op, boolean::Solver::Manifold, nullptr, &error);
    if (result == nullptr) {
      message += error == boolean::BooleanError::NonManifold ?
                     "Could not merge: a stone or the mortar is not a closed surface. " :
                     "Could not merge. ";
      /* Fall back to the parts side by side. */
      Vector<bke::GeometrySet> sets;
      for (OwnedMesh &part : parts) {
        sets.append(bke::GeometrySet::from_mesh(part.release()));
      }
      bke::GeometrySet joined = join_geometries(sets, {});
      result = joined.get_component_for_write<bke::MeshComponent>().release();
    }
    else {
      bke::mesh_sharp_edges_set_from_angle(*result, 0.6108652f);
    }
  }
  else {
    Vector<int> face_stone;
    result = stones_mesh(placed.index_range(), face_stone);
    write_attributes(*result, face_stone);
    if (mortar) {
      bke::GeometrySet joined = join_geometries(
          {bke::GeometrySet::from_mesh(result), bke::GeometrySet::from_mesh(mortar.release())},
          {});
      result = joined.get_component_for_write<bke::MeshComponent>().release();
    }
  }

  /* One color per face, from its stone. */
  if (params.color_id) {
    bke::MutableAttributeAccessor attributes = result->attributes_for_write();
    Array<int> face_stone(result->faces_num, 0);
    if (const bke::GAttributeReader stone = attributes.lookup(".stonework_stone",
                                                              bke::AttrDomain::Face))
    {
      stone.varray.typed<int>().materialize(face_stone);
    }
    attributes.remove(".stonework_stone");
    bke::SpanAttributeWriter<ColorGeometry4f> color =
        attributes.lookup_or_add_for_write_only_span<ColorGeometry4f>(*params.color_id,
                                                                      bke::AttrDomain::Corner);
    const OffsetIndices<int> faces = result->faces();
    for (const int f : face_stone.index_range()) {
      ColorGeometry4f col(0.5f, 0.5f, 0.5f, 1.0f);
      if (face_stone[f] >= 0) {
        const uint32_t id = uint32_t(face_stone[f]);
        const uint32_t seed = uint32_t(params.seed);
        const float value = math::interpolate(
            params.value_min, params.value_max, hash01(id, seed, 13u));
        if (params.color_mode == StoneworkColorMode::Gray) {
          col = ColorGeometry4f(value, value, value, 1.0f);
        }
        else {
          float hue = math::interpolate(params.hue_min, params.hue_max, hash01(id, seed, 11u));
          hue -= std::floor(hue);
          hsv_to_rgb(hue,
                     math::interpolate(
                         params.saturation_min, params.saturation_max, hash01(id, seed, 12u)),
                     value,
                     &col.r,
                     &col.g,
                     &col.b);
        }
      }
      color.span.slice(faces[f]).fill(col);
    }
    color.finish();
  }

  if (r_message) {
    *r_message = message;
  }
  if (r_stones_num) {
    *r_stones_num = placed.size();
  }
  return result;
}

namespace {

class StoneworkCachedValue : public memory_cache::CachedValue {
 public:
  Mesh *mesh = nullptr;
  int stones_num = 0;
  std::string message;

  ~StoneworkCachedValue() override
  {
    if (mesh != nullptr) {
      BKE_id_free(nullptr, mesh);
    }
  }

  void count_memory(MemoryCounter &memory) const override
  {
    if (mesh != nullptr) {
      mesh->count_memory(memory);
    }
  }
};

void hash_optional(SimCacheHasher &hasher, const std::optional<std::string> &str)
{
  hasher.add(str.has_value());
  if (str) {
    hasher.add_string(*str);
  }
}

}  // namespace

Mesh *mesh_stonework(const Mesh &mesh,
                     const StoneworkParams &params,
                     int *r_stones_num,
                     std::string *r_message)
{
  /* Objects are evaluated again for undo and for changes to other objects. Taking the result from
   * the cache when nothing it depends on changed keeps that from stacking all over again. */
  SimCacheHasher hasher("mesh_stonework");
  hasher.add_mesh(mesh);
  hasher.add(params.fill);
  hasher.add(params.shape);
  hasher.add(params.custom_stones.size());
  for (const Mesh *stone : params.custom_stones) {
    hasher.add(stone != nullptr);
    if (stone != nullptr) {
      hasher.add_mesh(*stone);
    }
  }
  hasher.add(params.stone_size);
  hasher.add(params.size_variation);
  hasher.add(params.flatness);
  hasher.add(params.gap);
  hasher.add(params.fillers);
  hasher.add(params.poke_out);
  hasher.add(params.roughness);
  hasher.add(params.detail);
  hasher.add(params.max_stones);
  hasher.add(params.seed);
  hasher.add(params.close_gaps);
  hasher.add(params.scale_min);
  hasher.add(params.scale_max);
  hasher.add(params.mortar);
  hasher.add(params.mortar_depth);
  hasher.add(params.mortar_remesh);
  hasher.add(params.mortar_resolution);
  hasher.add(params.merge);
  hasher.add(params.material_index);
  hasher.add(params.mortar_material_index);
  hash_optional(hasher, params.stone_id_id);
  hash_optional(hasher, params.random_id);
  hash_optional(hasher, params.color_id);
  hasher.add(params.color_mode);
  hasher.add(params.hue_min);
  hasher.add(params.hue_max);
  hasher.add(params.saturation_min);
  hasher.add(params.saturation_max);
  hasher.add(params.value_min);
  hasher.add(params.value_max);
  hash_optional(hasher, params.mortar_id);

  const std::shared_ptr<const StoneworkCachedValue> value =
      std::dynamic_pointer_cast<const StoneworkCachedValue>(
          sim_cache_get_value(hasher.finish(), [&]() {
            auto value = std::make_unique<StoneworkCachedValue>();
            value->mesh = mesh_stonework_compute(
                mesh, params, &value->stones_num, &value->message);
            return value;
          }));
  if (r_stones_num) {
    *r_stones_num = value ? value->stones_num : 0;
  }
  if (r_message && value) {
    *r_message = value->message;
  }
  if (!value || value->mesh == nullptr) {
    return nullptr;
  }
  /* The copy shares its arrays with the cached mesh, so it is cheap. */
  return BKE_mesh_copy_for_eval(*value->mesh);
}

}  // namespace blender::geometry
