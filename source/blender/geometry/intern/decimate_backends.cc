/* SPDX-FileCopyrightText: 2026 Blender Authors
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <numeric>
#include <unordered_map>
#include <unordered_set>

#include "BKE_attribute.hh"
#include "BKE_lib_id.hh"
#include "BKE_mesh.hh"
#include "BLI_array.hh"
#include "BLI_math_base.h"
#include "BLI_math_vector.hh"
#include "DNA_mesh_types.h"

#include "GEO_decimate_backends.hh"
#include "GEO_mesh_decimate.hh"
#include "decimate_backend_types.hh"
#include "meshoptimizer.h"
#include "pmp_mesh_conversion.hh"

namespace blender::geometry {
namespace {
using decimation::TriangleInput;
using decimation::TriangleResult;

uint64_t edge_key(int a, int b)
{
  if (a > b) {
    std::swap(a, b);
  }
  return (uint64_t(uint32_t(a)) << 32) | uint32_t(b);
}

struct TriangleHash {
  size_t operator()(const std::array<int, 3> &a) const
  {
    return size_t(uint64_t(a[0]) * 73856093u ^ uint64_t(a[1]) * 19349663u ^
                  uint64_t(a[2]) * 83492791u);
  }
};

struct EdgeUse {
  int count = 0;
  int material = 0;
  float3 normal;
  float2 uv_a, uv_b;
  bool protected_edge = false;
};

TriangleInput prepare_input(const Mesh &mesh, const DecimateBackendParams &params)
{
  const Span<float3> positions = mesh.vert_positions();
  const Span<int> corner_verts = mesh.corner_verts();
  const Span<int3> corner_tris = mesh.corner_tris();
  const Span<int> tri_faces = mesh.corner_tri_faces();
  std::vector<unsigned int> welded(mesh.verts_num);
  if (params.merge_coincident) {
    meshopt_generatePositionRemap(
        welded.data(), &positions[0].x, positions.size(), sizeof(float3));
  }
  else {
    std::iota(welded.begin(), welded.end(), 0u);
  }
  TriangleInput input;
  std::vector<int> compact(mesh.verts_num, -1), old_to_new(mesh.verts_num, -1);
  std::vector<int> source_triangles;
  std::unordered_set<std::array<int, 3>, TriangleHash> unique_faces;
  for (const int i : corner_tris.index_range()) {
    int3 tri;
    for (int k = 0; k < 3; k++) {
      const int old = corner_verts[corner_tris[i][k]];
      const int canonical = welded[old];
      if (compact[canonical] == -1) {
        compact[canonical] = int(input.positions.size());
        input.positions.push_back(positions[canonical]);
      }
      old_to_new[old] = compact[canonical];
      tri[k] = compact[canonical];
    }
    if (tri.x == tri.y || tri.y == tri.z || tri.z == tri.x) {
      continue;
    }
    const double3 a = double3(input.positions[tri.y]) - double3(input.positions[tri.x]);
    const double3 b = double3(input.positions[tri.z]) - double3(input.positions[tri.x]);
    if (math::length_squared(math::cross(a, b)) == 0) {
      continue;
    }
    std::array<int, 3> key = {tri.x, tri.y, tri.z};
    std::sort(key.begin(), key.end());
    if (!unique_faces.insert(key).second) {
      continue;
    }
    input.triangles.push_back(tri);
    source_triangles.push_back(i);
  }
  input.locked.resize(input.positions.size(), 0);
  for (const int old : positions.index_range()) {
    const int vertex = old_to_new[old];
    if (vertex >= 0 && params.selection && !params.selection[old]) {
      input.locked[vertex] = 1;
    }
  }
  const bke::AttributeAccessor attrs = mesh.attributes();
  const std::string uv_name = params.uv_map.is_empty() ? std::string(mesh.active_uv_map_name()) :
                                                         std::string(params.uv_map);
  const VArraySpan<float2> uv = *attrs.lookup_or_default<float2>(
      uv_name, bke::AttrDomain::Corner, float2(0));
  const bool has_uv = !uv_name.empty() && attrs.contains(uv_name);
  const VArraySpan<int> materials = *attrs.lookup_or_default<int>(
      "material_index", bke::AttrDomain::Face, 0);
  const VArraySpan<bool> sharp = *attrs.lookup_or_default<bool>(
      "sharp_edge", bke::AttrDomain::Edge, false);
  std::unordered_set<uint64_t> sharp_edges;
  if (params.preserve_sharp) {
    for (const int e : mesh.edges().index_range()) {
      const int2 edge = mesh.edges()[e];
      const int a = old_to_new[edge.x], b = old_to_new[edge.y];
      if (a >= 0 && b >= 0 && sharp[e]) {
        sharp_edges.insert(edge_key(a, b));
      }
    }
  }
  input.attributes.resize(input.positions.size() * 5, 0);
  std::vector<int> counts(input.positions.size(), 0);
  const Span<float3> normals = mesh.corner_normals();
  std::unordered_map<uint64_t, EdgeUse> edges;
  const float cos_angle = std::cos(params.feature_angle);
  for (int i = 0; i < int(input.triangles.size()); i++) {
    const int3 tri = input.triangles[i];
    const int original = source_triangles[i];
    const int3 corners = corner_tris[original];
    const float3 normal = float3(math::normalize(
        math::cross(double3(input.positions[tri.y]) - double3(input.positions[tri.x]),
                    double3(input.positions[tri.z]) - double3(input.positions[tri.x]))));
    for (int k = 0; k < 3; k++) {
      const int a = tri[k], b = tri[(k + 1) % 3];
      const int corner = corners[k], next = corners[(k + 1) % 3];
      const float2 uv_a = a < b ? uv[corner] : uv[next];
      const float2 uv_b = a < b ? uv[next] : uv[corner];
      EdgeUse &edge = edges[edge_key(a, b)];
      if (edge.count == 0) {
        edge.normal = normal;
        edge.material = materials[tri_faces[original]];
        edge.uv_a = uv_a;
        edge.uv_b = uv_b;
      }
      else {
        edge.protected_edge |= params.preserve_materials &&
                               edge.material != materials[tri_faces[original]];
        edge.protected_edge |= params.preserve_sharp && math::dot(normal, edge.normal) < cos_angle;
        edge.protected_edge |= params.preserve_uv && has_uv &&
                               (math::distance_squared(uv_a, edge.uv_a) > 1e-10f ||
                                math::distance_squared(uv_b, edge.uv_b) > 1e-10f);
      }
      edge.count++;
      const size_t offset = size_t(a) * 5;
      input.attributes[offset] += uv[corner].x;
      input.attributes[offset + 1] += uv[corner].y;
      for (int axis = 0; axis < 3; axis++) {
        input.attributes[offset + 2 + axis] += normals[corner][axis];
      }
      counts[a]++;
    }
  }
  for (const auto &[key, edge] : edges) {
    if (edge.protected_edge || (params.preserve_boundary && edge.count == 1) ||
        sharp_edges.contains(key))
    {
      input.locked[uint32_t(key >> 32)] = input.locked[uint32_t(key)] = 1;
    }
  }
  for (size_t i = 0; i < counts.size(); i++) {
    for (int k = 0; k < 5; k++) {
      input.attributes[i * 5 + k] /= float(std::max(counts[i], 1));
    }
  }
  return input;
}

Mesh *make_mesh(const std::vector<float3> &positions,
                const std::vector<int3> &triangles,
                const Mesh &source)
{
  /* Compact only used vertices, keeping output ordering deterministic. */
  std::vector<int> remap(positions.size(), -1);
  int count = 0;
  for (const int3 &tri : triangles) {
    for (const int vertex : {tri.x, tri.y, tri.z}) {
      if (remap[vertex] == -1) {
        remap[vertex] = count++;
      }
    }
  }
  Mesh *mesh = BKE_mesh_new_nomain(count, 0, triangles.size(), triangles.size() * 3);
  BKE_mesh_copy_parameters_for_eval(mesh, &source);
  MutableSpan<float3> output_positions = mesh->vert_positions_for_write();
  for (size_t i = 0; i < positions.size(); i++) {
    if (remap[i] >= 0) {
      output_positions[remap[i]] = positions[i];
    }
  }
  MutableSpan<int> offsets = mesh->face_offsets_for_write();
  for (const int i : offsets.index_range()) {
    offsets[i] = i * 3;
  }
  MutableSpan<int> corners = mesh->corner_verts_for_write();
  for (size_t i = 0; i < triangles.size(); i++) {
    for (int k = 0; k < 3; k++) {
      corners[i * 3 + k] = remap[triangles[i][k]];
    }
  }
  bke::mesh_calc_edges(*mesh, false, false);
  return mesh;
}

TriangleResult simplify_meshopt(const TriangleInput &input, const DecimateBackendParams &params)
{
  TriangleResult result;
  std::vector<unsigned int> indices;
  indices.reserve(input.triangles.size() * 3);
  for (const int3 &tri : input.triangles) {
    for (const int i : {tri.x, tri.y, tri.z}) {
      indices.push_back(i);
    }
  }
  std::vector<unsigned int> output(indices.size());
  const float *positions = &input.positions[0].x;
  const size_t target = std::min(size_t(params.target_faces) * 3, indices.size());
  size_t count;
  if (params.backend == DecimateBackend::Aggressive) {
    const float scale = meshopt_simplifyScale(positions, input.positions.size(), sizeof(float3));
    const float error = params.error_limit > 0 ?
                            std::clamp(params.error_limit / std::max(scale, 1e-20f), 0.0f, 1.0f) :
                            1;
    count = meshopt_simplifySloppy(output.data(),
                                   indices.data(),
                                   indices.size(),
                                   positions,
                                   input.positions.size(),
                                   sizeof(float3),
                                   input.locked.data(),
                                   target,
                                   error,
                                   nullptr);
  }
  else {
    const float weights[5] = {params.uv_weight,
                              params.uv_weight,
                              params.normal_weight,
                              params.normal_weight,
                              params.normal_weight};
    count = meshopt_simplifyWithAttributes(output.data(),
                                           indices.data(),
                                           indices.size(),
                                           positions,
                                           input.positions.size(),
                                           sizeof(float3),
                                           input.attributes.data(),
                                           5 * sizeof(float),
                                           weights,
                                           5,
                                           input.locked.data(),
                                           target,
                                           params.error_limit > 0 ? params.error_limit : FLT_MAX,
                                           meshopt_SimplifyErrorAbsolute,
                                           nullptr);
  }
  result.positions = input.positions;
  for (size_t i = 0; i < count; i += 3) {
    result.triangles.emplace_back(output[i], output[i + 1], output[i + 2]);
  }
  result.success = true;
  return result;
}
}  // namespace

Mesh *decimate_with_backend(const Mesh &mesh,
                            const DecimateBackendParams &params,
                            std::string &message)
{
  if (mesh.faces_num == 0 || mesh.verts_num == 0) {
    return nullptr;
  }
  for (const float3 &position : mesh.vert_positions()) {
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) {
      message = "Input contains non-finite vertex coordinates";
      return nullptr;
    }
  }
  if (params.target_faces <= 0) {
    return make_mesh({}, {}, mesh);
  }
  TriangleInput input = prepare_input(mesh, params);
  if (input.triangles.empty()) {
    message = "No non-degenerate triangles to simplify";
    return nullptr;
  }
  Mesh *output = nullptr;
  if (params.backend == DecimateBackend::PMP) {
    Mesh *temporary = make_mesh(input.positions, input.triangles, mesh);
    MeshDecimateParams pmp;
    pmp.mode = MeshDecimateMode::FaceCount;
    pmp.face_count = params.target_faces;
    pmp.preserve_boundary = params.preserve_boundary;
    pmp.max_aspect_ratio = params.max_aspect_ratio;
    pmp.max_normal_change = params.max_normal_change;
    pmp.max_error = params.error_limit;
    /* make_mesh compacts in first-use order, as did prepare_input. */
    Array<bool> selected(temporary->verts_num, true);
    std::vector<int> compact(input.positions.size(), -1);
    int next = 0;
    for (const int3 &tri : input.triangles) {
      for (const int vertex : {tri.x, tri.y, tri.z}) {
        if (compact[vertex] < 0) {
          compact[vertex] = next++;
          selected[compact[vertex]] = !input.locked[vertex];
        }
      }
    }
    pmp.selection = VArray<bool>::from_span(selected);
    int detached = 0;
    output = decimate_mesh(*temporary, pmp, {}, &detached);
    BKE_id_free(nullptr, temporary);
    if (detached) {
      message = "PMP detached non-manifold triangles to keep its half-edge mesh valid";
    }
  }
  else {
    TriangleResult result;
    if (params.backend == DecimateBackend::Meshoptimizer ||
        params.backend == DecimateBackend::Aggressive)
    {
      result = simplify_meshopt(input, params);
    }
    else if (params.backend == DecimateBackend::CGAL) {
      result = decimation::simplify_cgal(input, params);
    }
    else if (params.backend == DecimateBackend::Wild) {
      result = decimation::simplify_wild(input, params);
    }
    if (!result.success) {
      message = result.message.empty() ? "Decimation backend failed; input was left unchanged" :
                                         result.message;
      return nullptr;
    }
    for (const float3 &p : result.positions) {
      if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) {
        message = "Backend produced invalid coordinates; input was left unchanged";
        return nullptr;
      }
    }
    for (const int3 &tri : result.triangles) {
      for (const int vertex : {tri.x, tri.y, tri.z}) {
        if (vertex < 0 || size_t(vertex) >= result.positions.size()) {
          message = "Backend produced an invalid triangle; input was left unchanged";
          return nullptr;
        }
      }
    }
    output = make_mesh(result.positions, result.triangles, mesh);
    message = std::move(result.message);
  }
  if (output) {
    transfer_attributes_from_surface(mesh, *output, {}, true);
    if (message.empty() && output->faces_num > params.target_faces + 2) {
      message = "Target limited by protected vertices, topology, or error constraints";
    }
  }
  else if (message.empty()) {
    message = "Decimation could not produce a valid mesh; input was left unchanged";
  }
  return output;
}
}  // namespace blender::geometry
