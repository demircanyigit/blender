/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

/** \file
 * \ingroup geo
 *
 * Edge Damage: a dense copy of the mesh is smoothed, which pulls it in most at convex corners and
 * edges, then pushed out along its normals, with noise. Intersecting it with the original keeps
 * the original surface where the copy is outside of it, and bites into the edges where the copy
 * is inside.
 */

#include <string>

#include "BLI_span.hh"
#include "BLI_string_ref.hh"

namespace blender {
struct Mesh;
}

namespace blender::geometry {

/** How the copy is made dense before smoothing. The values are stored in files. */
enum class EdgeDamageRemesh {
  /** Even triangles. */
  Isotropic = 0,
  /** A new closed surface from voxels. */
  Voxel = 1,
  /** The faces cut by a grid. */
  Divide = 2,
  /** The mesh as it is. */
  None = 3,
};

/** The values are stored in files. */
enum class EdgeDamageNoise {
  /** Soft, rolling breaks. */
  Perlin = 0,
  /** Scalloped breaks, like chips. */
  Cells = 1,
};

/** The values are stored in files. */
enum class EdgeDamageSolver {
  Float = 0,
  Exact = 1,
  Manifold = 2,
};

struct EdgeDamageParams {
  EdgeDamageRemesh remesh = EdgeDamageRemesh::Isotropic;
  /** Edge length, voxel size or grid size of the copy. */
  float resolution = 0.05f;
  /** Isotropic remesh only. */
  int remesh_iterations = 3;
  /** How much the copy is smoothed, which sets how much the edges are worn. */
  int smooth_iterations = 10;
  float smooth_factor = 0.5f;
  /** How far the smoothed copy is pushed back out. Higher keeps more of the edges. */
  float push = 0.02f;
  EdgeDamageNoise noise_type = EdgeDamageNoise::Perlin;
  /** Most the noise moves the copy in or out. */
  float noise_amount = 0.03f;
  float noise_scale = 0.15f;
  float noise_detail = 3.0f;
  float noise_roughness = 0.5f;
  EdgeDamageSolver solver = EdgeDamageSolver::Manifold;
  /** Damage each connected piece on its own, with its own pattern and strength. */
  bool per_island = true;
  /** 0 to 1: how much the strength changes from piece to piece. */
  float island_variation = 0.3f;
  int seed = 0;
  /** Per point, 0 to 1: where damage is allowed. Empty for everywhere. */
  Span<float> weights;
  /** Material index of the damaged faces, or -1 to keep the original materials. */
  int material_index = -1;
  /** Smooth shading on the damaged faces. */
  bool smooth_shading = true;
  /** Write a color attribute that shows the damage in red. */
  bool visualize = false;
  /** Point attribute that is 1 on the damaged faces. Empty for none. */
  StringRef damage_attribute = "damage";
};

/**
 * \return A new mesh, or null to keep the input. \a r_message gets warnings and errors.
 */
Mesh *edge_damage(const Mesh &mesh, const EdgeDamageParams &params, std::string &r_message);

}  // namespace blender::geometry
