/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

/** \file
 * \ingroup geo
 *
 * Extrude faces along their normals and scale them, in the style of the 3ds Max Face Extrude
 * modifier. Used by the Face Extrude modifier and node.
 */

#include <optional>
#include <string>

#include "BLI_index_mask_fwd.hh"

namespace blender {
struct Mesh;
}

namespace blender::geometry {

struct FaceExtrudeParams {
  /** Distance to move the faces along their normals. */
  float amount = 0.1f;
  /** Scale of the extruded faces around their center. */
  float scale = 1.0f;
  /** Extrude each face on its own, rather than connected faces as one region. */
  bool individual = false;
  /** Names of optional boolean face attributes set on the new top and side faces. */
  std::optional<std::string> top_id;
  std::optional<std::string> side_id;
};

/** Returns a new mesh, or null when no face is selected. */
Mesh *mesh_face_extrude(const Mesh &mesh,
                        const IndexMask &face_selection,
                        const FaceExtrudeParams &params);

}  // namespace blender::geometry
