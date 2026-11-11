/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string>

#include "BLI_array.hh"
#include "BLI_math_matrix.hh"
#include "BLI_math_vector.hh"

#include "DNA_mesh_types.h"

#include "BKE_attribute.hh"
#include "BKE_mesh.hh"

#include "GEO_mesh_elements.hh"
#include "GEO_uv_scale.hh"

namespace blender::geometry {

const char *uv_scale(Mesh &mesh, const StringRef uv_map, const UVScaleParams &params)
{
  if (mesh.faces_num == 0) {
    return nullptr;
  }
  const std::string name = uv_map.is_empty() ? std::string(mesh.active_uv_map_name()) :
                                               std::string(uv_map);
  if (name.empty()) {
    return "The mesh needs an existing UV map";
  }
  const bke::AttributeAccessor attributes = mesh.attributes();
  const std::optional<bke::AttributeMetaData> meta = attributes.lookup_meta_data(name);
  if (!meta || meta->domain != bke::AttrDomain::Corner || meta->data_type != bke::AttrType::Float2)
  {
    return "UV map not found; choose an existing corner UV map";
  }
  if (!params.texel_density && params.scale.x == 1.0f && params.scale.y == 1.0f) {
    return nullptr;
  }

  /* Group before obtaining a writable span, since grouping reads the original UV attribute. */
  Array<int> face_groups(mesh.faces_num, 0);
  const int groups_num = params.individual_islands ?
                             mesh_face_elements(mesh, MeshElement::UVIsland, name, face_groups) :
                             1;
  bke::SpanAttributeWriter<float2> uvs = mesh.attributes_for_write().lookup_for_write_span<float2>(
      name);
  if (!uvs) {
    return "UV map cannot be written";
  }
  const OffsetIndices faces = mesh.faces();
  Array<float2> minimum(groups_num, float2(FLT_MAX));
  Array<float2> maximum(groups_num, float2(-FLT_MAX));
  for (const int face : faces.index_range()) {
    const int group = face_groups[face];
    for (const int corner : faces[face]) {
      minimum[group] = math::min(minimum[group], uvs.span[corner]);
      maximum[group] = math::max(maximum[group], uvs.span[corner]);
    }
  }

  Array<float2> scales(groups_num, params.scale);
  bool skipped = false;
  if (params.texel_density) {
    Array<double> surface_area(groups_num, 0.0);
    Array<double> uv_area(groups_num, 0.0);
    const Span<float3> positions = mesh.vert_positions();
    const Span<int> corner_verts = mesh.corner_verts();
    const Span<int3> triangles = mesh.corner_tris();
    const Span<int> triangle_faces = mesh.corner_tri_faces();
    for (const int tri_index : triangles.index_range()) {
      const int3 tri = triangles[tri_index];
      const int group = face_groups[triangle_faces[tri_index]];
      const float3 a = positions[corner_verts[tri[0]]];
      const float3 b = positions[corner_verts[tri[1]]];
      const float3 c = positions[corner_verts[tri[2]]];
      const double3 ab = double3(math::transform_direction(params.object_to_world, b - a));
      const double3 ac = double3(math::transform_direction(params.object_to_world, c - a));
      surface_area[group] += 0.5 * math::length(math::cross(ab, ac));
      const double2 uv_ab = double2(uvs.span[tri[1]]) - double2(uvs.span[tri[0]]);
      const double2 uv_ac = double2(uvs.span[tri[2]]) - double2(uvs.span[tri[0]]);
      uv_area[group] += 0.5 * std::abs(uv_ab.x * uv_ac.y - uv_ab.y * uv_ac.x);
    }
    const double pixels = double(std::max(params.resolution.x, 1)) *
                          double(std::max(params.resolution.y, 1));
    const double meters_squared = double(params.meters_per_unit) * params.meters_per_unit;
    for (const int group : IndexRange(groups_num)) {
      scales[group] = float2(1.0f);
      if (surface_area[group] <= 0.0 || uv_area[group] <= 0.0) {
        skipped = true;
        continue;
      }
      const double factor = double(params.density) *
                            std::sqrt(surface_area[group] * meters_squared /
                                      (uv_area[group] * pixels));
      if (!std::isfinite(factor) || factor <= 0.0 || factor > FLT_MAX) {
        skipped = true;
        continue;
      }
      scales[group] = float2(float(factor));
    }
  }
  for (const int face : faces.index_range()) {
    const int group = face_groups[face];
    const float2 pivot = params.pivot == 0 ? (minimum[group] + maximum[group]) * 0.5f :
                         params.pivot == 1 ? float2(0.0f) :
                                             params.center;
    for (const int corner : faces[face]) {
      uvs.span[corner] = pivot + (uvs.span[corner] - pivot) * scales[group];
    }
  }
  uvs.finish();
  return skipped ? "Zero-area UV islands or surfaces were left unchanged" : nullptr;
}

}  // namespace blender::geometry
