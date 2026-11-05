/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

/** \file
 * \ingroup geo
 *
 * Automatic UV unwrap: faces are projected flat from a few directions picked from their normals,
 * then the islands are packed. Shared by the Smart UV Project modifier and node.
 */

#include "BLI_index_mask_fwd.hh"
#include "BLI_math_vector_types.hh"
#include "BLI_span.hh"

namespace blender {
struct Mesh;
}

namespace blender::geometry {

struct SmartUVProjectParams {
  /** Faces whose normals differ by less than this share a projection. */
  float angle_limit = 1.151917f;
  /** Space between islands after packing. */
  float margin = 0.0f;
  /** Rotate islands to fit them better. */
  bool rotate = true;
  /** Pack the islands into the UV square. Without it, they keep their real size. */
  bool pack = true;
};

/**
 * Unwrap the selected faces into \a r_uvs (one per face corner). Corners of other faces keep
 * their values.
 */
void smart_uv_project(const Mesh &mesh,
                      const IndexMask &selection,
                      const SmartUVProjectParams &params,
                      MutableSpan<float2> r_uvs);

}  // namespace blender::geometry
