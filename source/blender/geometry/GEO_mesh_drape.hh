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

struct MeshDrapeParams {
  /** Seconds the cloth falls and settles for. */
  float time = 2.0f;
  /** Pull along -Z of the simulation space, in meters per second squared. */
  float gravity = 9.81f;
  /** 0 to 1: how much the cloth resists stretching. */
  float stiffness = 1.0f;
  /** 0 to 1: how much the cloth resists folding, from silk to leather. */
  float bending = 0.1f;
  /** Fraction the cloth tries to get smaller by, so it pulls taut between pins. Negative gives
   * slack. */
  float shrink = 0.0f;
  /** Distance the cloth keeps from colliders and the floor. */
  float thickness = 0.01f;
  /** 0 to 1: how much the cloth grips what it touches instead of sliding. */
  float friction = 0.5f;
  /** Push along the face normals, which puffs up closed meshes. */
  float pressure = 0.0f;
  /** A flat floor under the lowest point of the cloth and the collider. */
  bool floor = true;
  bool self_collision = false;
  /** Solver steps per 1/60 second. */
  int substeps = 8;
  /** From the mesh to the simulation space, where -Z is down. */
  float4x4 mesh_to_space = float4x4::identity();
  /** What the cloth lands on. May be null. */
  const Mesh *collider = nullptr;
  float4x4 collider_to_space = float4x4::identity();
  /** The collider is pushed out along its normals by this much. Negative pulls it in. */
  float collider_cage = 0.0f;
  /** Per point: these stay where they are. May be empty. */
  Span<bool> pin;
};

/** More points than this are refused, the solve would take too long. */
constexpr int mesh_drape_max_verts = 1000000;

/**
 * Let the mesh fall as cloth onto the collider and the floor until it rests, and return a copy of
 * the mesh in that shape. Returns null and sets `r_error` when that isn't possible.
 */
Mesh *mesh_drape(const Mesh &mesh, const MeshDrapeParams &params, const char **r_error);

}  // namespace blender::geometry
