/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup geo
 */

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>

#include "DNA_mesh_types.h"

#include "BLI_array.hh"
#include "BLI_color.hh"
#include "BLI_disjoint_set.hh"
#include "BLI_index_mask.hh"
#include "BLI_math_vector.hh"
#include "BLI_memory_counter.hh"
#include "BLI_noise.hh"
#include "BLI_vector.hh"

#include "BKE_attribute.h"
#include "BKE_attribute.hh"
#include "BKE_geometry_set.hh"
#include "BKE_lib_id.hh"
#include "BKE_mesh.h"
#include "BKE_mesh.hh"
#include "BKE_mesh_remesh_voxel.hh"

#include "GEO_edge_damage.hh"
#include "GEO_isotropic_remesh.hh"
#include "GEO_join_geometries.hh"
#include "GEO_mesh_boolean.hh"
#include "GEO_mesh_copy_selection.hh"
#include "GEO_mesh_divide.hh"
#include "GEO_mesh_sim_cache.hh"
#include "GEO_mesh_smooth.hh"

namespace blender::geometry {

namespace {

struct MeshFree {
  void operator()(Mesh *mesh) const
  {
    if (mesh != nullptr) {
      BKE_id_free(nullptr, mesh);
    }
  }
};
using OwnedMesh = std::unique_ptr<Mesh, MeshFree>;

/** Add a message once, however many pieces it happens on. */
void add_message(std::string &r_message, const std::string &message)
{
  if (r_message.find(message) == std::string::npos) {
    r_message += message;
  }
}

/** Internal attributes. */
constexpr const char *KEPT_ATTRIBUTE = ".edge_damage_kept";
constexpr const char *WEIGHT_ATTRIBUTE = ".edge_damage_weight";
constexpr const char *PREVIEW_ATTRIBUTE = "EdgeDamagePreview";

/** Distance to the nearest of randomly placed points, one per unit cell. About 0 to 1. */
float cell_distance(const float3 &p, const uint32_t seed)
{
  const int3 cell(int(std::floor(p.x)), int(std::floor(p.y)), int(std::floor(p.z)));
  float best = FLT_MAX;
  for (int dz = -1; dz <= 1; dz++) {
    for (int dy = -1; dy <= 1; dy++) {
      for (int dx = -1; dx <= 1; dx++) {
        const int3 c = cell + int3(dx, dy, dz);
        const uint32_t h = noise::hash(uint32_t(c.x) + seed, uint32_t(c.y), uint32_t(c.z));
        const float3 point = float3(c) + float3(noise::hash_to_float(h, 1),
                                                noise::hash_to_float(h, 2),
                                                noise::hash_to_float(h, 3));
        best = std::min(best, math::distance_squared(p, point));
      }
    }
  }
  return std::sqrt(best);
}

/** Noise from -1 to 1. */
float damage_noise(const EdgeDamageParams &params, const float3 &p, const uint32_t seed)
{
  const float detail = std::clamp(params.noise_detail, 0.0f, 15.0f);
  const float roughness = std::clamp(params.noise_roughness, 0.0f, 1.0f);
  switch (params.noise_type) {
    case EdgeDamageNoise::Perlin: {
      const float3 offset(float(seed % 1021u), float((seed >> 10) % 1021u), float(seed >> 21));
      return noise::perlin_fbm(p + offset, detail, roughness, 2.0f, true) * 2.0f - 1.0f;
    }
    case EdgeDamageNoise::Cells: {
      const int octaves = int(detail) + 1;
      float total = 0.0f;
      float weight = 0.0f;
      float amplitude = 1.0f;
      float frequency = 1.0f;
      for (int octave = 0; octave < octaves; octave++) {
        total += amplitude * cell_distance(p * frequency, seed + uint32_t(octave) * 7919u);
        weight += amplitude;
        amplitude *= roughness;
        frequency *= 2.0f;
      }
      return std::clamp(total / std::max(weight, 1e-6f) * 2.0f - 1.0f, -1.0f, 1.0f);
    }
  }
  return 0.0f;
}

boolean::Solver boolean_solver(const EdgeDamageSolver solver)
{
  switch (solver) {
    case EdgeDamageSolver::Float:
      return boolean::Solver::Float;
    case EdgeDamageSolver::Exact:
      return boolean::Solver::MeshArr;
    case EdgeDamageSolver::Manifold:
      return boolean::Solver::Manifold;
  }
  return boolean::Solver::Float;
}

/** The dense copy of the mesh that is smoothed and pushed. */
OwnedMesh make_dense_copy(const Mesh &mesh, const EdgeDamageParams &params, std::string &r_message)
{
  switch (params.remesh) {
    case EdgeDamageRemesh::Isotropic: {
      IsotropicRemeshParams remesh;
      remesh.target_edge_length = params.resolution;
      remesh.iterations = std::clamp(params.remesh_iterations, 1, 20);
      remesh.preserve_boundary = true;
      remesh.transfer_attributes = true;
      return OwnedMesh(isotropic_remesh(mesh, remesh));
    }
    case EdgeDamageRemesh::Voxel: {
      OwnedMesh result(BKE_mesh_remesh_voxel(&mesh, params.resolution, 0.0f, 0.0f, nullptr));
      if (result) {
        bke::mesh_remesh_reproject_attributes(mesh, *result);
      }
      return result;
    }
    case EdgeDamageRemesh::Divide: {
      MeshDivideParams divide;
      divide.size = float3(params.resolution);
      std::string message;
      Mesh *result = mesh_divide(mesh, divide, message);
      if (!message.empty()) {
        add_message(r_message, message + " ");
      }
      return OwnedMesh(result ? result : BKE_mesh_copy_for_eval(mesh));
    }
    case EdgeDamageRemesh::None:
      return OwnedMesh(BKE_mesh_copy_for_eval(mesh));
  }
  return nullptr;
}

/**
 * Damage one closed piece. The faces of the result have #KEPT_ATTRIBUTE set where they come from
 * the original. Returns null on failure.
 */
OwnedMesh damage_piece(const Mesh &piece,
                       const EdgeDamageParams &params,
                       const uint32_t seed,
                       const float strength,
                       const float3 &origin,
                       std::string &r_message)
{
  OwnedMesh copy = make_dense_copy(piece, params, r_message);
  if (!copy || copy->faces_num == 0) {
    add_message(r_message, "A piece could not be remeshed. ");
    return nullptr;
  }
  {
    bke::MutableAttributeAccessor attributes = copy->attributes_for_write();
    attributes.remove(KEPT_ATTRIBUTE);
    attributes.add<bool>(KEPT_ATTRIBUTE, bke::AttrDomain::Face, bke::AttributeInitDefaultValue());
  }

  const int verts_num = copy->verts_num;
  Array<float> weights(verts_num);
  {
    const VArraySpan<float> transferred = *copy->attributes().lookup_or_default<float>(
        WEIGHT_ATTRIBUTE, bke::AttrDomain::Point, 1.0f);
    for (const int i : IndexRange(verts_num)) {
      weights[i] = std::clamp(transferred[i], 0.0f, 1.0f);
    }
  }

  /* Smooth: pulls the copy in at convex corners and edges, and barely moves flat areas. */
  if (params.smooth_iterations > 0 && params.smooth_factor > 0.0f) {
    Array<float> factors(verts_num);
    const float factor = std::clamp(params.smooth_factor * strength, 0.0f, 1.0f);
    for (const int i : IndexRange(verts_num)) {
      factors[i] = factor * weights[i];
    }
    MeshSmoothParams smooth;
    smooth.iterations = params.smooth_iterations;
    smooth.preserve_boundary = true;
    mesh_smooth(*copy,
                IndexMask(verts_num),
                VArray<float>::from_span(factors),
                VArray<bool>::from_single(false, copy->edges_num),
                MeshSmoothMethod::Average,
                smooth);
    copy->tag_positions_changed();
  }

  /* Push back out along the normals, with noise. */
  {
    const Array<float3> normals(copy->vert_normals());
    MutableSpan<float3> positions = copy->vert_positions_for_write();
    const float scale = std::max(params.noise_scale, 1e-5f);
    const float noise_amount = params.noise_amount * strength;
    for (const int i : IndexRange(verts_num)) {
      float distance = params.push;
      if (noise_amount != 0.0f) {
        distance += damage_noise(params, (positions[i] - origin) / scale, seed) * noise_amount *
                    weights[i];
      }
      /* Where no damage is allowed, keep the copy outside of the original. */
      if (weights[i] <= 0.0f) {
        distance = std::max(distance, params.push);
      }
      positions[i] += normals[i] * distance;
    }
    copy->tag_positions_changed();
  }

  /* Keep what is inside of both. */
  const Mesh *operands[2] = {&piece, copy.get()};
  const float4x4 transforms[2] = {float4x4::identity(), float4x4::identity()};
  boolean::BooleanOpParameters op;
  op.boolean_mode = boolean::Operation::Intersect;
  op.no_self_intersections = false;
  op.no_nested_components = false;
  boolean::BooleanError error = boolean::BooleanError::NoError;
#ifndef WITH_MANIFOLD
  if (params.solver == EdgeDamageSolver::Manifold) {
    add_message(r_message, "The Manifold solver isn't available in this build. ");
    return nullptr;
  }
#endif
  OwnedMesh result(boolean::mesh_boolean(Span<const Mesh *>(operands, 2),
                                         Span<float4x4>(transforms, 2),
                                         {},
                                         op,
                                         boolean_solver(params.solver),
                                         nullptr,
                                         &error));
  switch (error) {
    case boolean::BooleanError::NoError:
      if (!result) {
        add_message(r_message, "The intersection failed on some pieces. ");
        return nullptr;
      }
      break;
    case boolean::BooleanError::NonManifold:
      add_message(r_message, "Open or non-manifold pieces were left unchanged; try the Exact solver. ");
      return nullptr;
    case boolean::BooleanError::ResultTooBig:
      add_message(r_message, "The result is too big; increase Resolution. ");
      return nullptr;
    case boolean::BooleanError::SolverNotAvailable:
      add_message(r_message, "That solver isn't available in this build. ");
      return nullptr;
    case boolean::BooleanError::UnknownError:
      add_message(r_message, "The intersection failed on some pieces. ");
      return nullptr;
  }
  return result;
}

/** Damage attribute, material, shading and preview colors from the kept faces. */
void finish(Mesh &mesh, const EdgeDamageParams &params)
{
  const OffsetIndices<int> faces = mesh.faces();
  const Span<int> corner_verts = mesh.corner_verts();
  bke::MutableAttributeAccessor attributes = mesh.attributes_for_write();
  Array<bool> damaged(mesh.faces_num);
  {
    const VArraySpan<bool> kept = *attributes.lookup_or_default<bool>(
        KEPT_ATTRIBUTE, bke::AttrDomain::Face, true);
    for (const int face : faces.index_range()) {
      damaged[face] = !kept[face];
    }
  }
  Array<float> point_damage(mesh.verts_num, 0.0f);
  for (const int face : faces.index_range()) {
    if (damaged[face]) {
      for (const int vert : corner_verts.slice(faces[face])) {
        point_damage[vert] = 1.0f;
      }
    }
  }

  if (!params.damage_attribute.is_empty()) {
    attributes.remove(params.damage_attribute);
    bke::SpanAttributeWriter<float> output = attributes.lookup_or_add_for_write_only_span<float>(
        params.damage_attribute, bke::AttrDomain::Point);
    if (output) {
      output.span.copy_from(point_damage);
      output.finish();
    }
  }
  if (params.material_index >= 0) {
    bke::SpanAttributeWriter<int> materials = attributes.lookup_or_add_for_write_span<int>(
        "material_index", bke::AttrDomain::Face);
    for (const int face : faces.index_range()) {
      if (damaged[face]) {
        materials.span[face] = params.material_index;
      }
    }
    materials.finish();
  }
  {
    bke::SpanAttributeWriter<bool> sharp = attributes.lookup_or_add_for_write_span<bool>(
        "sharp_face", bke::AttrDomain::Face);
    for (const int face : faces.index_range()) {
      if (damaged[face]) {
        sharp.span[face] = !params.smooth_shading;
      }
    }
    sharp.finish();
  }
  if (params.visualize) {
    bke::SpanAttributeWriter<ColorGeometry4f> colors =
        attributes.lookup_or_add_for_write_only_span<ColorGeometry4f>(PREVIEW_ATTRIBUTE,
                                                                      bke::AttrDomain::Corner);
    if (colors) {
      for (const int face : faces.index_range()) {
        const ColorGeometry4f color = damaged[face] ?
                                          ColorGeometry4f(0.85f, 0.015f, 0.005f, 1.0f) :
                                          ColorGeometry4f(0.68f, 0.74f, 0.77f, 1.0f);
        for (const int corner : faces[face]) {
          colors.span[corner] = color;
        }
      }
      colors.finish();
      BKE_id_attributes_active_color_set(&mesh.id, PREVIEW_ATTRIBUTE);
      BKE_id_attributes_default_color_set(&mesh.id, PREVIEW_ATTRIBUTE);
    }
  }
  attributes.remove(KEPT_ATTRIBUTE);
  attributes.remove(WEIGHT_ATTRIBUTE);
}

double surface_area(const Mesh &mesh)
{
  const Span<float3> positions = mesh.vert_positions();
  const Span<int> corner_verts = mesh.corner_verts();
  double area = 0.0;
  for (const int3 &tri : mesh.corner_tris()) {
    const double3 a(positions[corner_verts[tri.x]]);
    const double3 b(positions[corner_verts[tri.y]]);
    const double3 c(positions[corner_verts[tri.z]]);
    area += 0.5 * math::length(math::cross(b - a, c - a));
  }
  return area;
}

}  // namespace

static Mesh *edge_damage_compute(const Mesh &mesh,
                                 const EdgeDamageParams &params,
                                 std::string &r_message)
{
  if (mesh.faces_num == 0) {
    return nullptr;
  }
  if (!(params.resolution > 0.0f) || !std::isfinite(params.resolution)) {
    r_message = "Resolution must be above 0";
    return nullptr;
  }
  if (params.remesh != EdgeDamageRemesh::None &&
      surface_area(mesh) * 4.0 / (double(params.resolution) * params.resolution) > 4000000.0)
  {
    r_message = "Resolution is too small for the size of the mesh; increase it";
    return nullptr;
  }
  if (!params.weights.is_empty() && params.weights.size() != mesh.verts_num) {
    return nullptr;
  }

  OwnedMesh source(BKE_mesh_copy_for_eval(mesh));
  {
    bke::MutableAttributeAccessor attributes = source->attributes_for_write();
    attributes.remove(KEPT_ATTRIBUTE);
    bke::SpanAttributeWriter<bool> kept = attributes.lookup_or_add_for_write_only_span<bool>(
        KEPT_ATTRIBUTE, bke::AttrDomain::Face);
    kept.span.fill(true);
    kept.finish();
    if (!params.weights.is_empty()) {
      bke::SpanAttributeWriter<float> weights =
          attributes.lookup_or_add_for_write_only_span<float>(WEIGHT_ATTRIBUTE,
                                                              bke::AttrDomain::Point);
      weights.span.copy_from(params.weights);
      weights.finish();
    }
  }

  /* The pieces, or the whole mesh as one. */
  Vector<Vector<int>> pieces;
  if (params.per_island) {
    DisjointSet<int> components(source->verts_num);
    for (const int2 &edge : source->edges()) {
      components.join(edge.x, edge.y);
    }
    std::map<int, int> piece_of_root;
    for (const int vert : IndexRange(source->verts_num)) {
      const int root = components.find_root(vert);
      const auto [it, inserted] = piece_of_root.try_emplace(root, int(pieces.size()));
      if (inserted) {
        pieces.append({});
      }
      pieces[it->second].append(vert);
    }
  }

  const uint32_t base_seed = noise::hash(uint32_t(params.seed));
  Vector<bke::GeometrySet> results;
  bool any_changed = false;

  auto process = [&](OwnedMesh piece, const uint32_t piece_seed) {
    float strength = 1.0f;
    float3 origin(0.0f);
    if (params.per_island) {
      strength = 1.0f - std::clamp(params.island_variation, 0.0f, 1.0f) *
                            noise::hash_to_float(piece_seed, 17u);
      /* Each piece gets its own noise, which moves with it. */
      double3 center(0.0);
      for (const float3 &p : piece->vert_positions()) {
        center += double3(p);
      }
      origin = float3(center / double(std::max(piece->verts_num, 1)));
    }
    OwnedMesh damaged = damage_piece(*piece, params, piece_seed, strength, origin, r_message);
    if (damaged) {
      any_changed = true;
      results.append(bke::GeometrySet::from_mesh(damaged.release()));
    }
    else {
      results.append(bke::GeometrySet::from_mesh(piece.release()));
    }
  };

  if (pieces.size() <= 1) {
    process(std::move(source), base_seed);
  }
  else {
    Array<bool> selected(source->verts_num, false);
    for (const int i : pieces.index_range()) {
      for (const int vert : pieces[i]) {
        selected[vert] = true;
      }
      const std::optional<Mesh *> copied = mesh_copy_selection(
          *source, VArray<bool>::from_span(selected), bke::AttrDomain::Point);
      for (const int vert : pieces[i]) {
        selected[vert] = false;
      }
      OwnedMesh piece(copied ? *copied : BKE_mesh_copy_for_eval(*source));
      if (!piece || piece->faces_num == 0) {
        continue;
      }
      process(std::move(piece), noise::hash(base_seed, uint32_t(pieces[i].first())));
    }
  }

  if (!any_changed) {
    return nullptr;
  }
  bke::GeometrySet joined = join_geometries(results, {});
  if (!joined.has_mesh()) {
    return nullptr;
  }
  Mesh *result = joined.get_component_for_write<bke::MeshComponent>().release();
  BKE_mesh_copy_parameters_for_eval(result, &mesh);
  finish(*result, params);
  return result;
}

namespace {

class EdgeDamageCachedValue : public memory_cache::CachedValue {
 public:
  Mesh *mesh = nullptr;
  std::string message;

  ~EdgeDamageCachedValue() override
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

}  // namespace

Mesh *edge_damage(const Mesh &mesh, const EdgeDamageParams &params, std::string &r_message)
{
  /* Objects are evaluated again for undo and for changes to other objects. Taking the result from
   * the cache when nothing it depends on changed keeps that from remeshing all over again. */
  SimCacheHasher hasher("edge_damage");
  hasher.add_mesh(mesh);
  hasher.add(params.remesh);
  hasher.add(params.resolution);
  hasher.add(params.remesh_iterations);
  hasher.add(params.smooth_iterations);
  hasher.add(params.smooth_factor);
  hasher.add(params.push);
  hasher.add(params.noise_type);
  hasher.add(params.noise_amount);
  hasher.add(params.noise_scale);
  hasher.add(params.noise_detail);
  hasher.add(params.noise_roughness);
  hasher.add(params.solver);
  hasher.add(params.per_island);
  hasher.add(params.island_variation);
  hasher.add(params.seed);
  hasher.add_span(params.weights);
  hasher.add(params.material_index);
  hasher.add(params.smooth_shading);
  hasher.add(params.visualize);
  hasher.add_string(params.damage_attribute);
  const std::shared_ptr<const EdgeDamageCachedValue> value =
      std::dynamic_pointer_cast<const EdgeDamageCachedValue>(
          sim_cache_get_value(hasher.finish(), [&]() {
            auto value = std::make_unique<EdgeDamageCachedValue>();
            value->mesh = edge_damage_compute(mesh, params, value->message);
            return value;
          }));
  if (!value) {
    return nullptr;
  }
  r_message += value->message;
  if (value->mesh == nullptr) {
    return nullptr;
  }
  return BKE_mesh_copy_for_eval(*value->mesh);
}

}  // namespace blender::geometry
