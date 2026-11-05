/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

/** \file
 * \ingroup geo
 *
 * Point deformations shared by the Noise, Push, Melt and FFD modifiers and nodes. They work on
 * any list of positions, so meshes and curves both use them. \a weights may be empty for 1.
 */

#include "BLI_math_vector_types.hh"
#include "BLI_span.hh"

namespace blender::geometry {

struct NoiseDeformParams {
  /** Largest offset on X, Y and Z. */
  float3 strength = float3(0.0f, 0.0f, 1.0f);
  /** Size of the noise pattern. */
  float scale = 1.0f;
  /** Moves through the noise; animate it for motion. */
  float phase = 0.0f;
  bool fractal = false;
  float detail = 2.0f;
  float roughness = 0.5f;
  float lacunarity = 2.0f;
  int seed = 0;
};

void noise_deform(MutableSpan<float3> positions,
                  Span<float> weights,
                  const NoiseDeformParams &params);

/** Move each point along its normal. */
void push_deform(MutableSpan<float3> positions,
                 Span<float3> normals,
                 Span<float> weights,
                 float distance);

struct MeltParams {
  /** 0 is untouched; 1 is fully melted to the ground. */
  float amount = 0.0f;
  /** How far the melted part spreads out, relative to how far it fell. */
  float spread = 0.2f;
  /** 0 melts like ice cream, 1 holds the top together longer. */
  float solidity = 0.5f;
  /** 0, 1 or 2: the axis that points up. */
  int axis = 2;
  bool flip = false;
};

void melt_deform(MutableSpan<float3> positions, Span<float> weights, const MeltParams &params);

struct FFDParams {
  /** Points per side: 2, 3 or 4. */
  int resolution = 2;
  /** The box the lattice fills, in the space of the positions. */
  float3 bounds_min = float3(-1.0f);
  float3 bounds_max = float3(1.0f);
  /**
   * Offset of each control point from its rest place, \a resolution cubed of them, X fastest.
   * Offsets are in the units of the positions.
   */
  Span<float3> offsets;
  /** Deform only the points inside the box. */
  bool inside_only = false;
};

void ffd_deform(MutableSpan<float3> positions, Span<float> weights, const FFDParams &params);

/** Rest place of control point \a index of a lattice. */
float3 ffd_rest_point(const FFDParams &params, int index);

}  // namespace blender::geometry
