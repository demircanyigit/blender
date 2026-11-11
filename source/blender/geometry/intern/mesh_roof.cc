/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup geo
 *
 * Roof.
 *
 * Both modes end with the same steps on a set of roof faces:
 * - Overhangs: each open edge gets a strip in the plane of its face, reaching out and down the
 *   slope at eaves, and out at the sloped edges, with patches at outer corners.
 * - A slab: the faces given a thickness under them (in Cover mode only the overhangs).
 * - Covering: rows laid level across each slope from the eave up, cut where the slope ends.
 * - Caps on the edges that fold outward (ridges and hips) and flashing in the ones that fold
 *   inward (valleys).
 * - Fascia boards and gutters along the outer edges.
 */

#include <cfloat>
#include <cmath>

#include "DNA_mesh_types.h"

#include "BLI_array.hh"
#include "BLI_math_base.hh"
#include "BLI_math_vector.hh"
#include "BLI_noise.hh"
#include "BLI_ordered_edge.hh"
#include "BLI_utildefines.h"

#include "BKE_mesh.hh"

#include "GEO_mesh_roof.hh"

#include "mesh_boards.hh"

namespace blender::geometry {

namespace bd = boards;

namespace {

constexpr float PI_F = 3.14159265358979f;
/** Faces steeper than this are walls, not roof. */
constexpr float WALL_NORMAL_Z = 0.17364818f;

struct Roof {
  bd::Surface surface;
  /** Faces of #surface that are roof. */
  Vector<int> faces;
  /** Gable ends, shed walls. */
  bd::Surface walls;
  /** Outlines of flat roofs, for the parapets. */
  Vector<Vector<float3>> parapets;
};

int material_or(const int material, const int fallback)
{
  return material >= 0 ? material : fallback;
}

/* -------------------------------------------------------------------- */
/** \name Build
 * \{ */

/** A frame on the ground: X along the ridge, Y across it. */
struct Frame {
  float2 center;
  float2 dir;
  float2 perp;
  float z0;

  float3 operator()(const float x, const float y, const float z) const
  {
    const float2 p = center + dir * x + perp * y;
    return float3(p.x, p.y, z0 + z);
  }
};

void build_rect_roof(Roof &roof,
                     const MeshRoofParams &params,
                     const Frame &frame,
                     float a,
                     float b,
                     const int material)
{
  auto slope = [&](std::initializer_list<float3> points) {
    const int face = roof.surface.add_face_positions(
        Span<float3>(points.begin(), points.size()), material);
    if (face >= 0) {
      roof.faces.append(face);
    }
  };
  auto wall = [&](std::initializer_list<float3> points) {
    roof.walls.add_face_positions(Span<float3>(points.begin(), points.size()));
  };
  const float tan_pitch = std::tan(std::clamp(params.pitch, 0.0f, 1.5f));
  const float tan_lower = std::tan(std::clamp(params.lower_pitch, 0.0f, 1.5f));
  const float brk = std::clamp(params.break_ratio, 0.05f, 0.95f);
  const Frame &f = frame;
  const float3 e0 = f(-a, -b, 0), e1 = f(a, -b, 0), e2 = f(a, b, 0), e3 = f(-a, b, 0);

  auto hip = [&](const float ha, const float hb, const float z,
                 const float3 &c0, const float3 &c1, const float3 &c2, const float3 &c3) {
    const float h = hb * tan_pitch;
    const float rx = ha - hb;
    if (rx < 1e-5f) {
      const float3 apex = f(0, 0, z + std::min(ha, hb) * tan_pitch);
      slope({c0, c1, apex});
      slope({c1, c2, apex});
      slope({c2, c3, apex});
      slope({c3, c0, apex});
      return;
    }
    const float3 r0 = f(-rx, 0, z + h), r1 = f(rx, 0, z + h);
    slope({c0, c1, r1, r0});
    slope({c2, c3, r0, r1});
    slope({c1, c2, r1});
    slope({c3, c0, r0});
  };

  switch (params.shape) {
    case RoofShape::Gable: {
      const float h = b * tan_pitch;
      const float3 r0 = f(-a, 0, h), r1 = f(a, 0, h);
      slope({e0, e1, r1, r0});
      slope({e2, e3, r0, r1});
      wall({e1, e2, r1});
      wall({e3, e0, r0});
      break;
    }
    case RoofShape::Hip:
      hip(a, b, 0.0f, e0, e1, e2, e3);
      break;
    case RoofShape::Pyramid: {
      const float3 apex = f(0, 0, std::min(a, b) * tan_pitch);
      slope({e0, e1, apex});
      slope({e1, e2, apex});
      slope({e2, e3, apex});
      slope({e3, e0, apex});
      break;
    }
    case RoofShape::Shed: {
      const float h = 2.0f * b * tan_pitch;
      const float3 h0 = f(-a, b, h), h1 = f(a, b, h);
      slope({e0, e1, h1, h0});
      wall({e1, e2, h1});
      wall({e3, e0, h0});
      wall({e2, e3, h0, h1});
      break;
    }
    case RoofShape::Gambrel: {
      const float r1 = b * brk;
      const float z1 = r1 * tan_lower;
      const float zr = z1 + (b - r1) * tan_pitch;
      const float3 l0 = f(-a, -b + r1, z1), l1 = f(a, -b + r1, z1);
      const float3 k0 = f(-a, 0, zr), k1 = f(a, 0, zr);
      const float3 m0 = f(-a, b - r1, z1), m1 = f(a, b - r1, z1);
      slope({e0, e1, l1, l0});
      slope({l0, l1, k1, k0});
      slope({m1, m0, k0, k1});
      slope({e2, e3, m0, m1});
      wall({e1, e2, m1, k1, l1});
      wall({e3, e0, l0, k0, m0});
      break;
    }
    case RoofShape::Mansard: {
      const float r1 = std::min(a, b) * brk;
      const float z1 = r1 * tan_lower;
      const float a1 = a - r1, b1 = b - r1;
      const float3 i0 = f(-a1, -b1, z1), i1 = f(a1, -b1, z1), i2 = f(a1, b1, z1),
                   i3 = f(-a1, b1, z1);
      slope({e0, e1, i1, i0});
      slope({e1, e2, i2, i1});
      slope({e2, e3, i3, i2});
      slope({e3, e0, i0, i3});
      hip(a1, b1, z1, i0, i1, i2, i3);
      break;
    }
    case RoofShape::Flat:
      break;
  }
}

bool build(Roof &roof, const Mesh &mesh, const MeshRoofParams &params, std::string &r_message)
{
  const bd::Surface input = bd::surface_from_mesh(mesh, params.selection);
  Vector<int> up;
  for (const int face : input.faces.index_range()) {
    if (input.face_normal(face).z > 0.5f) {
      up.append(face);
    }
  }
  if (up.is_empty()) {
    r_message = "Build needs faces facing up, as the outlines of the buildings";
    return false;
  }
  const float tan_pitch = std::tan(std::clamp(params.pitch, 0.0f, 1.5f));
  bool not_rect = false;
  for (const Span<int> group : bd::coplanar_groups(input, up)) {
    /* The outer outline, counter-clockwise from above. */
    Vector<float3> outline;
    float best_area = 0.0f;
    for (const Span<int> loop : bd::boundary_loops(input, group)) {
      bd::Polygon flat;
      for (const int vert : loop) {
        flat.append(input.positions[vert].xy());
      }
      const float area = std::abs(bd::signed_area(flat));
      if (area > best_area) {
        best_area = area;
        outline.clear();
        for (const int vert : loop) {
          outline.append(input.positions[vert]);
        }
      }
    }
    if (outline.size() < 3 || best_area < 1e-10f) {
      continue;
    }
    bd::Polygon flat;
    float z0 = 0.0f;
    for (const float3 &p : outline) {
      flat.append(p.xy());
      z0 += p.z;
    }
    z0 /= float(outline.size());
    if (bd::signed_area(flat) < 0.0f) {
      std::reverse(flat.begin(), flat.end());
    }
    const int material = input.face_materials[group[0]];

    if (params.shape == RoofShape::Flat) {
      Vector<float3> top;
      for (const float2 &p : flat) {
        top.append(float3(p.x, p.y, z0));
      }
      const int face = roof.surface.add_face_positions(top, material);
      if (face >= 0) {
        roof.faces.append(face);
      }
      roof.parapets.append(std::move(top));
      continue;
    }
    if (params.shape == RoofShape::Pyramid && bd::is_convex(flat)) {
      const float2 center = bd::centroid(flat);
      float nearest = FLT_MAX;
      for (const int i : flat.index_range()) {
        const float2 &p = flat[i];
        const float2 edge = flat[(i + 1) % flat.size()] - p;
        const float length = math::length(edge);
        if (length > 1e-7f) {
          nearest = std::min(nearest, std::abs(bd::cross2(edge, center - p)) / length);
        }
      }
      const float3 apex(center.x, center.y, z0 + nearest * tan_pitch);
      for (const int i : flat.index_range()) {
        const float2 &p = flat[i];
        const float2 &q = flat[(i + 1) % flat.size()];
        const int face = roof.surface.add_face_positions(
            {float3(p.x, p.y, z0), float3(q.x, q.y, z0), apex}, material);
        if (face >= 0) {
          roof.faces.append(face);
        }
      }
      continue;
    }

    /* The rectangle around the outline, along its longest edge or at the given angle. */
    float2 dir(std::cos(params.ridge_angle), std::sin(params.ridge_angle));
    if (params.auto_ridge) {
      float longest = -1.0f;
      for (const int i : flat.index_range()) {
        const float2 edge = flat[(i + 1) % flat.size()] - flat[i];
        if (math::length_squared(edge) > longest) {
          longest = math::length_squared(edge);
          dir = edge;
        }
      }
      dir = math::normalize(dir);
    }
    float2 perp(-dir.y, dir.x);
    float x_min = FLT_MAX, x_max = -FLT_MAX, y_min = FLT_MAX, y_max = -FLT_MAX;
    for (const float2 &p : flat) {
      x_min = std::min(x_min, math::dot(p, dir));
      x_max = std::max(x_max, math::dot(p, dir));
      y_min = std::min(y_min, math::dot(p, perp));
      y_max = std::max(y_max, math::dot(p, perp));
    }
    float a = (x_max - x_min) * 0.5f;
    float b = (y_max - y_min) * 0.5f;
    if (a < 1e-6f || b < 1e-6f) {
      continue;
    }
    if (best_area < a * b * 4.0f * 0.98f) {
      not_rect = true;
    }
    Frame frame;
    frame.center = dir * ((x_min + x_max) * 0.5f) + perp * ((y_min + y_max) * 0.5f);
    frame.z0 = z0;
    if (ELEM(params.shape, RoofShape::Hip, RoofShape::Mansard) && a < b) {
      /* The ridge of a hip roof runs along the long side. */
      const float2 old_dir = dir;
      dir = perp;
      perp = -old_dir;
      std::swap(a, b);
    }
    frame.dir = dir;
    frame.perp = perp;
    build_rect_roof(roof, params, frame, a, b, material);
  }
  if (not_rect) {
    r_message = "Outlines that are not rectangles get a roof over the rectangle around them";
  }
  return !roof.faces.is_empty();
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Finishing
 * \{ */

struct OpenEdge {
  int a, b;
  int face;
  float3 offset;
  /** Unit direction out of the face, in its plane. */
  float3 out;
  bool eave;
};

bool is_flat_normal(const float3 &n, const float flat_angle)
{
  return n.z > std::cos(std::clamp(flat_angle, 0.0f, 1.5f));
}

/** The open edges of a set of faces of a surface: edges used by one face of the set. */
Vector<OpenEdge> open_edges(const bd::Surface &surface,
                            const Span<int> faces,
                            const float flat_angle)
{
  Map<OrderedEdge, int> count;
  for (const int face : faces) {
    const Span<int> verts = surface.faces[face];
    for (const int i : verts.index_range()) {
      count.lookup_or_add(OrderedEdge(verts[i], verts[(i + 1) % verts.size()]), 0)++;
    }
  }
  Vector<OpenEdge> result;
  for (const int face : faces) {
    const float3 n = surface.face_normal(face);
    const bool flat = is_flat_normal(n, flat_angle);
    const Span<int> verts = surface.faces[face];
    for (const int i : verts.index_range()) {
      const int a = verts[i];
      const int b = verts[(i + 1) % verts.size()];
      if (count.lookup(OrderedEdge(a, b)) != 1) {
        continue;
      }
      const float3 d = surface.positions[b] - surface.positions[a];
      float3 out = math::cross(d, n);
      if (math::length_squared(out) < 1e-14f) {
        continue;
      }
      out = math::normalize(out);
      OpenEdge edge;
      edge.a = a;
      edge.b = b;
      edge.face = face;
      edge.out = out;
      edge.eave = flat || out.z < -0.25f;
      edge.offset = float3(0.0f);
      result.append(edge);
    }
  }
  return result;
}

/** Overhang strips along the open edges, and patches at the outer corners. */
void add_overhangs(Roof &roof,
                   const MeshRoofParams &params,
                   Vector<int> &r_strips,
                   Vector<int> &r_corners)
{
  Vector<OpenEdge> edges = open_edges(roof.surface, roof.faces, params.flat_angle);
  for (OpenEdge &edge : edges) {
    if (edge.eave) {
      const float level = math::length(float2(edge.out.x, edge.out.y));
      edge.offset = edge.out * (std::max(params.eave_overhang, 0.0f) / std::max(level, 0.2f));
    }
    else {
      edge.offset = edge.out * std::max(params.rake_overhang, 0.0f);
    }
  }
  Map<int, int> incoming;
  for (const int i : edges.index_range()) {
    incoming.add(edges[i].b, i);
  }
  bd::Surface &surface = roof.surface;
  for (const OpenEdge &edge : edges) {
    if (math::length_squared(edge.offset) < 1e-12f) {
      continue;
    }
    const float3 pa = surface.positions[edge.a];
    const float3 pb = surface.positions[edge.b];
    const int strip = surface.add_face_positions(
        {pa, pa + edge.offset, pb + edge.offset, pb}, surface.face_materials[edge.face]);
    if (strip >= 0) {
      r_strips.append(strip);
    }
  }
  for (const OpenEdge &next : edges) {
    const int *prev_index = incoming.lookup_ptr(next.a);
    if (prev_index == nullptr) {
      continue;
    }
    const OpenEdge &prev = edges[*prev_index];
    if (math::length_squared(prev.offset) < 1e-12f || math::length_squared(next.offset) < 1e-12f)
    {
      continue;
    }
    const float3 n_prev = surface.face_normal(prev.face);
    const float3 n_next = surface.face_normal(next.face);
    const float3 n = math::normalize(n_prev + n_next);
    if (math::dot(math::cross(prev.offset, next.offset), n) <= 0.0f) {
      continue;
    }
    const float3 v = surface.positions[next.a];
    int corner;
    if (prev.face == next.face || math::dot(n_prev, n_next) > 0.999f) {
      corner = surface.add_face_positions({v, v + prev.offset, v + prev.offset + next.offset,
                                           v + next.offset},
                                          surface.face_materials[prev.face]);
    }
    else {
      corner = surface.add_face_positions({v, v + prev.offset, v + next.offset},
                                          surface.face_materials[prev.face]);
    }
    if (corner >= 0) {
      r_corners.append(corner);
    }
  }
}

/** Level along the slope and up it, for UVs that run down the roof. */
void slope_frame(const float3 &n, float3 &r_u, float3 &r_v)
{
  r_u = math::normalize(bd::contour_direction(n));
  r_v = math::cross(n, r_u);
}

/** Give the faces a thickness under them. */
void add_slab(bd::Builder &builder,
              const bd::Surface &surface,
              const Span<int> faces,
              const float thickness,
              const MeshRoofParams &params)
{
  if (thickness <= 0.0f || faces.is_empty() || !builder.begin_board(0.0f)) {
    return;
  }
  Map<int, float3> normal_sums;
  Array<float3> face_normals(faces.size());
  for (const int i : faces.index_range()) {
    face_normals[i] = surface.face_normal(faces[i]);
    for (const int vert : surface.faces[faces[i]]) {
      normal_sums.lookup_or_add(vert, float3(0.0f)) += face_normals[i];
    }
  }
  Map<int, float> min_dots;
  for (const int i : faces.index_range()) {
    for (const int vert : surface.faces[faces[i]]) {
      const float3 vn = math::normalize(normal_sums.lookup(vert));
      const float d = math::dot(vn, face_normals[i]);
      float &value = min_dots.lookup_or_add(vert, 1.0f);
      value = std::min(value, d);
    }
  }
  Map<int, int2> verts;
  for (const auto item : normal_sums.items()) {
    const float3 vn = math::normalize(item.value);
    const float scale = thickness / std::max(min_dots.lookup(item.key), 0.35f);
    const float3 top = surface.positions[item.key];
    const int top_index = builder.add_vert(top);
    const int bottom_index = builder.add_vert(top - vn * scale);
    verts.add(item.key, int2(top_index, bottom_index));
  }

  Map<OrderedEdge, int> count;
  for (const int face : faces) {
    const Span<int> face_verts = surface.faces[face];
    for (const int i : face_verts.index_range()) {
      count.lookup_or_add(OrderedEdge(face_verts[i], face_verts[(i + 1) % face_verts.size()]),
                          0)++;
    }
  }
  const int trim = material_or(params.trim_material, 0);
  for (const int i : faces.index_range()) {
    const int face = faces[i];
    const int material = material_or(params.roof_material, surface.face_materials[face]);
    float3 u, v;
    slope_frame(face_normals[i], u, v);
    const Span<int> face_verts = surface.faces[face];
    Vector<int, 8> top, bottom;
    Vector<float2, 8> top_uvs, bottom_uvs;
    for (const int vert : face_verts) {
      const float3 &p = surface.positions[vert];
      top.append(verts.lookup(vert)[0]);
      top_uvs.append(float2(math::dot(p, u), math::dot(p, v)));
    }
    for (int k = face_verts.size() - 1; k >= 0; k--) {
      const float3 &p = surface.positions[face_verts[k]];
      bottom.append(verts.lookup(face_verts[k])[1]);
      bottom_uvs.append(float2(math::dot(p, u), math::dot(p, v)));
    }
    builder.add_face(top, top_uvs, material, int(RoofPart::Slope));
    builder.add_face(bottom, bottom_uvs, material, int(RoofPart::Underside));
    for (const int k : face_verts.index_range()) {
      const int a = face_verts[k];
      const int b = face_verts[(k + 1) % face_verts.size()];
      if (count.lookup(OrderedEdge(a, b)) != 1) {
        continue;
      }
      const float length = math::distance(surface.positions[a], surface.positions[b]);
      const int rim[4] = {verts.lookup(a)[0], verts.lookup(a)[1], verts.lookup(b)[1],
                          verts.lookup(b)[0]};
      const float2 rim_uvs[4] = {float2(0.0f, thickness),
                                 float2(0.0f, 0.0f),
                                 float2(length, 0.0f),
                                 float2(length, thickness)};
      builder.add_face(Span<int>(rim, 4), Span<float2>(rim_uvs, 4), trim, int(RoofPart::Fascia));
    }
  }
}

void add_covering(bd::Builder &builder,
                  const bd::Surface &surface,
                  const Span<int> faces,
                  const MeshRoofParams &params)
{
  if (params.covering == RoofCovering::None || faces.is_empty()) {
    return;
  }
  const float ct = std::max(params.covering_thickness, 1e-5f);
  const float tile = std::max(params.tile_width, 1e-4f);
  const float rows = std::max(params.row_spacing, 1e-4f);
  bd::Variation variation;
  variation.height = std::max(params.random_height, 0.0f);
  variation.tilt = std::max(params.tilt, 0.0f);
  variation.missing = std::clamp(params.missing, 0.0f, 1.0f);

  for (const Span<int> group : bd::coplanar_groups(surface, faces)) {
    if (builder.too_many) {
      break;
    }
    const float3 n = surface.face_normal(group[0]);
    bd::Fill fill;
    fill.material = material_or(params.covering_material, 0);
    fill.part = int(RoofPart::Covering);
    fill.seed = uint32_t(params.seed);
    fill.rows_from_min = true;
    fill.thickness = ct;
    if (is_flat_normal(n, params.flat_angle)) {
      if (!params.flat_roofing) {
        continue;
      }
      /* One sheet over the whole flat area. */
      const bd::Area area = bd::area_from_faces(surface, group, bd::auto_row_direction(n), 0.0f);
      bd::Board sheet;
      sheet.outline = bd::rect(area.box_min.x - 1.0f,
                               area.box_min.y - 1.0f,
                               area.box_max.x + 1.0f,
                               area.box_max.y + 1.0f);
      sheet.id = noise::hash(uint32_t(group[0]), uint32_t(params.seed), 811u);
      sheet.thickness = ct;
      sheet.material = fill.material;
      sheet.part = fill.part;
      bd::build_board(builder, area, sheet, {});
      continue;
    }
    float angle = 0.0f;
    switch (params.covering) {
      case RoofCovering::None:
        break;
      case RoofCovering::Tiles:
        fill.length = tile;
        fill.width = rows + std::max(params.overlap, 0.0f);
        fill.overlap = std::max(params.overlap, 0.0f);
        fill.lean = ct;
        fill.joints = bd::Joints::Half;
        fill.min_length = tile * 0.3f;
        fill.gap = params.covering_gap;
        break;
      case RoofCovering::Shingles:
        fill.thickness = ct * 0.6f;
        fill.length = tile;
        fill.width = rows + std::max(params.overlap, 0.0f);
        fill.overlap = std::max(params.overlap, 0.0f);
        fill.lean = ct * 0.6f;
        fill.joints = bd::Joints::Random;
        fill.random_length = 0.6f;
        fill.min_length = tile * 0.3f;
        fill.gap = params.covering_gap;
        break;
      case RoofCovering::Metal:
        /* Sheets running down the slope with raised seams between them. */
        angle = PI_F * 0.5f;
        fill.thickness = ct * 0.3f;
        fill.length = 1e6f;
        fill.width = tile;
        fill.joints = bd::Joints::None;
        fill.batten_width = ct * 1.5f;
        fill.batten_thickness = ct * 2.0f;
        break;
      case RoofCovering::Planks:
        fill.length = tile * 6.0f;
        fill.width = rows;
        fill.joints = bd::Joints::Random;
        fill.random_length = 0.3f;
        fill.min_length = rows;
        fill.gap = params.covering_gap;
        break;
    }
    const bd::Area area = bd::area_from_faces(
        surface, group, bd::contour_direction(n), angle);
    bd::fill_area(builder, area, fill, variation);
  }
}

/** The direction in the plane of \a face, away from the edge \a a to \a b and into the face. */
float3 into_face(const bd::Surface &surface, const int face, const float3 &a, const float3 &b)
{
  const float3 n = surface.face_normal(face);
  float3 w = math::normalize(math::cross(n, math::normalize(b - a)));
  if (math::dot(w, surface.face_center(face) - a) < 0.0f) {
    w = -w;
  }
  return w;
}

/** Caps on outward folds and flashing in inward folds, between the roof faces. */
void add_folds(bd::Builder &builder,
               const bd::Surface &surface,
               const Span<int> faces,
               const MeshRoofParams &params)
{
  if (!params.ridge_caps && !params.valleys) {
    return;
  }
  Map<OrderedEdge, Vector<int, 2>> edge_faces;
  for (const int face : faces) {
    const Span<int> verts = surface.faces[face];
    for (const int i : verts.index_range()) {
      edge_faces.lookup_or_add_default(OrderedEdge(verts[i], verts[(i + 1) % verts.size()]))
          .append(face);
    }
  }
  const float min_cos = std::cos(std::clamp(params.cap_angle, 0.0f, PI_F));
  const float ct = std::max(params.covering_thickness, 0.005f);
  const float lift = params.covering == RoofCovering::None ? 0.0f : ct * 2.0f;
  const int trim = material_or(params.trim_material, 0);
  for (const auto item : edge_faces.items()) {
    if (item.value.size() != 2) {
      continue;
    }
    const int f1 = item.value[0];
    const int f2 = item.value[1];
    const float3 n1 = surface.face_normal(f1);
    const float3 n2 = surface.face_normal(f2);
    if (math::dot(n1, n2) > min_cos) {
      continue;
    }
    const float3 a = surface.positions[item.key.v_low];
    const float3 b = surface.positions[item.key.v_high];
    const float3 middle = (a + b) * 0.5f;
    const bool outward = math::dot(n1, surface.face_center(f2) - middle) < 0.0f;
    if (outward ? !params.ridge_caps : !params.valleys) {
      continue;
    }
    const float3 w1 = into_face(surface, f1, a, b);
    const float3 w2 = into_face(surface, f2, a, b);
    const float3 bisector = math::normalize(n1 + n2);
    const float bend = std::max(math::dot(bisector, n1), 0.3f);
    if (outward) {
      const float half = std::max(params.cap_width, 0.0f) * 0.5f;
      const float3 inner1 = w1 * half + n1 * lift;
      const float3 inner2 = w2 * half + n2 * lift;
      const float3 apex = bisector * (lift / bend);
      const float3 profile[6] = {inner1 + n1 * ct,
                                 apex + bisector * (ct / bend),
                                 inner2 + n2 * ct,
                                 inner2,
                                 apex,
                                 inner1};
      bd::add_prism(builder, Span<float3>(profile, 6), a, b, trim, int(RoofPart::Ridge), true);
    }
    else {
      const float half = std::max(params.valley_width, 0.0f) * 0.5f;
      const float ft = std::max(ct * 0.25f, 0.002f);
      const float3 profile[6] = {w1 * half,
                                 float3(0.0f),
                                 w2 * half,
                                 w2 * half - n2 * ft,
                                 -bisector * (ft / bend),
                                 w1 * half - n1 * ft};
      bd::add_prism(builder, Span<float3>(profile, 6), a, b, trim, int(RoofPart::Valley));
    }
  }
}

/** Fascia boards and gutters along the outer edges. */
void add_edge_trim(bd::Builder &builder,
                   const bd::Surface &surface,
                   const Span<int> faces,
                   const MeshRoofParams &params)
{
  const bool fascia = params.fascia_height > 0.0f && params.fascia_thickness > 0.0f;
  if (!fascia && !params.gutters) {
    return;
  }
  const int trim = material_or(params.trim_material, 0);
  const float fh = std::max(params.fascia_height, 0.0f);
  const float ft = fascia ? std::max(params.fascia_thickness, 0.0f) : 0.0f;
  const float gs = std::max(params.gutter_size, 1e-3f);
  const float3 up(0.0f, 0.0f, 1.0f);
  for (const OpenEdge &edge : open_edges(surface, faces, params.flat_angle)) {
    const float3 a = surface.positions[edge.a];
    const float3 b = surface.positions[edge.b];
    float3 level(edge.out.x, edge.out.y, 0.0f);
    if (math::length_squared(level) < 1e-8f) {
      continue;
    }
    level = math::normalize(level);
    if (fascia) {
      const float3 profile[4] = {float3(0.0f), level * ft, level * ft - up * fh, -up * fh};
      bd::add_prism(builder, Span<float3>(profile, 4), a, b, trim, int(RoofPart::Fascia));
    }
    const float length = math::distance(a, b);
    if (params.gutters && edge.eave && std::abs(a.z - b.z) < length * 0.02f) {
      const float wall = gs * 0.08f;
      const float top = -std::max(params.thickness, 0.0f) * 0.3f;
      auto at = [&](const float x, const float y) { return level * x + up * (top + y); };
      const float3 profile[8] = {at(ft, 0.0f),
                                 at(ft, -gs),
                                 at(ft + gs, -gs),
                                 at(ft + gs, 0.0f),
                                 at(ft + gs - wall, 0.0f),
                                 at(ft + gs - wall, -gs + wall),
                                 at(ft + wall, -gs + wall),
                                 at(ft + wall, 0.0f)};
      bd::add_prism(builder, Span<float3>(profile, 8), a, b, trim, int(RoofPart::Gutter), true);
    }
  }
}

void add_walls_and_parapets(bd::Builder &builder, const Roof &roof, const MeshRoofParams &params)
{
  const int wall_material = material_or(params.wall_material, 0);
  if (!roof.walls.faces.is_empty() && builder.begin_board(0.0f)) {
    for (const int face : roof.walls.faces.index_range()) {
      const float3 n = roof.walls.face_normal(face);
      float3 u, v;
      slope_frame(n, u, v);
      Vector<int, 8> verts;
      Vector<float2, 8> uvs;
      for (const int vert : roof.walls.faces[face]) {
        const float3 &p = roof.walls.positions[vert];
        verts.append(builder.add_vert(p));
        uvs.append(float2(math::dot(p, u), math::dot(p, v)));
      }
      builder.add_face(verts, uvs, wall_material, int(RoofPart::Wall));
    }
  }
  const float height = std::max(params.parapet_height, 0.0f);
  const float width = std::max(params.parapet_width, 0.0f);
  if (height <= 0.0f || width <= 0.0f) {
    return;
  }
  const float3 up(0.0f, 0.0f, 1.0f);
  for (const Span<float3> outline : roof.parapets) {
    for (const int i : outline.index_range()) {
      const float3 a = outline[i];
      const float3 b = outline[(i + 1) % outline.size()];
      float3 inward = math::cross(up, b - a);
      if (math::length_squared(inward) < 1e-12f) {
        continue;
      }
      inward = math::normalize(inward) * width;
      const float3 profile[4] = {float3(0.0f), inward, inward + up * height, up * height};
      bd::add_prism(builder, Span<float3>(profile, 4), a, b, wall_material, int(RoofPart::Parapet));
    }
  }
}

/** \} */

}  // namespace

Mesh *mesh_roof(const Mesh &mesh, const MeshRoofParams &params, std::string &r_message)
{
  if (mesh.faces_num == 0) {
    return nullptr;
  }
  Roof roof;
  if (params.mode == RoofMode::Build) {
    if (!build(roof, mesh, params, r_message)) {
      return nullptr;
    }
  }
  else {
    roof.surface = bd::surface_from_mesh(mesh, params.selection);
    for (const int face : roof.surface.faces.index_range()) {
      const float3 n = roof.surface.face_normal(face);
      if (n.z < WALL_NORMAL_Z) {
        continue;
      }
      if (is_flat_normal(n, params.flat_angle) && !params.flat_roofing) {
        continue;
      }
      roof.faces.append(face);
    }
    if (roof.faces.is_empty()) {
      r_message = "No faces sloped like a roof to cover";
      return nullptr;
    }
  }

  const Vector<int> roof_faces = roof.faces;
  Vector<int> strips, corners;
  add_overhangs(roof, params, strips, corners);

  Vector<int> with_strips = roof_faces;
  with_strips.extend(strips);
  Vector<int> all = with_strips;
  all.extend(corners);
  Vector<int> overhangs = strips;
  overhangs.extend(corners);

  bd::Builder builder;
  add_slab(builder,
           roof.surface,
           params.mode == RoofMode::Build ? Span<int>(all) : Span<int>(overhangs),
           std::max(params.thickness, 0.0f),
           params);
  add_covering(builder, roof.surface, with_strips, params);
  add_folds(builder, roof.surface, roof_faces, params);
  add_edge_trim(builder, roof.surface, all, params);
  add_walls_and_parapets(builder, roof, params);
  if (builder.too_many) {
    r_message = "Too many pieces, only some were made. Make the tiles bigger";
  }

  bd::OutputNames names;
  names.part = params.part_id;
  names.random = params.random_id;
  const StringRef uv_name = mesh.active_or_default_uv_map_name();
  if (!uv_name.is_empty()) {
    names.uv_map = std::string(uv_name);
  }
  Mesh *roof_mesh = bd::builder_to_mesh(builder, names);
  if (roof_mesh == nullptr) {
    return nullptr;
  }
  return bd::join_meshes(&mesh, roof_mesh, names);
}

}  // namespace blender::geometry
