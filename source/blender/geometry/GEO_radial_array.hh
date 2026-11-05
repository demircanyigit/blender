/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

/** \file
 * \ingroup geo
 *
 * Copies of a mesh arranged around an axis. Used by the Radial Array modifier and node.
 */

#include "BLI_math_vector_types.hh"

namespace blender {
struct Mesh;
}

namespace blender::geometry {

struct RadialArrayParams {
  int count = 6;
  /** Angle the copies are spread over. A full turn leaves no copy on top of the first. */
  float angle = 6.2831853f;
  /** 0, 1 or 2: the axis the copies turn around. */
  int axis = 2;
  float3 center = float3(0.0f);
  /** Move each copy away from the axis by this distance. */
  float radius = 0.0f;
  /** Move each copy along the axis by this distance more than the one before, for a spiral. */
  float step = 0.0f;
  /** Turn each copy to face out from the axis, rather than only moving it. */
  bool rotate_copies = true;
  bool merge = false;
  float merge_distance = 0.001f;
};

/** Returns a new mesh, or null when \a mesh is empty. */
Mesh *radial_array(const Mesh &mesh, const RadialArrayParams &params);

}  // namespace blender::geometry
