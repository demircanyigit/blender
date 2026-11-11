/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

/** \file
 * \ingroup geo
 *
 * Distribute: packs objects on a surface by their measured sides, so none of them touch.
 */

#include <optional>
#include <string>

#include "BLI_math_matrix_types.hh"
#include "BLI_math_vector_types.hh"
#include "BLI_span.hh"
#include "BLI_vector.hh"

#include "GEO_mesh_stonework.hh"

struct Mesh;

namespace blender::geometry {

enum class DistributeRotation : int8_t {
  /** Lined up with the direction, plus the jitter. */
  Align = 0,
  /** Turned in steps of 90 degrees. */
  Steps = 1,
  /** Turned at random. */
  Random = 2,
};

enum class DistributeDirection : int8_t {
  /** Level on walls, along X on floors. */
  Auto = 0,
  WorldX = 1,
  WorldY = 2,
};

/** One object to place: its bounding box, in its own space. */
struct DistributeItem {
  float3 min = float3(-0.5f);
  float3 max = float3(0.5f);
  /** Only used to fill the gaps at the end. */
  bool filler = false;
};

struct DistributeParams {
  Span<DistributeItem> items;
  /** Per face, which faces get objects. Empty for all. */
  Span<bool> selection;
  /** Space between the objects. */
  float gap = 0.01f;
  /** How many spots are tried for each object. More packs tighter, slower. */
  int tries = 24;
  /** Objects stand on the faces. Otherwise they stay upright. */
  bool align_to_normal = true;
  DistributeDirection direction = DistributeDirection::Auto;
  DistributeRotation rotation = DistributeRotation::Align;
  /** Largest random turn, in radians. */
  float jitter = 0.0f;
  float scale_min = 1.0f;
  float scale_max = 1.0f;
  /** Distance along the normal, from the bottom of each object to the surface. */
  float offset = 0.0f;
  /** Objects must not hang over the edges of the surface. */
  bool stay_inside = true;
  /** Fill the gaps at the end with the filler objects, or smaller copies of the others. */
  bool fillers = true;
  /** Size of the smaller copies, when there are no filler objects. */
  float filler_scale = 0.5f;
  int max_count = 5000;
  int seed = 0;
};

struct DistributePlacement {
  int item;
  /** Where the object's origin goes, and how it is turned and scaled. */
  float4x4 transform;
  bool filler;
};

Vector<DistributePlacement> distribute_by_size(const Mesh &surface,
                                               const DistributeParams &params);

struct DistributeMortarParams {
  /** Distance along the normal, negative goes in. */
  float offset = 0.0f;
  StoneworkRemesh remesh = StoneworkRemesh::Isotropic;
  float resolution = 0.05f;
  /** Bumps along the normal. */
  float noise_strength = 0.01f;
  float noise_scale = 5.0f;
  float noise_detail = 3.0f;
  int seed = 0;
  int material_index = 0;
  /** Per face, which faces get mortar. Empty for all. */
  Span<bool> selection;
};

/** A copy of the surface, made dense and bumpy, to go between the objects. */
Mesh *distribute_mortar(const Mesh &surface,
                        const DistributeMortarParams &params,
                        std::string &r_message);

/** A random color for one object, as Stonework makes them. */
float4 distribute_random_color(int index,
                               int seed,
                               StoneworkColorMode mode,
                               float2 hue,
                               float2 saturation,
                               float2 value);

}  // namespace blender::geometry
