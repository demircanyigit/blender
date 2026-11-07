/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup geo
 *
 * Shatter: break a closed mesh the way a material breaks. Every material is a layout of Voronoi
 * cell points, cut by #mesh_voronoi_fracture:
 * - Stone: points of mixed spacing with small chips next to some of them.
 * - Glass: points on rays out from the impact and rings around it, so the cells are shards.
 * - Wood: the cut is done in a space squeezed along the grain, so the cells come out as splinters.
 * - Ceramic: few, even points.
 * - Ice: glass rays mixed with stone chunks.
 *
 * Roughness subdivides the broken faces and moves their points by noise of their position. The
 * broken faces of neighboring pieces are at the same place, so they move the same way and the
 * cracks stay closed.
 */

#include <algorithm>
#include <cmath>

#include "DNA_mesh_types.h"

#include "BLI_bounds.hh"
#include "BLI_math_vector.h"
#include "BLI_math_vector.hh"
#include "BLI_noise.hh"
#include "BLI_rand.hh"
#include "BLI_vector.hh"

#include "BKE_attribute.hh"
#include "BKE_customdata.hh"
#include "BKE_lib_id.hh"
#include "BKE_mesh.hh"

#include "bmesh.hh"

#include "GEO_mesh_shatter.hh"
#include "GEO_mesh_voronoi_fracture.hh"

namespace blender::geometry {

/** Face attribute that marks the broken faces while the pieces are made. */
static const char *INTERIOR_ATTRIBUTE = ".shatter_interior";

namespace {

constexpr float TAU = 6.28318530718f;

struct PointLayout {
  Bounds<float3> bounds;
  float3 impact;
  /** Typical spacing of the cell points. */
  float cell_size;
  RandomNumberGenerator rng;
};

float3 random_in_bounds(PointLayout &layout)
{
  const float3 t(layout.rng.get_float(), layout.rng.get_float(), layout.rng.get_float());
  return layout.bounds.min + (layout.bounds.max - layout.bounds.min) * t;
}

/** A point in the bounds, pulled toward the impact by `focus`, so the pieces are smaller there. */
float3 focused_point(PointLayout &layout, const float focus)
{
  const float3 p = random_in_bounds(layout);
  if (layout.rng.get_float() >= focus) {
    return p;
  }
  const float pull = layout.rng.get_float();
  return layout.impact + (p - layout.impact) * (pull * pull);
}

void chunky_points(PointLayout &layout,
                   const int count,
                   const float focus,
                   const float chips,
                   Vector<float3> &points)
{
  for ([[maybe_unused]] const int i : IndexRange(count)) {
    const float3 p = focused_point(layout, focus);
    points.append(p);
    if (layout.rng.get_float() < chips) {
      /* A small chip at the edge of this piece. */
      points.append(p + layout.rng.get_unit_float3() * (layout.cell_size * 0.3f));
    }
  }
}

/** Points on rays out from the impact, spaced wider farther out, in the plane across the thinnest
 * side of the bounds. */
void radial_points(PointLayout &layout, const int count, const float focus, Vector<float3> &points)
{
  const float3 size = layout.bounds.max - layout.bounds.min;
  int thin = 0;
  for (const int axis : {1, 2}) {
    if (size[axis] < size[thin]) {
      thin = axis;
    }
  }
  const int axis_u = (thin + 1) % 3;
  const int axis_v = (thin + 2) % 3;
  float reach = 0.0f;
  for (const int corner : IndexRange(4)) {
    const float u = (corner & 1) ? layout.bounds.max[axis_u] : layout.bounds.min[axis_u];
    const float v = (corner & 2) ? layout.bounds.max[axis_v] : layout.bounds.min[axis_v];
    reach = std::max(reach,
                     math::length(float2(u - layout.impact[axis_u], v - layout.impact[axis_v])));
  }
  const float middle = (layout.bounds.min[thin] + layout.bounds.max[thin]) * 0.5f;

  const int rays = std::clamp(int(std::round(std::sqrt(float(count)) * 1.3f)), 5, 32);
  const int rings = std::max(1, count / rays);
  const float ray_step = TAU / float(rays);
  /* Rings close together at the impact and farther apart outside. */
  const float spread = 1.0f + std::clamp(focus, 0.0f, 1.0f) * 1.5f;
  points.append(layout.impact);
  for (const int ray : IndexRange(rays)) {
    const float ray_angle = ray_step * (float(ray) + (layout.rng.get_float() - 0.5f) * 0.7f);
    for (const int ring : IndexRange(rings)) {
      const float t = (float(ring) + 0.5f + (layout.rng.get_float() - 0.5f) * 0.6f) / float(rings);
      const float radius = reach * std::pow(std::max(t, 0.02f), spread);
      const float angle = ray_angle + (layout.rng.get_float() - 0.5f) * ray_step * 0.5f;
      float3 p;
      p[thin] = middle;
      p[axis_u] = layout.impact[axis_u] + std::cos(angle) * radius;
      p[axis_v] = layout.impact[axis_v] + std::sin(angle) * radius;
      points.append(p);
    }
  }
}

int longest_axis(const Bounds<float3> &bounds)
{
  const float3 size = bounds.max - bounds.min;
  int axis = 0;
  for (const int i : {1, 2}) {
    if (size[i] > size[axis]) {
      axis = i;
    }
  }
  return axis;
}

float3 noise_offset(const float3 &p)
{
  return float3(noise::perlin_signed(p),
                noise::perlin_signed(p + float3(31.4f, 7.1f, 19.3f)),
                noise::perlin_signed(p + float3(-12.7f, 45.2f, -3.9f)));
}

/** Subdivide the broken faces and move their points by noise. */
Mesh *roughen(const Mesh &mesh, const float amount, const float frequency, const int detail)
{
  BMeshCreateParams create_params{};
  create_params.use_toolflags = true;
  BMeshFromMeshParams convert_params{};
  convert_params.calc_face_normal = true;
  convert_params.calc_vert_normal = true;
  BMesh *bm = BKE_mesh_to_bmesh_ex(&mesh, &create_params, &convert_params);

  const int interior_offset = CustomData_get_offset_named(
      &bm->pdata, CD_PROP_BOOL, INTERIOR_ATTRIBUTE);
  if (interior_offset == -1) {
    BM_mesh_free(bm);
    return nullptr;
  }

  auto tag_broken_faces = [&]() {
    BM_mesh_elem_hflag_disable_all(bm, BM_FACE | BM_EDGE | BM_VERT, BM_ELEM_TAG, false);
    BMIter iter;
    BMFace *face;
    BM_ITER_MESH (face, &iter, bm, BM_FACES_OF_MESH) {
      if (BM_ELEM_CD_GET_BOOL(face, interior_offset)) {
        BM_elem_flag_enable(face, BM_ELEM_TAG);
      }
    }
  };

  /* Triangles, so the subdivision fills the faces with points too, not only their edges. */
  tag_broken_faces();
  BMO_op_callf(bm,
               BMO_FLAG_DEFAULTS,
               "triangulate faces=%hf quad_method=%i ngon_method=%i",
               BM_ELEM_TAG,
               0,
               0);

  if (detail > 0) {
    tag_broken_faces();
    BMIter iter;
    BMFace *face;
    BM_ITER_MESH (face, &iter, bm, BM_FACES_OF_MESH) {
      if (!BM_elem_flag_test(face, BM_ELEM_TAG)) {
        continue;
      }
      BMLoop *loop = BM_FACE_FIRST_LOOP(face);
      do {
        BM_elem_flag_enable(loop->e, BM_ELEM_TAG);
      } while ((loop = loop->next) != BM_FACE_FIRST_LOOP(face));
    }
    BMO_op_callf(bm,
                 BMO_FLAG_DEFAULTS,
                 "subdivide_edges edges=%he cuts=%i use_grid_fill=%b use_single_edge=%b",
                 BM_ELEM_TAG,
                 detail,
                 true,
                 false);
  }

  /* Move every point of a broken face. Points with the same place get the same offset. */
  tag_broken_faces();
  {
    BMIter iter;
    BMFace *face;
    BM_ITER_MESH (face, &iter, bm, BM_FACES_OF_MESH) {
      if (!BM_elem_flag_test(face, BM_ELEM_TAG)) {
        continue;
      }
      BMLoop *loop = BM_FACE_FIRST_LOOP(face);
      do {
        BM_elem_flag_enable(loop->v, BM_ELEM_TAG);
      } while ((loop = loop->next) != BM_FACE_FIRST_LOOP(face));
    }
    BMVert *vert;
    BM_ITER_MESH (vert, &iter, bm, BM_VERTS_OF_MESH) {
      if (!BM_elem_flag_test(vert, BM_ELEM_TAG)) {
        continue;
      }
      const float3 p(vert->co);
      const float3 offset = noise_offset(p * frequency) +
                            noise_offset(p * (frequency * 2.3f) + float3(5.0f)) * 0.45f;
      copy_v3_v3(vert->co, p + offset * amount);
    }
  }

  Mesh *result = BKE_mesh_from_bmesh_for_eval_nomain(bm, nullptr, &mesh);
  BM_mesh_free(bm);
  return result;
}

}  // namespace

Mesh *mesh_shatter(const Mesh &mesh, const MeshShatterParams &params, const char **r_error)
{
  *r_error = nullptr;
  const std::optional<Bounds<float3>> mesh_bounds = mesh.bounds_min_max();
  if (mesh.faces_num == 0 || !mesh_bounds) {
    return nullptr;
  }

  /* Wood is cut in a space squeezed along the grain, so the cells come out long. */
  const bool use_grain = params.material == ShatterMaterial::Wood && params.grain > 1.0f;
  const int grain_axis = params.grain_axis >= 0 && params.grain_axis < 3 ?
                             params.grain_axis :
                             longest_axis(*mesh_bounds);
  float3 squeeze(1.0f);
  if (use_grain) {
    squeeze[grain_axis] = 1.0f / params.grain;
  }
  const Mesh *cut_mesh = &mesh;
  Mesh *squeezed = nullptr;
  if (use_grain) {
    squeezed = BKE_mesh_copy_for_eval(mesh);
    for (float3 &p : squeezed->vert_positions_for_write()) {
      p *= squeeze;
    }
    squeezed->tag_positions_changed();
    cut_mesh = squeezed;
  }

  const int pieces = std::max(params.pieces, 1);
  PointLayout layout{};
  layout.bounds = {mesh_bounds->min * squeeze, mesh_bounds->max * squeeze};
  layout.impact = params.impact ? *params.impact * squeeze :
                                  (layout.bounds.min + layout.bounds.max) * 0.5f;
  {
    const float3 size = math::max(layout.bounds.max - layout.bounds.min, float3(1e-6f));
    layout.cell_size = std::cbrt(size.x * size.y * size.z / float(pieces));
  }
  layout.rng = RandomNumberGenerator(uint32_t(params.seed) * 2654435761u + 13u);

  const float focus = std::clamp(params.focus, 0.0f, 1.0f);
  Vector<float3> points;
  switch (params.material) {
    case ShatterMaterial::Stone:
      chunky_points(layout, pieces, focus, 0.35f, points);
      break;
    case ShatterMaterial::Glass:
      radial_points(layout, pieces, focus, points);
      break;
    case ShatterMaterial::Wood:
      chunky_points(layout, pieces, focus, 0.0f, points);
      break;
    case ShatterMaterial::Ceramic:
      chunky_points(layout, pieces, focus * 0.5f, 0.0f, points);
      break;
    case ShatterMaterial::Ice: {
      const int chunks = std::max(pieces / 3, 1);
      radial_points(layout, pieces - chunks, focus, points);
      chunky_points(layout, chunks, focus, 0.2f, points);
      break;
    }
  }

  MeshVoronoiFractureParams fracture_params;
  fracture_params.cut_gap = params.cut_gap;
  fracture_params.separate_islands = true;
  fracture_params.piece_index_id = params.piece_index_id;
  fracture_params.interior_id = INTERIOR_ATTRIBUTE;
  MeshVoronoiFractureError error = MeshVoronoiFractureError::NoError;
  Mesh *result = mesh_voronoi_fracture(*cut_mesh, points, fracture_params, &error);
  if (squeezed != nullptr) {
    BKE_id_free(nullptr, squeezed);
  }
  if (result == nullptr) {
    switch (error) {
      case MeshVoronoiFractureError::NonManifold:
        *r_error = "The mesh must be closed and manifold";
        break;
      case MeshVoronoiFractureError::SolverNotAvailable:
        *r_error = "Built without the Manifold library";
        break;
      default:
        *r_error = "The mesh could not be shattered";
        break;
    }
    return nullptr;
  }

  /* Roughness in the squeezed space too, so wood is rougher across the grain than along it. */
  if (params.roughness > 0.0f) {
    const float amount = params.roughness * layout.cell_size * 0.15f;
    const float frequency = 2.5f / layout.cell_size;
    if (Mesh *rough = roughen(*result, amount, frequency, std::clamp(params.detail, 0, 6))) {
      BKE_id_free(nullptr, result);
      result = rough;
    }
  }

  if (use_grain) {
    const float3 stretch = 1.0f / squeeze;
    for (float3 &p : result->vert_positions_for_write()) {
      p *= stretch;
    }
    result->tag_positions_changed();
  }

  bke::MutableAttributeAccessor attributes = result->attributes_for_write();
  if (params.interior_id) {
    attributes.rename(INTERIOR_ATTRIBUTE, *params.interior_id);
  }
  else {
    attributes.remove(INTERIOR_ATTRIBUTE);
  }
  return result;
}

}  // namespace blender::geometry
