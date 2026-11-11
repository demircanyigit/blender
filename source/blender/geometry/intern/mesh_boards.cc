/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup geo
 */

#include <algorithm>
#include <cfloat>
#include <climits>
#include <cmath>

#include "DNA_mesh_types.h"

#include "BLI_array.hh"
#include "BLI_math_base.hh"
#include "BLI_math_vector.hh"
#include "BLI_noise.hh"
#include "BLI_ordered_edge.hh"

#include "BKE_attribute.hh"
#include "BKE_geometry_set.hh"
#include "BKE_mesh.h"
#include "BKE_mesh.hh"

#include "GEO_join_geometries.hh"

#include "mesh_boards.hh"

namespace blender::geometry::boards {

constexpr float PI_F = 3.14159265358979f;

/* -------------------------------------------------------------------- */
/** \name 2D Polygons
 * \{ */

float cross2(const float2 &a, const float2 &b)
{
  return a.x * b.y - a.y * b.x;
}

float signed_area(const Span<float2> poly)
{
  float area = 0.0f;
  for (const int i : poly.index_range()) {
    area += cross2(poly[i], poly[(i + 1) % poly.size()]);
  }
  return area * 0.5f;
}

float2 centroid(const Span<float2> poly)
{
  float2 sum(0.0f);
  for (const float2 &p : poly) {
    sum += p;
  }
  return sum / float(std::max<int64_t>(poly.size(), 1));
}

bool is_convex(const Span<float2> poly)
{
  if (poly.size() < 3) {
    return false;
  }
  for (const int i : poly.index_range()) {
    const float2 &a = poly[i];
    const float2 &b = poly[(i + 1) % poly.size()];
    const float2 &c = poly[(i + 2) % poly.size()];
    if (cross2(b - a, c - b) < -1e-9f) {
      return false;
    }
  }
  return true;
}

bool point_inside(const Span<float2> poly, const float2 &p)
{
  bool inside = false;
  for (int i = 0, j = int(poly.size()) - 1; i < poly.size(); j = i++) {
    const float2 &a = poly[i];
    const float2 &b = poly[j];
    if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) {
      inside = !inside;
    }
  }
  return inside;
}

Polygon clip(const Span<float2> subject, const Span<float2> clipper)
{
  Polygon result(subject);
  for (const int i : clipper.index_range()) {
    if (result.is_empty()) {
      break;
    }
    const float2 &a = clipper[i];
    const float2 &b = clipper[(i + 1) % clipper.size()];
    const Polygon input = std::move(result);
    result.clear();
    for (const int j : input.index_range()) {
      const float2 &p = input[j];
      const float2 &q = input[(j + 1) % input.size()];
      const float dp = cross2(b - a, p - a);
      const float dq = cross2(b - a, q - a);
      if (dp >= 0.0f) {
        result.append(p);
      }
      if ((dp >= 0.0f) != (dq >= 0.0f)) {
        result.append(p + (q - p) * (dp / (dp - dq)));
      }
    }
  }
  return result;
}

Polygon offset_convex(const Span<float2> poly, const float distance)
{
  const int n = poly.size();
  if (n < 3) {
    return {};
  }
  if (distance == 0.0f) {
    return Polygon(poly);
  }
  Array<float2> line_point(n);
  Array<float2> line_dir(n);
  for (const int i : IndexRange(n)) {
    const float2 edge = poly[(i + 1) % n] - poly[i];
    const float length = math::length(edge);
    if (length < 1e-12f) {
      return {};
    }
    line_dir[i] = edge / length;
    line_point[i] = poly[i] + float2(-line_dir[i].y, line_dir[i].x) * distance;
  }
  Polygon result;
  for (const int i : IndexRange(n)) {
    const int prev = (i + n - 1) % n;
    const float denom = cross2(line_dir[prev], line_dir[i]);
    if (std::abs(denom) < 1e-9f) {
      result.append(line_point[i]);
      continue;
    }
    const float t = cross2(line_point[i] - line_point[prev], line_dir[i]) / denom;
    result.append(line_point[prev] + line_dir[prev] * t);
  }
  for (const int i : IndexRange(n)) {
    const float2 new_edge = result[(i + 1) % n] - result[i];
    if (math::dot(new_edge, line_dir[i]) <= 0.0f) {
      return {};
    }
  }
  return result;
}

Polygon rect(const float x0, const float y0, const float x1, const float y1)
{
  return {float2(x0, y0), float2(x1, y0), float2(x1, y1), float2(x0, y1)};
}

Polygon oriented_rect(const float2 &center,
                      const float2 &axis,
                      const float length,
                      const float width)
{
  const float2 a = axis * (length * 0.5f);
  const float2 b = float2(-axis.y, axis.x) * (width * 0.5f);
  return {center - a - b, center + a - b, center + a + b, center - a + b};
}

float2 rotate(const float2 &p, const float angle)
{
  const float c = std::cos(angle);
  const float s = std::sin(angle);
  return float2(p.x * c - p.y * s, p.x * s + p.y * c);
}

static float triangle_wave(const float x, const float period)
{
  const float t = x / period - std::floor(x / period);
  return 1.0f - std::abs(t * 2.0f - 1.0f);
}

static int floor_div(const float value, const float step)
{
  return int(std::floor(value / step));
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Surfaces
 * \{ */

int Surface::vert(const float3 &position)
{
  constexpr double step = 1e-6;
  const VertKey key{int64_t(std::llround(double(position.x) / step)),
                    int64_t(std::llround(double(position.y) / step)),
                    int64_t(std::llround(double(position.z) / step))};
  return lookup_.lookup_or_add_cb(key, [&]() {
    positions.append(position);
    return int(positions.size() - 1);
  });
}

int Surface::add_face(const Span<int> verts, const int material, const int origin)
{
  Vector<int, 8> face;
  for (const int vert : verts) {
    if (face.is_empty() || face.last() != vert) {
      face.append(vert);
    }
  }
  while (face.size() > 1 && face.first() == face.last()) {
    face.remove_last();
  }
  if (face.size() < 3) {
    return -1;
  }
  faces.append(std::move(face));
  face_materials.append(material);
  face_origins.append(origin);
  return int(faces.size() - 1);
}

int Surface::add_face_positions(const Span<float3> face_positions,
                                const int material,
                                const int origin)
{
  Vector<int, 8> verts;
  for (const float3 &p : face_positions) {
    verts.append(this->vert(p));
  }
  return this->add_face(verts, material, origin);
}

float3 Surface::face_normal(const int face) const
{
  const Span<int> verts = faces[face];
  float3 normal(0.0f);
  for (const int i : verts.index_range()) {
    const float3 &a = positions[verts[i]];
    const float3 &b = positions[verts[(i + 1) % verts.size()]];
    normal.x += (a.y - b.y) * (a.z + b.z);
    normal.y += (a.z - b.z) * (a.x + b.x);
    normal.z += (a.x - b.x) * (a.y + b.y);
  }
  const float length = math::length(normal);
  return length > 1e-20f ? normal / length : float3(0.0f, 0.0f, 1.0f);
}

float3 Surface::face_center(const int face) const
{
  float3 sum(0.0f);
  for (const int vert : faces[face]) {
    sum += positions[vert];
  }
  return sum / float(faces[face].size());
}

Surface surface_from_mesh(const Mesh &mesh, const Span<bool> selection)
{
  Surface surface;
  const Span<float3> positions = mesh.vert_positions();
  const OffsetIndices<int> faces = mesh.faces();
  const Span<int> corner_verts = mesh.corner_verts();
  const VArraySpan<int> materials = *mesh.attributes().lookup_or_default<int>(
      "material_index", bke::AttrDomain::Face, 0);
  Array<int> map(mesh.verts_num, -1);
  for (const int face : faces.index_range()) {
    if (!selection.is_empty() && !selection[face]) {
      continue;
    }
    Vector<int, 8> verts;
    for (const int vert : corner_verts.slice(faces[face])) {
      if (map[vert] < 0) {
        map[vert] = surface.vert(positions[vert]);
      }
      verts.append(map[vert]);
    }
    surface.add_face(verts, materials[face], face);
  }
  return surface;
}

/** For each edge of the given faces, the faces (indices into \a faces) using it. */
static Map<OrderedEdge, Vector<int, 2>> edge_faces(const Surface &surface, const Span<int> faces)
{
  Map<OrderedEdge, Vector<int, 2>> map;
  for (const int i : faces.index_range()) {
    const Span<int> verts = surface.faces[faces[i]];
    for (const int corner : verts.index_range()) {
      const OrderedEdge edge(verts[corner], verts[(corner + 1) % verts.size()]);
      map.lookup_or_add_default(edge).append(i);
    }
  }
  return map;
}

Vector<Vector<int>> coplanar_groups(const Surface &surface, const Span<int> faces)
{
  const Map<OrderedEdge, Vector<int, 2>> map = edge_faces(surface, faces);
  Array<float3> normals(faces.size());
  Array<float> planes(faces.size());
  for (const int i : faces.index_range()) {
    normals[i] = surface.face_normal(faces[i]);
    planes[i] = math::dot(normals[i], surface.face_center(faces[i]));
  }
  Array<bool> done(faces.size(), false);
  Vector<Vector<int>> groups;
  for (const int start : faces.index_range()) {
    if (done[start]) {
      continue;
    }
    Vector<int> group;
    Vector<int> stack = {start};
    done[start] = true;
    while (!stack.is_empty()) {
      const int i = stack.pop_last();
      group.append(faces[i]);
      const Span<int> verts = surface.faces[faces[i]];
      for (const int corner : verts.index_range()) {
        const OrderedEdge edge(verts[corner], verts[(corner + 1) % verts.size()]);
        for (const int other : map.lookup(edge)) {
          if (done[other]) {
            continue;
          }
          if (math::dot(normals[i], normals[other]) < 0.99996f) {
            continue;
          }
          const float distance = std::abs(
              math::dot(normals[i], surface.face_center(faces[other])) - planes[i]);
          if (distance > 1e-4f) {
            continue;
          }
          done[other] = true;
          stack.append(other);
        }
      }
    }
    groups.append(std::move(group));
  }
  return groups;
}

Vector<Vector<int>> boundary_loops(const Surface &surface, const Span<int> faces)
{
  const Map<OrderedEdge, Vector<int, 2>> map = edge_faces(surface, faces);
  Vector<int2> edges;
  Map<int, Vector<int, 2>> outgoing;
  for (const int face : faces) {
    const Span<int> verts = surface.faces[face];
    for (const int corner : verts.index_range()) {
      const int a = verts[corner];
      const int b = verts[(corner + 1) % verts.size()];
      if (map.lookup(OrderedEdge(a, b)).size() != 1) {
        continue;
      }
      outgoing.lookup_or_add_default(a).append(int(edges.size()));
      edges.append(int2(a, b));
    }
  }
  Array<bool> used(edges.size(), false);
  Vector<Vector<int>> loops;
  for (const int start : edges.index_range()) {
    if (used[start]) {
      continue;
    }
    Vector<int> loop;
    int edge = start;
    while (edge >= 0 && !used[edge]) {
      used[edge] = true;
      loop.append(edges[edge][0]);
      const int next_vert = edges[edge][1];
      edge = -1;
      if (next_vert == loop.first()) {
        break;
      }
      if (const Vector<int, 2> *candidates = outgoing.lookup_ptr(next_vert)) {
        for (const int candidate : *candidates) {
          if (!used[candidate]) {
            edge = candidate;
            break;
          }
        }
      }
    }
    if (loop.size() >= 3) {
      loops.append(std::move(loop));
    }
  }
  return loops;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Areas
 * \{ */

float3 Area::to_3d(const float2 &q, const float z) const
{
  return origin + u * q.x + v * q.y + n * z;
}

float2 Area::to_2d(const float3 &p) const
{
  const float3 d = p - origin;
  return float2(math::dot(d, u), math::dot(d, v));
}

void Area::add_face(Polygon poly)
{
  if (poly.size() < 3) {
    return;
  }
  const float area = signed_area(poly);
  if (std::abs(area) < 1e-12f) {
    return;
  }
  if (area < 0.0f) {
    std::reverse(poly.begin(), poly.end());
  }
  float2 min(FLT_MAX), max(-FLT_MAX);
  for (const float2 &q : poly) {
    min = math::min(min, q);
    max = math::max(max, q);
  }
  if (faces.is_empty()) {
    box_min = min;
    box_max = max;
  }
  else {
    box_min = math::min(box_min, min);
    box_max = math::max(box_max, max);
  }
  face_min.append(min);
  face_max.append(max);
  faces.append(std::move(poly));
}

bool Area::contains(const float2 &p) const
{
  for (const int i : faces.index_range()) {
    if (p.x < face_min[i].x || p.y < face_min[i].y || p.x > face_max[i].x ||
        p.y > face_max[i].y)
    {
      continue;
    }
    if (point_inside(faces[i], p)) {
      return true;
    }
  }
  return false;
}

Vector<float2> line_intervals(const Span<Polygon> loops, const float c, const bool vertical)
{
  Vector<float> hits;
  for (const Polygon &loop : loops) {
    for (const int i : loop.index_range()) {
      float2 a = loop[i];
      float2 b = loop[(i + 1) % loop.size()];
      if (vertical) {
        a = float2(a.y, a.x);
        b = float2(b.y, b.x);
      }
      if ((a.y > c) != (b.y > c)) {
        hits.append(a.x + (c - a.y) * (b.x - a.x) / (b.y - a.y));
      }
    }
  }
  std::sort(hits.begin(), hits.end());
  Vector<float2> result;
  for (int i = 0; i + 1 < hits.size(); i += 2) {
    if (hits[i + 1] > hits[i]) {
      result.append(float2(hits[i], hits[i + 1]));
    }
  }
  return result;
}

Vector<float2> Area::intervals(const float y, const bool vertical) const
{
  return line_intervals(loops, y, vertical);
}

/** The union of the intervals of a band, found at its middle and near its two sides. */
static Vector<float2> band_intervals(const Span<Polygon> loops,
                                     const float c0,
                                     const float c1,
                                     const bool vertical)
{
  Vector<float2> all;
  const float inset = (c1 - c0) * 0.02f;
  for (const float c : {c0 + inset, (c0 + c1) * 0.5f, c1 - inset}) {
    all.extend(line_intervals(loops, c, vertical));
  }
  std::sort(all.begin(), all.end(), [](const float2 &a, const float2 &b) { return a.x < b.x; });
  Vector<float2> merged;
  for (const float2 &interval : all) {
    if (!merged.is_empty() && interval.x <= merged.last().y) {
      merged.last().y = std::max(merged.last().y, interval.y);
    }
    else {
      merged.append(interval);
    }
  }
  return merged;
}

float3 auto_row_direction(const float3 &normal)
{
  if (std::abs(normal.z) < 0.7f) {
    return math::cross(float3(0.0f, 0.0f, 1.0f), normal);
  }
  return float3(1.0f, 0.0f, 0.0f);
}

float3 contour_direction(const float3 &normal)
{
  const float3 level = math::cross(float3(0.0f, 0.0f, 1.0f), normal);
  if (math::length_squared(level) > 1e-10f) {
    return level;
  }
  return auto_row_direction(normal);
}

Area area_from_faces(const Surface &surface,
                     const Span<int> faces,
                     const float3 &u_hint,
                     const float angle)
{
  Area area;
  float3 normal(0.0f);
  float3 center(0.0f);
  for (const int face : faces) {
    normal += surface.face_normal(face);
    center += surface.face_center(face);
  }
  area.n = math::normalize(normal);
  center /= float(std::max<int64_t>(faces.size(), 1));
  auto project = [&](const float3 &dir) { return dir - area.n * math::dot(area.n, dir); };
  float3 u = project(u_hint);
  if (math::length_squared(u) < 1e-10f) {
    u = project(float3(1.0f, 0.0f, 0.0f));
  }
  if (math::length_squared(u) < 1e-10f) {
    u = project(float3(0.0f, 1.0f, 0.0f));
  }
  u = math::normalize(u);
  float3 v = math::cross(area.n, u);
  if (angle != 0.0f) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    const float3 u2 = u * c + v * s;
    v = v * c - u * s;
    u = u2;
  }
  area.u = u;
  area.v = v;
  area.origin = area.n * math::dot(center, area.n);
  for (const int face : faces) {
    Polygon poly;
    for (const int vert : surface.faces[face]) {
      poly.append(area.to_2d(surface.positions[vert]));
    }
    area.add_face(std::move(poly));
  }
  for (const Span<int> loop : boundary_loops(surface, faces)) {
    Polygon poly;
    for (const int vert : loop) {
      poly.append(area.to_2d(surface.positions[vert]));
    }
    area.loops.append(std::move(poly));
  }
  return area;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Mesh Output
 * \{ */

bool Builder::begin_board(const float random)
{
  if (boards_num >= max_boards) {
    too_many = true;
    return false;
  }
  current_board = boards_num++;
  current_random = random;
  return true;
}

int Builder::add_vert(const float3 &position)
{
  positions.append(position);
  vert_board.append(current_board);
  vert_random.append(current_random);
  return int(positions.size() - 1);
}

void Builder::add_face(const Span<int> verts,
                       const Span<float2> uvs,
                       const int material,
                       const int part,
                       const bool smooth)
{
  face_sizes.append(verts.size());
  face_materials.append(material);
  face_parts.append(part);
  face_smooth.append(smooth);
  corner_verts.extend(verts);
  corner_uvs.extend(uvs);
}

void Builder::rotate_verts(const int first_vert,
                           const float3 &pivot,
                           const float3 &axis,
                           const float angle)
{
  const float c = std::cos(angle);
  const float s = std::sin(angle);
  for (const int i : IndexRange(first_vert, positions.size() - first_vert)) {
    const float3 d = positions[i] - pivot;
    positions[i] = pivot + d * c + math::cross(axis, d) * s +
                   axis * (math::dot(axis, d) * (1.0f - c));
  }
}

Mesh *builder_to_mesh(const Builder &builder, const OutputNames &names)
{
  if (builder.face_sizes.is_empty()) {
    return nullptr;
  }
  Mesh *mesh = bke::mesh_new_no_attributes(builder.positions.size(),
                                           0,
                                           builder.face_sizes.size(),
                                           builder.corner_verts.size());
  mesh->vert_positions_for_write().copy_from(builder.positions);
  MutableSpan<int> offsets = mesh->face_offsets_for_write();
  offsets[0] = 0;
  for (const int i : builder.face_sizes.index_range()) {
    offsets[i + 1] = offsets[i] + builder.face_sizes[i];
  }
  mesh->corner_verts_for_write().copy_from(builder.corner_verts);
  bke::mesh_calc_edges(*mesh, false, false);

  bke::MutableAttributeAccessor attributes = mesh->attributes_for_write();
  attributes.add<int>("material_index",
                      bke::AttrDomain::Face,
                      bke::AttributeInitVArray(VArray<int>::from_span(builder.face_materials)));
  bool all_smooth = true;
  for (const bool smooth : builder.face_smooth) {
    all_smooth &= smooth;
  }
  if (!all_smooth) {
    bke::SpanAttributeWriter<bool> sharp = attributes.lookup_or_add_for_write_only_span<bool>(
        "sharp_face", bke::AttrDomain::Face);
    for (const int i : builder.face_smooth.index_range()) {
      sharp.span[i] = !builder.face_smooth[i];
    }
    sharp.finish();
  }
  if (!names.uv_map.empty()) {
    attributes.add<float2>(
        names.uv_map,
        bke::AttrDomain::Corner,
        bke::AttributeInitVArray(VArray<float2>::from_span(builder.corner_uvs)));
  }
  if (names.board_index) {
    attributes.add<int>(*names.board_index,
                        bke::AttrDomain::Point,
                        bke::AttributeInitVArray(VArray<int>::from_span(builder.vert_board)));
  }
  if (names.random) {
    attributes.add<float>(*names.random,
                          bke::AttrDomain::Point,
                          bke::AttributeInitVArray(VArray<float>::from_span(builder.vert_random)));
  }
  if (names.part) {
    attributes.add<int>(*names.part,
                        bke::AttrDomain::Face,
                        bke::AttributeInitVArray(VArray<int>::from_span(builder.face_parts)));
  }
  return mesh;
}

Mesh *join_meshes(const Mesh *base, Mesh *added, const OutputNames &names)
{
  if (base == nullptr) {
    return added;
  }
  if (added == nullptr) {
    return nullptr;
  }
  Mesh *base_copy = BKE_mesh_copy_for_eval(*base);
  bke::MutableAttributeAccessor attributes = base_copy->attributes_for_write();
  auto fill_int = [&](const std::optional<std::string> &name,
                      const bke::AttrDomain domain,
                      const int value) {
    if (!name) {
      return;
    }
    attributes.remove(*name);
    bke::SpanAttributeWriter<int> writer = attributes.lookup_or_add_for_write_only_span<int>(
        *name, domain);
    if (writer) {
      writer.span.fill(value);
      writer.finish();
    }
  };
  fill_int(names.board_index, bke::AttrDomain::Point, -1);
  fill_int(names.part, bke::AttrDomain::Face, -1);
  if (names.random) {
    attributes.remove(*names.random);
    attributes.add<float>(*names.random, bke::AttrDomain::Point, bke::AttributeInitDefaultValue());
  }
  const Vector<bke::GeometrySet> parts = {bke::GeometrySet::from_mesh(base_copy),
                                          bke::GeometrySet::from_mesh(added)};
  bke::GeometrySet joined = join_geometries(parts, {});
  return joined.get_component_for_write<bke::MeshComponent>().release();
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Boards
 * \{ */

void build_board(Builder &builder,
                 const Area &area,
                 const Board &board,
                 const Variation &variation)
{
  if (board.outline.size() < 3 || builder.too_many || board.thickness <= 0.0f) {
    return;
  }
  if (variation.missing > 0.0f && noise::hash_to_float(board.id, 1) < variation.missing) {
    return;
  }
  const float2 a = math::normalize(board.axis);
  const float2 b(-a.y, a.x);
  float s_min = FLT_MAX, s_max = -FLT_MAX, t_min = FLT_MAX, t_max = -FLT_MAX;
  float2 box_min(FLT_MAX), box_max(-FLT_MAX);
  for (const float2 &p : board.outline) {
    s_min = std::min(s_min, math::dot(p, a));
    s_max = std::max(s_max, math::dot(p, a));
    t_min = std::min(t_min, math::dot(p, b));
    t_max = std::max(t_max, math::dot(p, b));
    box_min = math::min(box_min, p);
    box_max = math::max(box_max, p);
  }
  const float length = s_max - s_min;
  const float width = t_max - t_min;
  if (length < 1e-7f || width < 1e-7f) {
    return;
  }

  Vector<int, 16> candidates;
  for (const int i : area.faces.index_range()) {
    if (area.face_max[i].x < box_min.x || area.face_min[i].x > box_max.x ||
        area.face_max[i].y < box_min.y || area.face_min[i].y > box_max.y)
    {
      continue;
    }
    candidates.append(i);
  }
  if (candidates.is_empty()) {
    return;
  }

  /* Cut the board into cells so cupping and bowing can bend it, then each cell to the faces. */
  const int ns = variation.bow > 0.0f ? std::clamp(int(length / width * 2.0f), 4, 16) : 1;
  const int nt = variation.cup > 0.0f ? 4 : 1;
  const float min_area = length * width * 1e-6f;
  Vector<Polygon> pieces;
  for (const int i : IndexRange(ns)) {
    const float s0 = s_min + length * float(i) / float(ns);
    const float s1 = s_min + length * float(i + 1) / float(ns);
    for (const int j : IndexRange(nt)) {
      const float t0 = t_min + width * float(j) / float(nt);
      const float t1 = t_min + width * float(j + 1) / float(nt);
      const Polygon cell = {a * s0 + b * t0, a * s1 + b * t0, a * s1 + b * t1, a * s0 + b * t1};
      const Polygon cell_poly = clip(board.outline, cell);
      if (cell_poly.size() < 3) {
        continue;
      }
      for (const int face : candidates) {
        Polygon piece = clip(area.faces[face], cell_poly);
        if (piece.size() >= 3 && std::abs(signed_area(piece)) > min_area) {
          pieces.append(std::move(piece));
        }
      }
    }
  }
  if (pieces.is_empty()) {
    return;
  }

  /* Weld the corners shared by the pieces. */
  const float step = std::max(std::max(length, width) * 1e-5f, 1e-7f);
  Map<int2, int> weld;
  Vector<float2> points;
  Vector<Vector<int, 8>> piece_points;
  for (const Polygon &piece : pieces) {
    Vector<int, 8> indices;
    for (const float2 &p : piece) {
      const int2 key(int(std::lround(p.x / step)), int(std::lround(p.y / step)));
      const int index = weld.lookup_or_add_cb(key, [&]() {
        points.append(p);
        return int(points.size() - 1);
      });
      if (indices.is_empty() || indices.last() != index) {
        indices.append(index);
      }
    }
    while (indices.size() > 1 && indices.first() == indices.last()) {
      indices.remove_last();
    }
    if (indices.size() >= 3) {
      piece_points.append(std::move(indices));
    }
  }
  if (piece_points.is_empty()) {
    return;
  }

  if (!builder.begin_board(noise::hash_to_float(board.id, 2))) {
    return;
  }
  const float jitter = (noise::hash_to_float(board.id, 3) * 2.0f - 1.0f) * variation.height;
  auto height = [&](const float2 &p) {
    const float s = std::clamp((math::dot(p, a) - s_min) / length, 0.0f, 1.0f);
    const float t = std::clamp((math::dot(p, b) - t_min) / width, 0.0f, 1.0f);
    const float cup = variation.cup * math::square(t * 2.0f - 1.0f);
    const float bow = variation.bow * (1.0f - math::square(s * 2.0f - 1.0f));
    return board.z0 + jitter + cup + bow + board.lean * (1.0f - t);
  };
  const int first_vert = builder.positions.size();
  const int count = points.size();
  for (const float2 &p : points) {
    builder.add_vert(area.to_3d(p, height(p)));
  }
  for (const float2 &p : points) {
    builder.add_vert(area.to_3d(p, height(p) + board.thickness));
  }
  if (variation.tilt > 0.0f) {
    const float turn = noise::hash_to_float(board.id, 6) * 2.0f * PI_F;
    const float3 axis = area.u * std::cos(turn) + area.v * std::sin(turn);
    const float2 middle = centroid(board.outline);
    builder.rotate_verts(first_vert,
                         area.to_3d(middle, height(middle) + board.thickness * 0.5f),
                         axis,
                         noise::hash_to_float(board.id, 5) * variation.tilt);
  }

  const bool flip = noise::hash_to_float(board.id, 7) < 0.5f;
  const float2 uv_offset(noise::hash_to_float(board.id, 8) * 2.0f,
                         noise::hash_to_float(board.id, 9) * 2.0f);
  auto uv = [&](const float2 &p) {
    const float s = math::dot(p, a) - s_min;
    return float2(flip ? length - s : s, math::dot(p, b) - t_min) + uv_offset;
  };
  const float probe = std::max(std::min(length, width) * 1e-3f, 1e-6f);
  auto inside = [&](const float2 &p) {
    if (!point_inside(board.outline, p)) {
      return false;
    }
    for (const int face : candidates) {
      if (point_inside(area.faces[face], p)) {
        return true;
      }
    }
    return false;
  };

  for (const Span<int> indices : piece_points) {
    Vector<int, 8> verts;
    Vector<float2, 8> uvs;
    /* Top. */
    for (const int i : indices) {
      verts.append(first_vert + count + i);
      uvs.append(uv(points[i]));
    }
    builder.add_face(verts, uvs, board.material, board.part);
    /* Bottom. */
    verts.clear();
    uvs.clear();
    for (int k = indices.size() - 1; k >= 0; k--) {
      verts.append(first_vert + indices[k]);
      uvs.append(uv(points[indices[k]]));
    }
    builder.add_face(verts, uvs, board.material, board.part);
    /* Sides, only where the board ends. */
    for (const int k : indices.index_range()) {
      const int i0 = indices[k];
      const int i1 = indices[(k + 1) % indices.size()];
      const float2 &p0 = points[i0];
      const float2 &p1 = points[i1];
      const float2 d = p1 - p0;
      const float edge_length = math::length(d);
      if (edge_length < step) {
        continue;
      }
      const float2 out(d.y / edge_length, -d.x / edge_length);
      if (inside((p0 + p1) * 0.5f + out * probe)) {
        continue;
      }
      const float u0 = math::dot(p0, a) - s_min + math::dot(p0, b) - t_min;
      const float u1 = math::dot(p1, a) - s_min + math::dot(p1, b) - t_min;
      const int side[4] = {first_vert + i0, first_vert + i1, first_vert + count + i1,
                           first_vert + count + i0};
      const float2 side_uvs[4] = {float2(u0, 0.0f) + uv_offset,
                                  float2(u1, 0.0f) + uv_offset,
                                  float2(u1, board.thickness) + uv_offset,
                                  float2(u0, board.thickness) + uv_offset};
      builder.add_face(
          Span<int>(side, 4), Span<float2>(side_uvs, 4), board.material, board.part);
    }
  }
}

static int pick_material(const Fill &fill, const uint32_t id)
{
  if (fill.material_count <= 1) {
    return fill.material;
  }
  const int pick = int(noise::hash_to_float(id, 20) * float(fill.material_count));
  return fill.material + std::min(pick, fill.material_count - 1);
}

bool fill_area(Builder &builder, const Area &area, const Fill &fill, const Variation &variation)
{
  if (area.faces.is_empty()) {
    return true;
  }
  const float g = std::max(fill.gap, 0.0f) * 0.5f;
  const float width = std::max(fill.width, 1e-5f);
  const float length = std::max(fill.length, 1e-5f);
  const uint32_t seed = noise::hash(fill.seed + 1u, uint32_t(fill.layout) + 31u);

  auto add = [&](const Span<float2> outline,
                 const float2 &axis,
                 const uint32_t id,
                 const float z0,
                 const float thickness,
                 const float lean,
                 const Polygon *limit) {
    if (builder.too_many || outline.size() < 3) {
      return;
    }
    Board board;
    board.outline = limit ? clip(outline, *limit) : Polygon(outline);
    if (board.outline.size() < 3) {
      return;
    }
    float2 min(FLT_MAX), max(-FLT_MAX);
    for (const float2 &p : board.outline) {
      min = math::min(min, p);
      max = math::max(max, p);
    }
    if (max.x < area.box_min.x || min.x > area.box_max.x || max.y < area.box_min.y ||
        min.y > area.box_max.y)
    {
      return;
    }
    board.axis = axis;
    board.id = id;
    board.z0 = z0;
    board.thickness = thickness;
    board.lean = lean;
    board.material = pick_material(fill, id);
    board.part = fill.part;
    build_board(builder, area, board, variation);
  };

  /* Border: one board along each edge of a convex outline, and the rest of the fill inside. */
  bool border_ok = true;
  Polygon inner;
  const Polygon *limit = nullptr;
  if (fill.border) {
    const bool convex = area.loops.size() == 1 && signed_area(area.loops[0]) > 0.0f &&
                        is_convex(area.loops[0]);
    if (convex) {
      const Polygon &outer = area.loops[0];
      inner = offset_convex(outer, width);
      if (inner.size() == outer.size()) {
        for (const int i : outer.index_range()) {
          const int next = (i + 1) % outer.size();
          const Polygon quad = {outer[i], outer[next], inner[next], inner[i]};
          const Polygon shrunk = offset_convex(quad, g);
          const float2 edge = outer[next] - outer[i];
          if (shrunk.size() >= 3 && math::length_squared(edge) > 1e-12f) {
            add(shrunk,
                math::normalize(edge),
                noise::hash(seed, uint32_t(i), 9001u),
                fill.z0,
                fill.thickness,
                0.0f,
                nullptr);
          }
        }
        limit = &inner;
      }
      else {
        border_ok = false;
      }
    }
    else {
      border_ok = false;
    }
  }
  const Span<Polygon> loops = limit ? Span<Polygon>(limit, 1) : Span<Polygon>(area.loops);

  /* The box of the pattern turned by \a angle, to cover the area after turning back. */
  auto turned_box = [&](const float angle, float2 &r_min, float2 &r_max) {
    r_min = float2(FLT_MAX);
    r_max = float2(-FLT_MAX);
    for (const float2 &corner : {area.box_min,
                                 area.box_max,
                                 float2(area.box_min.x, area.box_max.y),
                                 float2(area.box_max.x, area.box_min.y)})
    {
      const float2 p = rotate(corner, -angle);
      r_min = math::min(r_min, p);
      r_max = math::max(r_max, p);
    }
  };
  auto add_turned = [&](Polygon outline, const float2 &axis, const uint32_t id, const float angle) {
    Polygon shrunk = offset_convex(outline, g);
    for (float2 &p : shrunk) {
      p = rotate(p, angle);
    }
    add(shrunk, rotate(axis, angle), id, fill.z0, fill.thickness, 0.0f, limit);
  };

  switch (fill.layout) {
    case Layout::Rows: {
      const float overlap = std::clamp(fill.overlap, 0.0f, width * 0.9f);
      const float pitch = width - overlap;
      const float gy = overlap > 0.0f ? 0.0f : g;
      const float y_origin = fill.rows_from_min ? area.box_min.y : 0.0f;
      const int first = floor_div(area.box_min.y - y_origin - width, pitch);
      const int last = floor_div(area.box_max.y - y_origin, pitch) + 1;
      const float min_piece = std::max(fill.min_length, g * 2.0f + 1e-5f);
      for (int r = first; r <= last && !builder.too_many; r++) {
        const float y0 = y_origin + float(r) * pitch;
        const float y1 = y0 + width;
        const uint32_t row_id = noise::hash(uint32_t(r), seed, 77u);
        const Vector<float2> intervals = band_intervals(loops, y0, y1, false);
        float offset = 0.0f;
        switch (fill.joints) {
          case Joints::Random:
            offset = noise::hash_to_float(row_id, 1) * length;
            break;
          case Joints::Half:
            offset = (r & 1) ? length * 0.5f : 0.0f;
            break;
          case Joints::Third:
            offset = float(((r % 3) + 3) % 3) * length / 3.0f;
            break;
          case Joints::None:
            break;
        }
        for (const int k : intervals.index_range()) {
          const float xa = intervals[k].x;
          const float xb = intervals[k].y;
          const float ext_left = k > 0 ? std::min(width * 2.0f, (xa - intervals[k - 1].y) * 0.5f) :
                                         width * 2.0f;
          const float ext_right = k + 1 < intervals.size() ?
                                      std::min(width * 2.0f, (intervals[k + 1].x - xb) * 0.5f) :
                                      width * 2.0f;
          Vector<float> cuts = {xa - ext_left};
          if (fill.joints != Joints::None) {
            const int j_first = floor_div(xa - offset, length) - 1;
            const int j_last = floor_div(xb - offset, length) + 1;
            for (int j = j_first; j <= j_last; j++) {
              const float jitter = (noise::hash_to_float(row_id, uint32_t(j) * 2u + 11u) - 0.5f) *
                                   fill.random_length * length * 0.8f;
              const float x = float(j) * length + offset + jitter;
              if (x <= xa + min_piece || x >= xb - min_piece) {
                continue;
              }
              if (cuts.size() > 1 && x - cuts.last() < min_piece) {
                continue;
              }
              cuts.append(x);
            }
          }
          cuts.append(xb + ext_right);
          for (const int c : IndexRange(cuts.size() - 1)) {
            const float x0 = cuts[c] + (c == 0 ? 0.0f : g);
            const float x1 = cuts[c + 1] - (c + 2 == cuts.size() ? 0.0f : g);
            if (x1 - x0 <= 1e-6f) {
              continue;
            }
            add(rect(x0, y0 + gy, x1, y1 - gy),
                float2(1.0f, 0.0f),
                noise::hash(row_id, uint32_t(k), uint32_t(c)),
                fill.z0,
                fill.thickness,
                fill.lean,
                limit);
          }
        }
        /* A strip over the joint under this row. */
        if (fill.batten_width > 0.0f && fill.batten_thickness > 0.0f) {
          const float half = fill.batten_width * 0.5f;
          for (const float2 &interval : band_intervals(loops, y0 - half, y0 + half, false)) {
            add(rect(interval.x - width * 2.0f, y0 - half, interval.y + width * 2.0f, y0 + half),
                float2(1.0f, 0.0f),
                noise::hash(row_id, 5003u, uint32_t(std::lround(interval.x * 1000.0f))),
                fill.z0 + fill.thickness,
                fill.batten_thickness,
                0.0f,
                limit);
          }
        }
      }
      break;
    }
    case Layout::Herringbone: {
      const float angle = PI_F * 0.25f;
      float2 min, max;
      turned_box(angle, min, max);
      const float w = width;
      const float l = std::max(length, w * 1.01f);
      const float2 a(w, w);
      const float2 b(l, -l);
      const float det = cross2(a, b);
      int k_min = INT_MAX, k_max = INT_MIN, m_min = INT_MAX, m_max = INT_MIN;
      for (const float2 &corner : {min, max, float2(min.x, max.y), float2(max.x, min.y)}) {
        const float k = cross2(corner, b) / det;
        const float m = cross2(a, corner) / det;
        k_min = std::min(k_min, int(std::floor(k)));
        k_max = std::max(k_max, int(std::ceil(k)));
        m_min = std::min(m_min, int(std::floor(m)));
        m_max = std::max(m_max, int(std::ceil(m)));
      }
      for (int k = k_min - 2; k <= k_max + 2 && !builder.too_many; k++) {
        for (int m = m_min - 2; m <= m_max + 2; m++) {
          const float2 o = a * float(k) + b * float(m);
          add_turned(rect(o.x, o.y, o.x + l, o.y + w),
                     float2(1.0f, 0.0f),
                     noise::hash(uint32_t(k), uint32_t(m) * 2u, seed),
                     angle);
          add_turned(rect(o.x, o.y + w, o.x + w, o.y + w + l),
                     float2(0.0f, 1.0f),
                     noise::hash(uint32_t(k), uint32_t(m) * 2u + 1u, seed),
                     angle);
        }
      }
      break;
    }
    case Layout::Chevron: {
      const float column = length * 0.70710678f;
      const float pitch = width * 1.41421356f;
      auto shift = [&](const float x) { return triangle_wave(x, column * 2.0f) * column; };
      const int first_col = floor_div(area.box_min.x, column) - 1;
      const int last_col = floor_div(area.box_max.x, column) + 1;
      const int first_row = floor_div(area.box_min.y - column, pitch) - 1;
      const int last_row = floor_div(area.box_max.y, pitch) + 1;
      for (int j = first_col; j <= last_col && !builder.too_many; j++) {
        const float x0 = float(j) * column;
        const float x1 = float(j + 1) * column;
        const float2 axis = math::normalize(float2(x1 - x0, shift(x1) - shift(x0)));
        for (int k = first_row; k <= last_row; k++) {
          const float y = float(k) * pitch;
          const Polygon poly = {float2(x0, y + shift(x0)),
                                float2(x1, y + shift(x1)),
                                float2(x1, y + pitch + shift(x1)),
                                float2(x0, y + pitch + shift(x0))};
          add(offset_convex(poly, g),
              axis,
              noise::hash(uint32_t(j), uint32_t(k), seed),
              fill.z0,
              fill.thickness,
              0.0f,
              limit);
        }
      }
      break;
    }
    case Layout::Basket: {
      const int n = std::max(1, int(std::round(length / width)));
      const float square = width * float(n);
      const float strip = width;
      for (int i = floor_div(area.box_min.x, square) - 1;
           i <= floor_div(area.box_max.x, square) + 1 && !builder.too_many;
           i++)
      {
        for (int j = floor_div(area.box_min.y, square) - 1;
             j <= floor_div(area.box_max.y, square) + 1;
             j++)
        {
          const float x = float(i) * square;
          const float y = float(j) * square;
          const bool across = ((i + j) & 1) != 0;
          for (int t = 0; t < n; t++) {
            const float o = float(t) * strip;
            const Polygon poly = across ? rect(x + o, y, x + o + strip, y + square) :
                                          rect(x, y + o, x + square, y + o + strip);
            add(offset_convex(poly, g),
                across ? float2(0.0f, 1.0f) : float2(1.0f, 0.0f),
                noise::hash(uint32_t(i) * 64u + uint32_t(t), uint32_t(j), seed),
                fill.z0,
                fill.thickness,
                0.0f,
                limit);
          }
        }
      }
      break;
    }
  }
  return border_ok;
}

void add_supports(Builder &builder,
                  const Area &area,
                  const float spacing,
                  const float width,
                  const float depth,
                  const int material,
                  const int part,
                  const uint32_t seed)
{
  if (spacing <= 0.0f || width <= 0.0f || depth <= 0.0f || area.faces.is_empty()) {
    return;
  }
  const float half = width * 0.5f;
  const int first = floor_div(area.box_min.x, spacing);
  const int last = floor_div(area.box_max.x, spacing) + 1;
  for (int k = first; k <= last && !builder.too_many; k++) {
    const float x = float(k) * spacing;
    for (const float2 &interval : band_intervals(area.loops, x - half, x + half, true)) {
      Board board;
      board.outline = rect(x - half, interval.x - width, x + half, interval.y + width);
      board.axis = float2(0.0f, 1.0f);
      board.id = noise::hash(uint32_t(k), seed, 4242u);
      board.z0 = -depth;
      board.thickness = depth;
      board.material = material;
      board.part = part;
      build_board(builder, area, board, {});
    }
  }
}

void add_prism(Builder &builder,
               const Span<float3> profile,
               const float3 &start,
               const float3 &end,
               const int material,
               const int part,
               const bool smooth)
{
  const float3 direction = end - start;
  const float length = math::length(direction);
  if (length < 1e-7f || profile.size() < 3) {
    return;
  }
  float3 normal(0.0f);
  for (const int i : profile.index_range()) {
    normal += math::cross(profile[i], profile[(i + 1) % profile.size()]);
  }
  Vector<float3, 16> points(profile);
  if (math::dot(normal, direction) < 0.0f) {
    std::reverse(points.begin(), points.end());
  }
  if (!builder.begin_board(0.0f)) {
    return;
  }
  const int n = points.size();
  const int first = builder.positions.size();
  for (const float3 &p : points) {
    builder.add_vert(start + p);
  }
  for (const float3 &p : points) {
    builder.add_vert(end + p);
  }
  float along = 0.0f;
  for (const int i : IndexRange(n)) {
    const int next = (i + 1) % n;
    const float step = math::distance(points[i], points[next]);
    const int verts[4] = {first + i, first + next, first + n + next, first + n + i};
    const float2 uvs[4] = {float2(along, 0.0f),
                           float2(along + step, 0.0f),
                           float2(along + step, length),
                           float2(along, length)};
    builder.add_face(Span<int>(verts, 4), Span<float2>(uvs, 4), material, part, smooth);
    along += step;
  }
  const float3 axis = direction / length;
  float3 side = math::cross(axis, float3(0.0f, 0.0f, 1.0f));
  if (math::length_squared(side) < 1e-8f) {
    side = math::cross(axis, float3(1.0f, 0.0f, 0.0f));
  }
  side = math::normalize(side);
  const float3 up = math::cross(side, axis);
  Vector<int, 16> cap;
  Vector<float2, 16> cap_uvs;
  for (int i = n - 1; i >= 0; i--) {
    cap.append(first + i);
    cap_uvs.append(float2(math::dot(points[i], side), math::dot(points[i], up)));
  }
  builder.add_face(cap, cap_uvs, material, part);
  cap.clear();
  cap_uvs.clear();
  for (const int i : IndexRange(n)) {
    cap.append(first + n + i);
    cap_uvs.append(float2(math::dot(points[i], side), math::dot(points[i], up)));
  }
  builder.add_face(cap, cap_uvs, material, part);
}

/** \} */

}  // namespace blender::geometry::boards
