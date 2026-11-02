/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "BLI_math_vector_types.hh"

namespace blender {
struct BMesh;
}

namespace blender::geometry {

enum class BMeshClipKeep {
  /** Keep the side the plane normal points away from. */
  Below = 0,
  /** Keep the side the plane normal points to. */
  Above = 1,
  /** Only add the cut to the surface, and keep everything. */
  Both = 2,
};

struct BMeshClipParams {
  float3 plane_co;
  float3 plane_no;
  /** Points closer to the plane than this count as being on it. */
  float threshold = 1e-5f;
  BMeshClipKeep keep = BMeshClipKeep::Below;
  /** Close the holes left by the cut. Not used when both sides are kept. */
  bool fill = true;
  /** Offsets of optional boolean layers, set to true for the fill faces and the cut edges. */
  int interior_face_offset = -1;
  int cut_edge_offset = -1;
};

/**
 * Cut \a bm with a plane and remove one side. Edges and corners that are cut get interpolated
 * attributes, and the faces that fill the cut copy the attributes of the faces next to them.
 *
 * \a bm must have been created with tool flags.
 */
void bmesh_clip(BMesh *bm, const BMeshClipParams &params);

}  // namespace blender::geometry
