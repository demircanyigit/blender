/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

/** \file
 * \ingroup geo
 *
 * Close the holes of a mesh with new faces. Used by the Cap Holes modifier and node.
 */

#include <optional>
#include <string>

namespace blender {
struct Mesh;
}

namespace blender::geometry {

struct CapHolesParams {
  /** Only fill holes with at most this many sides. Zero fills holes of any size. */
  int max_sides = 0;
  /** Split the new faces into triangles. */
  bool triangulate = false;
  /** Name of an optional boolean face attribute set to true on the new faces. */
  std::optional<std::string> new_faces_id;
};

/**
 * Fill every hole of \a mesh (a loop of boundary edges) with a face. The new faces copy the
 * attributes of the faces next to them. Returns null when there is no hole to fill.
 */
Mesh *mesh_cap_holes(const Mesh &mesh, const CapHolesParams &params);

}  // namespace blender::geometry
