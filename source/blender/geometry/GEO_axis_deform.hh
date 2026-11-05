/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

/** \file
 * \ingroup geo
 *
 * Bend, twist, taper and stretch along an axis of a gizmo, in the style of the 3ds Max
 * modifiers of the same names. Used by the Bend, Twist, Taper and Stretch modifiers and nodes.
 */

#include "BLI_math_matrix_types.hh"
#include "BLI_math_vector_types.hh"
#include "BLI_span.hh"

namespace blender::geometry {

enum class AxisDeformType {
  Bend = 0,
  Twist = 1,
  Taper = 2,
  Stretch = 3,
};

struct AxisDeformParams {
  AxisDeformType type = AxisDeformType::Bend;
  /** 0, 1 or 2: the gizmo axis along which the shape changes. */
  int axis = 2;
  /** Gizmo center and rotation, in the space of the positions. */
  float3 center = float3(0.0f);
  float3 rotation = float3(0.0f);
  /** Bend and twist: total angle over the length of the shape (or between the limits). */
  float angle = 0.0f;
  /** Bend: angle around the axis of the side the shape bends toward. */
  float direction = 0.0f;
  /** Taper: scale at the top relative to the center. Stretch: scale along the axis. */
  float amount = 0.0f;
  /** Taper: bulge of the sides. Stretch: how much the sides grow thinner as it stretches. */
  float curve = 0.0f;
  /** Only change the part between these gizmo coordinates, and carry the rest along. */
  bool use_limits = false;
  float lower = -1.0f;
  float upper = 1.0f;
};

/** The transform from gizmo space to the space of the positions. */
float4x4 axis_deform_gizmo_matrix(const AxisDeformParams &params);

/**
 * Deform \a positions in place. \a weights, when not empty, blends each point between its
 * original and deformed position.
 */
void axis_deform(MutableSpan<float3> positions,
                 const AxisDeformParams &params,
                 Span<float> weights = {});

}  // namespace blender::geometry
