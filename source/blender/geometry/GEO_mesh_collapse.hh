/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "BLI_math_matrix_types.hh"
#include "BLI_span.hh"

namespace blender {
struct Mesh;
}

namespace blender::geometry {

struct MeshCollapseParams {
  /** Seconds of falling before the result is taken. */
  float time = 3.0f;
  /** Pull along -Z of the simulation space, in meters per second squared. */
  float gravity = 9.81f;
  /** Random start speed of each piece, so it doesn't stay standing. */
  float push = 1.5f;
  int seed = 0;
  float friction = 0.5f;
  float bounce = 0.1f;
  /** Solver steps per 1/60 second. */
  int substeps = 4;
  /** From the mesh to the simulation space, where -Z is down. */
  float4x4 mesh_to_space = float4x4::identity();
  /** What the pieces land on. When null, a flat floor under the lowest point. */
  const Mesh *ground = nullptr;
  float4x4 ground_to_space = float4x4::identity();
  /** The ground is pushed out along its normals by this much. Negative pulls it in. */
  float ground_cage = 0.0f;
  /** Per point: pieces with any of these points stay where they are. May be empty. */
  Span<bool> keep;
  /** Per point: points with the same number fall as one piece. When empty, every mesh island is
   * a piece. */
  Span<int> pieces;
};

/** More pieces than this are refused, the solve would take too long. */
constexpr int mesh_collapse_max_islands = 20000;

/**
 * Let every mesh island fall as a rigid piece (its convex hull) until it rests on the ground and
 * on the other pieces, and return a copy of the mesh with each island moved to where it ended up.
 * Returns null and sets `r_error` when that isn't possible.
 */
Mesh *mesh_collapse(const Mesh &mesh, const MeshCollapseParams &params, const char **r_error);

}  // namespace blender::geometry
