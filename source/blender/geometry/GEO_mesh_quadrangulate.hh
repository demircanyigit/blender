/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

/** \file
 * \ingroup geo
 *
 * Join pairs of triangles into quads, the same as the Triangles to Quads operator in edit mode.
 * Used by the Quadrangulate modifier and node.
 */

#include "BLI_index_mask_fwd.hh"
#include "BLI_math_base.h"

namespace blender {
struct Mesh;
}

namespace blender::geometry {

struct QuadrangulateParams {
  /** Largest angle between the normals of two triangles that can be joined. */
  float face_threshold = DEG2RADF(40.0f);
  /** Largest difference from a square corner of the joined quad. */
  float shape_threshold = DEG2RADF(40.0f);
  /** How much to prefer regular grids of quads and quads next to other quads. */
  float topology_influence = 0.0f;
  /** Don't join triangles across a difference in these. */
  bool compare_uvs = false;
  bool compare_vcols = false;
  bool compare_seam = false;
  bool compare_sharp = false;
  bool compare_materials = false;
};

/**
 * Join the selected triangles of \a mesh into quads. Returns null when there is nothing to join.
 */
Mesh *mesh_quadrangulate(const Mesh &mesh,
                         const IndexMask &selection,
                         const QuadrangulateParams &params);

}  // namespace blender::geometry
