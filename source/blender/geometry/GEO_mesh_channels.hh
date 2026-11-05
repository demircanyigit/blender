/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

/** \file
 * \ingroup geo
 *
 * Values computed per element of a mesh: material indices by element, tension, and data
 * channels. Used by the Material By Element, Tension and Data Channel modifiers and nodes.
 */

#include "BLI_math_vector_types.hh"
#include "BLI_span.hh"

namespace blender {
struct Mesh;
}

namespace blender::geometry {

/* -------------------------------------------------------------------- */
/** \name Material By Element
 * \{ */

enum class MaterialByElementMode {
  /** A random index for each face. */
  Random = 0,
  /** A random index for each connected piece. */
  Islands = 1,
  /** Go through the indices in order, face by face. */
  Cycle = 2,
  /** One index for each of the six directions a face can point to. */
  Direction = 3,
};

struct MaterialByElementParams {
  MaterialByElementMode mode = MaterialByElementMode::Random;
  int min_index = 0;
  int max_index = 1;
  int seed = 0;
};

void material_by_element(const Mesh &mesh,
                         const MaterialByElementParams &params,
                         MutableSpan<int> r_face_material_indices);

/** \} */

/* -------------------------------------------------------------------- */
/** \name Tension
 * \{ */

/**
 * For each point, how much the edges around it are stretched (positive) or squashed (negative)
 * compared to \a rest_positions, as a fraction of their rest length, times \a strength.
 */
void mesh_tension(const Mesh &mesh,
                  Span<float3> rest_positions,
                  float strength,
                  MutableSpan<float> r_tension);

/** \} */

/* -------------------------------------------------------------------- */
/** \name Data Channel
 * \{ */

enum class DataChannelSource {
  PositionX = 0,
  PositionY = 1,
  PositionZ = 2,
  NormalX = 3,
  NormalY = 4,
  NormalZ = 5,
  /** Positive on outward bumps and negative in hollows. */
  Convexity = 6,
  /** Average length of the edges around each point. */
  EdgeLength = 7,
  /** Average area of the faces around each point. */
  FaceArea = 8,
  Random = 9,
  /** Distance from the center of the bounding box. */
  DistanceFromCenter = 10,
};

struct DataChannelParams {
  DataChannelSource source = DataChannelSource::Convexity;
  int seed = 0;
  /** Remap the values so the smallest is 0 and the largest is 1. */
  bool normalize = true;
  bool invert = false;
  /** Raise to this power after normalizing. */
  float power = 1.0f;
  float multiply = 1.0f;
  float add = 0.0f;
  bool clamp = false;
};

/** Compute a value for each point. */
void data_channel(const Mesh &mesh, const DataChannelParams &params, MutableSpan<float> r_values);

/** \} */

}  // namespace blender::geometry
