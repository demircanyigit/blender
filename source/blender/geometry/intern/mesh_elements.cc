/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <algorithm>
#include <cfloat>
#include <numeric>

#include "BLI_array.hh"
#include "BLI_array_utils.hh"
#include "BLI_atomic_disjoint_set.hh"
#include "BLI_disjoint_set.hh"
#include "BLI_hash.h"
#include "BLI_index_mask.hh"
#include "BLI_map.hh"
#include "BLI_math_axis_angle.hh"
#include "BLI_math_color.h"
#include "BLI_math_euler.hh"
#include "BLI_math_matrix.hh"
#include "BLI_math_vector.hh"
#include "BLI_task.hh"

#include "DNA_mesh_types.h"

#include "BKE_attribute.h"
#include "BKE_attribute.hh"
#include "BKE_mesh.hh"

#include "GEO_mesh_elements.hh"
#include "GEO_mesh_split_edges.hh"
#include "GEO_reorder.hh"

namespace blender::geometry {

/** A random value from 0 to 1 for an element, a seed and a channel. */
static float element_random(const int element, const int seed, const int channel)
{
  return BLI_hash_int_3d_to_float(uint32_t(element), uint32_t(seed), uint32_t(channel));
}

/** A random value from -1 to 1. */
static float element_random_signed(const int element, const int seed, const int channel)
{
  return element_random(element, seed, channel) * 2.0f - 1.0f;
}

/* -------------------------------------------------------------------- */
/** \name Element IDs
 * \{ */

/** Number the roots of a disjoint set in the order they are met. */
template<typename RootFn>
static int compact_ids(const int faces_num, const RootFn root_of, MutableSpan<int> r_ids)
{
  Map<int, int> index_by_root;
  for (const int face : IndexRange(faces_num)) {
    r_ids[face] = index_by_root.lookup_or_add(root_of(face), index_by_root.size());
  }
  return index_by_root.size();
}

static int island_elements(const Mesh &mesh, MutableSpan<int> r_ids)
{
  const OffsetIndices faces = mesh.faces();
  const Span<int> corner_verts = mesh.corner_verts();
  AtomicDisjointSet islands(mesh.verts_num);
  for (const int face : faces.index_range()) {
    const Span<int> verts = corner_verts.slice(faces[face]);
    for (const int i : verts.index_range().drop_front(1)) {
      islands.join(verts[0], verts[i]);
    }
  }
  return compact_ids(
      faces.size(),
      [&](const int face) { return islands.find_root(corner_verts[faces[face].first()]); },
      r_ids);
}

static int uv_island_elements(const Mesh &mesh, const Span<float2> uvs, MutableSpan<int> r_ids)
{
  const OffsetIndices faces = mesh.faces();
  const Span<int> corner_verts = mesh.corner_verts();
  const Span<int> corner_edges = mesh.corner_edges();
  const Span<int2> edges = mesh.edges();

  /* The first face met on each edge, with the UVs at the edge's two points. */
  Array<int> edge_face(edges.size(), -1);
  Array<float2> edge_uv_a(edges.size());
  Array<float2> edge_uv_b(edges.size());
  DisjointSet<int> islands(faces.size());

  const float limit = 1e-5f;
  for (const int face : faces.index_range()) {
    const IndexRange range = faces[face];
    for (const int corner : range) {
      const int next = bke::mesh::face_corner_next(range, corner);
      const int edge = corner_edges[corner];
      const bool forward = corner_verts[corner] == edges[edge][0];
      const float2 uv_a = forward ? uvs[corner] : uvs[next];
      const float2 uv_b = forward ? uvs[next] : uvs[corner];
      if (edge_face[edge] == -1) {
        edge_face[edge] = face;
        edge_uv_a[edge] = uv_a;
        edge_uv_b[edge] = uv_b;
        continue;
      }
      if (math::distance_squared(uv_a, edge_uv_a[edge]) < limit * limit &&
          math::distance_squared(uv_b, edge_uv_b[edge]) < limit * limit)
      {
        islands.join(face, edge_face[edge]);
      }
    }
  }
  return compact_ids(
      faces.size(), [&](const int face) { return islands.find_root(face); }, r_ids);
}

static int value_elements(const VArraySpan<int> &values, MutableSpan<int> r_ids)
{
  return compact_ids(values.size(), [&](const int face) { return values[face]; }, r_ids);
}

int mesh_face_elements(const Mesh &mesh,
                       const MeshElement element,
                       const StringRef uv_map,
                       MutableSpan<int> r_face_elements)
{
  const bke::AttributeAccessor attributes = mesh.attributes();
  switch (element) {
    case MeshElement::Face:
      array_utils::fill_index_range(r_face_elements);
      return mesh.faces_num;
    case MeshElement::Island:
      return island_elements(mesh, r_face_elements);
    case MeshElement::UVIsland: {
      const StringRef name = uv_map.is_empty() ? StringRef(mesh.active_uv_map_name()) : uv_map;
      if (!name.is_empty()) {
        const VArraySpan uvs = *attributes.lookup<float2>(name, bke::AttrDomain::Corner);
        if (!uvs.is_empty()) {
          return uv_island_elements(mesh, uvs, r_face_elements);
        }
      }
      return island_elements(mesh, r_face_elements);
    }
    case MeshElement::Material: {
      const VArraySpan values = *attributes.lookup_or_default<int>(
          "material_index", bke::AttrDomain::Face, 0);
      return value_elements(values, r_face_elements);
    }
    case MeshElement::FaceSet: {
      if (const VArray values = *attributes.lookup<int>(".sculpt_face_set",
                                                       bke::AttrDomain::Face))
      {
        return value_elements(VArraySpan(values), r_face_elements);
      }
      return island_elements(mesh, r_face_elements);
    }
  }
  return 0;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Shared Helpers
 * \{ */

/**
 * Split the mesh so that no point is shared by two elements. Faces keep their order, so the face
 * element IDs stay valid.
 */
static void separate_elements(Mesh &mesh, const Span<int> face_elements)
{
  const OffsetIndices faces = mesh.faces();
  const Span<int> corner_edges = mesh.corner_edges();
  Array<int> edge_element(mesh.edges_num, -1);
  Array<bool> split(mesh.edges_num, false);
  Array<int> edge_users(mesh.edges_num, 0);
  for (const int face : faces.index_range()) {
    for (const int edge : corner_edges.slice(faces[face])) {
      edge_users[edge]++;
      if (edge_element[edge] == -1) {
        edge_element[edge] = face_elements[face];
      }
      else if (edge_element[edge] != face_elements[face]) {
        split[edge] = true;
      }
    }
  }
  bool any = false;
  for (const int edge : split.index_range()) {
    /* Also split open edges, so points touching other elements only at a corner are visited. */
    if (split[edge] || edge_users[edge] == 1) {
      split[edge] = true;
      any = true;
    }
  }
  if (!any) {
    return;
  }
  IndexMaskMemory memory;
  split_edges(mesh, IndexMask::from_bools(split, memory));
}

/** The element of each point, or -1 for points without faces. */
static Array<int> point_elements(const Mesh &mesh, const Span<int> face_elements)
{
  Array<int> result(mesh.verts_num, -1);
  const OffsetIndices faces = mesh.faces();
  const Span<int> corner_verts = mesh.corner_verts();
  for (const int face : faces.index_range()) {
    for (const int vert : corner_verts.slice(faces[face])) {
      if (result[vert] == -1) {
        result[vert] = face_elements[face];
      }
    }
  }
  return result;
}

struct ElementInfo {
  float3 center = float3(0.0f);
  float3 bounds_min = float3(FLT_MAX);
  float3 bounds_max = float3(-FLT_MAX);
  float3 normal = float3(0.0f);
  float weight = 0.0f;
  int count = 0;
  int weight_count = 0;
};

static Array<ElementInfo> element_infos(const Mesh &mesh,
                                        const Span<int> face_elements,
                                        const int elements_num,
                                        const Span<float> weights)
{
  Array<ElementInfo> infos(elements_num);
  const Span<float3> positions = mesh.vert_positions();
  const OffsetIndices faces = mesh.faces();
  const Span<int> corner_verts = mesh.corner_verts();
  const Span<float3> face_normals = mesh.face_normals();

  Array<bool> counted(mesh.verts_num, false);
  for (const int face : faces.index_range()) {
    ElementInfo &info = infos[face_elements[face]];
    const Span<int> verts = corner_verts.slice(faces[face]);
    const float area = bke::mesh::face_area_calc(positions, verts);
    info.normal += face_normals[face] * std::max(area, 1e-12f);
    for (const int vert : verts) {
      if (!weights.is_empty()) {
        info.weight += weights[vert];
        info.weight_count++;
      }
      if (counted[vert]) {
        continue;
      }
      counted[vert] = true;
      const float3 &p = positions[vert];
      info.center += p;
      info.bounds_min = math::min(info.bounds_min, p);
      info.bounds_max = math::max(info.bounds_max, p);
      info.count++;
    }
  }
  for (ElementInfo &info : infos) {
    if (info.count) {
      info.center /= float(info.count);
    }
    info.normal = math::normalize(info.normal);
    if (math::is_zero(info.normal)) {
      info.normal = float3(0.0f, 0.0f, 1.0f);
    }
    info.weight = info.weight_count ? info.weight / float(info.weight_count) : 1.0f;
  }
  return infos;
}

static float3 pivot_of(const ElementInfo &info, const ElementPivot pivot)
{
  switch (pivot) {
    case ElementPivot::Center:
      return info.center;
    case ElementPivot::BoundsCenter:
      return info.count ? math::midpoint(info.bounds_min, info.bounds_max) : float3(0.0f);
    case ElementPivot::Origin:
      return float3(0.0f);
  }
  return info.center;
}

static float3x3 rotation_around(const float3 &axis, const float angle)
{
  if (angle == 0.0f || math::is_zero(axis)) {
    return float3x3::identity();
  }
  return math::from_rotation<float3x3>(math::AxisAngle(math::normalize(axis), angle));
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Randomize Elements
 * \{ */

static void randomize_geometry(Mesh &mesh,
                               const Span<int> face_elements,
                               const Span<ElementInfo> infos,
                               const RandomizeElementsParams &params)
{
  const int seed = params.seed;
  const int elements_num = infos.size();
  Array<float3x3> transforms(elements_num);
  Array<float3> pivots(elements_num);
  Array<float3> offsets(elements_num);
  threading::parallel_for(IndexRange(elements_num), 512, [&](const IndexRange range) {
    for (const int e : range) {
      const ElementInfo &info = infos[e];
      const float w = info.weight;
      const math::EulerXYZ euler(params.rotation.x * element_random_signed(e, seed, 0) * w,
                                 params.rotation.y * element_random_signed(e, seed, 1) * w,
                                 params.rotation.z * element_random_signed(e, seed, 2) * w);
      const float3x3 rotation =
          math::from_rotation<float3x3>(euler) *
          rotation_around(info.normal,
                          params.normal_rotation * element_random_signed(e, seed, 3) * w);

      float3 scale;
      const auto random_scale = [&](const int channel) {
        const float s = math::interpolate(
            params.scale_min, params.scale_max, element_random(e, seed, channel));
        return 1.0f + (s - 1.0f) * w;
      };
      if (params.uniform_scale) {
        scale = float3(random_scale(4));
      }
      else {
        scale = float3(random_scale(4), random_scale(5), random_scale(6));
      }
      transforms[e] = rotation * math::from_scale<float3x3>(scale);
      pivots[e] = pivot_of(info, params.pivot);
      offsets[e] = (params.offset * float3(element_random_signed(e, seed, 7),
                                           element_random_signed(e, seed, 8),
                                           element_random_signed(e, seed, 9)) +
                    info.normal * params.normal_offset * element_random_signed(e, seed, 10)) *
                   w;
    }
  });

  const Array<int> vert_elements = point_elements(mesh, face_elements);
  MutableSpan<float3> positions = mesh.vert_positions_for_write();
  threading::parallel_for(positions.index_range(), 4096, [&](const IndexRange range) {
    for (const int vert : range) {
      const int e = vert_elements[vert];
      if (e == -1) {
        continue;
      }
      positions[vert] = pivots[e] + transforms[e] * (positions[vert] - pivots[e]) + offsets[e];
    }
  });
  mesh.tag_positions_changed();
}

static void randomize_uvs(Mesh &mesh,
                          const Span<int> face_elements,
                          const Span<ElementInfo> infos,
                          const RandomizeElementsParams &params)
{
  bke::MutableAttributeAccessor attributes = mesh.attributes_for_write();
  const std::string name = params.uv_map.empty() ? std::string(mesh.active_uv_map_name()) :
                                                   params.uv_map;
  if (name.empty()) {
    return;
  }
  bke::SpanAttributeWriter uvs = attributes.lookup_for_write_span<float2>(name);
  if (!uvs || uvs.domain != bke::AttrDomain::Corner) {
    return;
  }
  const int elements_num = infos.size();
  const OffsetIndices faces = mesh.faces();
  Array<float2> centers(elements_num, float2(0.0f));
  Array<int> counts(elements_num, 0);
  for (const int face : faces.index_range()) {
    for (const int corner : faces[face]) {
      centers[face_elements[face]] += uvs.span[corner];
      counts[face_elements[face]]++;
    }
  }
  const int seed = params.seed;
  Array<float2x2> transforms(elements_num);
  Array<float2> offsets(elements_num);
  for (const int e : IndexRange(elements_num)) {
    if (counts[e]) {
      centers[e] /= float(counts[e]);
    }
    const float w = infos[e].weight;
    const float angle = params.uv_rotation * element_random_signed(e, seed, 20) * w;
    const float s = 1.0f + (math::interpolate(params.uv_scale_min,
                                              params.uv_scale_max,
                                              element_random(e, seed, 21)) -
                            1.0f) *
                               w;
    const float c = std::cos(angle) * s;
    const float sn = std::sin(angle) * s;
    transforms[e] = float2x2(float2(c, sn), float2(-sn, c));
    offsets[e] = params.uv_offset *
                 float2(element_random_signed(e, seed, 22), element_random_signed(e, seed, 23)) *
                 w;
  }
  threading::parallel_for(faces.index_range(), 1024, [&](const IndexRange range) {
    for (const int face : range) {
      const int e = face_elements[face];
      for (const int corner : faces[face]) {
        uvs.span[corner] = centers[e] + transforms[e] * (uvs.span[corner] - centers[e]) +
                           offsets[e];
      }
    }
  });
  uvs.finish();
}

static void randomize_colors(Mesh &mesh,
                             const Span<int> face_elements,
                             const Span<ElementInfo> infos,
                             const RandomizeElementsParams &params)
{
  if (params.color_attribute.empty()) {
    return;
  }
  bke::MutableAttributeAccessor attributes = mesh.attributes_for_write();
  const bool existed = attributes.contains(params.color_attribute);
  bke::AttributeWriter<ColorGeometry4f> colors;
  if (existed) {
    colors = attributes.lookup_for_write<ColorGeometry4f>(params.color_attribute);
  }
  else {
    const ColorGeometry4f white(1.0f, 1.0f, 1.0f, 1.0f);
    colors = attributes.lookup_or_add_for_write<ColorGeometry4f>(
        params.color_attribute, bke::AttrDomain::Corner, bke::AttributeInitValue(white));
  }
  if (!colors) {
    return;
  }

  const int seed = params.seed;
  const int elements_num = infos.size();
  Array<ColorGeometry4f> element_colors(elements_num);
  for (const int e : IndexRange(elements_num)) {
    float3 rgb;
    switch (params.color_mode) {
      case ElementColorMode::RandomHue: {
        const float value = math::interpolate(
            params.value_min, params.value_max, element_random(e, seed, 31));
        hsv_to_rgb(element_random(e, seed, 30), params.saturation, value, &rgb.x, &rgb.y, &rgb.z);
        break;
      }
      case ElementColorMode::Grayscale:
        rgb = float3(
            math::interpolate(params.value_min, params.value_max, element_random(e, seed, 31)));
        break;
      case ElementColorMode::Palette: {
        if (params.palette.is_empty()) {
          rgb = float3(1.0f);
          break;
        }
        const int count = params.palette.size();
        const int i = std::min(int(element_random(e, seed, 32) * count), count - 1);
        rgb = params.palette[i].xyz();
        break;
      }
    }
    element_colors[e] = ColorGeometry4f(rgb.x, rgb.y, rgb.z, 1.0f);
  }

  const float blend_base = existed ? params.color_blend : 1.0f;
  const auto blend = [&](const ColorGeometry4f &old_color, const int e) {
    const float t = blend_base * infos[e].weight;
    ColorGeometry4f result;
    for (int i = 0; i < 4; i++) {
      result[i] = math::interpolate(old_color[i], element_colors[e][i], t);
    }
    return result;
  };

  const OffsetIndices faces = mesh.faces();
  switch (colors.domain) {
    case bke::AttrDomain::Corner:
      for (const int face : faces.index_range()) {
        for (const int corner : faces[face]) {
          colors.varray.set(corner, blend(colors.varray[corner], face_elements[face]));
        }
      }
      break;
    case bke::AttrDomain::Face:
      for (const int face : faces.index_range()) {
        colors.varray.set(face, blend(colors.varray[face], face_elements[face]));
      }
      break;
    case bke::AttrDomain::Point: {
      const Array<int> vert_elements = point_elements(mesh, face_elements);
      for (const int vert : vert_elements.index_range()) {
        if (vert_elements[vert] != -1) {
          colors.varray.set(vert, blend(colors.varray[vert], vert_elements[vert]));
        }
      }
      break;
    }
    default:
      break;
  }
  colors.finish();

  if (!existed) {
    BKE_id_attributes_active_color_set(&mesh.id, params.color_attribute);
    if (!mesh.default_color_attribute) {
      BKE_id_attributes_default_color_set(&mesh.id, params.color_attribute);
    }
  }
}

Mesh *randomize_elements(const Mesh &mesh, const RandomizeElementsParams &params)
{
  const bool use_id = params.random_id.has_value() && !params.random_id->empty();
  if (mesh.faces_num == 0 ||
      !(params.use_geometry || params.use_uvs || params.use_color || use_id))
  {
    return nullptr;
  }
  Array<int> face_elements(mesh.faces_num);
  const int elements_num = mesh_face_elements(
      mesh, params.element, params.uv_map, face_elements);
  const Array<ElementInfo> infos_orig = element_infos(
      mesh, face_elements, elements_num, params.weights);

  Mesh *result = BKE_mesh_copy_for_eval(mesh);
  if (params.use_geometry) {
    if (params.element != MeshElement::Island) {
      separate_elements(*result, face_elements);
    }
    randomize_geometry(*result, face_elements, infos_orig, params);
  }
  if (params.use_uvs) {
    randomize_uvs(*result, face_elements, infos_orig, params);
  }
  if (params.use_color) {
    randomize_colors(*result, face_elements, infos_orig, params);
  }
  if (use_id) {
    bke::MutableAttributeAccessor attributes = result->attributes_for_write();
    bke::SpanAttributeWriter ids = attributes.lookup_or_add_for_write_only_span<float>(
        *params.random_id, bke::AttrDomain::Face);
    if (ids) {
      for (const int face : face_elements.index_range()) {
        ids.span[face] = element_random(face_elements[face], params.seed, 40);
      }
      ids.finish();
    }
  }
  return result;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Exploded View
 * \{ */

Mesh *exploded_view(const Mesh &mesh, const ExplodedViewParams &params)
{
  if (mesh.faces_num == 0 || params.factor == 0.0f) {
    return nullptr;
  }
  Array<int> face_elements(mesh.faces_num);
  const int elements_num = mesh_face_elements(
      mesh, params.element, params.uv_map, face_elements);
  const Array<ElementInfo> infos = element_infos(
      mesh, face_elements, elements_num, params.weights);

  Mesh *result = BKE_mesh_copy_for_eval(mesh);
  if (params.element != MeshElement::Island) {
    separate_elements(*result, face_elements);
  }

  const int seed = params.seed;
  Array<float3> moves(elements_num);
  Array<float3x3> tumbles(elements_num);
  for (const int e : IndexRange(elements_num)) {
    const ElementInfo &info = infos[e];
    float3 direction;
    if (params.direction == ExplodeDirection::Normal) {
      direction = info.normal;
    }
    else {
      direction = math::normalize(info.center - params.center);
      if (math::is_zero(direction)) {
        direction = info.normal;
      }
    }
    const float amount = params.distance * params.factor * info.weight *
                         (1.0f - params.randomness * element_random(e, seed, 0));
    moves[e] = direction * amount;
    const float3 axis(element_random_signed(e, seed, 1),
                      element_random_signed(e, seed, 2),
                      element_random_signed(e, seed, 3));
    tumbles[e] = rotation_around(
        axis, params.tumble * params.factor * info.weight * element_random_signed(e, seed, 4));
  }

  const Array<int> vert_elements = point_elements(*result, face_elements);
  MutableSpan<float3> positions = result->vert_positions_for_write();
  threading::parallel_for(positions.index_range(), 4096, [&](const IndexRange range) {
    for (const int vert : range) {
      const int e = vert_elements[vert];
      if (e == -1) {
        continue;
      }
      const float3 &c = infos[e].center;
      positions[vert] = c + tumbles[e] * (positions[vert] - c) + moves[e];
    }
  });
  result->tag_positions_changed();
  return result;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Element Order
 * \{ */

Mesh *element_order(const Mesh &mesh, const ElementOrderParams &params)
{
  if (mesh.faces_num < 2) {
    return nullptr;
  }
  Array<int> face_elements(mesh.faces_num);
  const int elements_num = mesh_face_elements(
      mesh, params.element, params.uv_map, face_elements);
  const Array<ElementInfo> infos = element_infos(mesh, face_elements, elements_num, {});

  Array<float> keys(elements_num);
  const int axis = std::clamp(params.axis, 0, 2);
  for (const int e : IndexRange(elements_num)) {
    switch (params.mode) {
      case ElementOrderMode::Random:
        keys[e] = element_random(e, params.seed, 0);
        break;
      case ElementOrderMode::Axis:
        keys[e] = infos[e].center[axis];
        break;
      case ElementOrderMode::Distance:
        keys[e] = math::distance(infos[e].center, params.point);
        break;
    }
  }
  Array<int> element_order(elements_num);
  std::iota(element_order.begin(), element_order.end(), 0);
  std::stable_sort(element_order.begin(), element_order.end(), [&](const int a, const int b) {
    return params.reverse ? keys[a] > keys[b] : keys[a] < keys[b];
  });
  Array<int> rank(elements_num);
  for (const int i : element_order.index_range()) {
    rank[element_order[i]] = i;
  }

  Array<int> old_by_new(mesh.faces_num);
  std::iota(old_by_new.begin(), old_by_new.end(), 0);
  std::stable_sort(old_by_new.begin(), old_by_new.end(), [&](const int a, const int b) {
    return rank[face_elements[a]] < rank[face_elements[b]];
  });
  return reorder_mesh(mesh, old_by_new, bke::AttrDomain::Face, {});
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Store Element Pivot
 * \{ */

void store_element_pivot(Mesh &mesh, const ElementPivotParams &params)
{
  if (mesh.faces_num == 0) {
    return;
  }
  Array<int> face_elements(mesh.faces_num);
  const int elements_num = mesh_face_elements(
      mesh, params.element, params.uv_map, face_elements);
  const Array<ElementInfo> infos = element_infos(mesh, face_elements, elements_num, {});

  /* The X axis follows the longest edge of each element, made perpendicular to its normal. */
  const Span<float3> positions = mesh.vert_positions();
  const OffsetIndices faces = mesh.faces();
  const Span<int> corner_verts = mesh.corner_verts();
  Array<float3> x_axes(elements_num, float3(0.0f));
  Array<float> longest(elements_num, -1.0f);
  for (const int face : faces.index_range()) {
    const int e = face_elements[face];
    const IndexRange range = faces[face];
    for (const int corner : range) {
      const float3 edge = positions[corner_verts[bke::mesh::face_corner_next(range, corner)]] -
                          positions[corner_verts[corner]];
      const float length = math::length_squared(edge);
      if (length > longest[e]) {
        longest[e] = length;
        x_axes[e] = edge;
      }
    }
  }
  for (const int e : IndexRange(elements_num)) {
    const float3 &n = infos[e].normal;
    float3 x = x_axes[e] - n * math::dot(x_axes[e], n);
    x = math::normalize(x);
    if (math::is_zero(x)) {
      x = math::normalize(math::cross(n, float3(0.0f, 0.0f, 1.0f)));
      if (math::is_zero(x)) {
        x = float3(1.0f, 0.0f, 0.0f);
      }
    }
    x_axes[e] = x;
  }

  const Array<int> vert_elements = point_elements(mesh, face_elements);
  bke::MutableAttributeAccessor attributes = mesh.attributes_for_write();
  const auto write_float3 = [&](const std::string &name, const auto value_of) {
    if (name.empty()) {
      return;
    }
    attributes.remove(name);
    bke::SpanAttributeWriter values = attributes.lookup_or_add_for_write_only_span<float3>(
        name, bke::AttrDomain::Point);
    if (!values) {
      return;
    }
    for (const int vert : vert_elements.index_range()) {
      const int e = vert_elements[vert];
      values.span[vert] = e == -1 ? float3(0.0f) : value_of(e);
    }
    values.finish();
  };
  write_float3(params.center_name, [&](const int e) { return pivot_of(infos[e], params.pivot); });
  write_float3(params.x_axis_name, [&](const int e) { return x_axes[e]; });
  write_float3(params.z_axis_name, [&](const int e) { return infos[e].normal; });

  if (!params.index_name.empty()) {
    attributes.remove(params.index_name);
    bke::SpanAttributeWriter values = attributes.lookup_or_add_for_write_only_span<int>(
        params.index_name, bke::AttrDomain::Point);
    if (values) {
      values.span.copy_from(vert_elements);
      values.finish();
    }
  }
}

/** \} */

}  // namespace blender::geometry
