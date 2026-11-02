/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "BLI_index_mask_fwd.hh"
#include "BLI_virtual_array_fwd.hh"

namespace blender {
struct Mesh;
}

namespace blender::geometry {

enum class MeshSmoothMethod {
  /** Move points toward the average of their neighbors. The mesh shrinks with more iterations. */
  Average = 0,
  /** Alternate between shrinking and growing, which mostly keeps the size of the mesh. */
  KeepVolume = 1,
};

struct MeshSmoothParams {
  int iterations = 1;
  /** Don't move points on open boundaries. */
  bool preserve_boundary = true;
};

/**
 * Smooth the surface of \a mesh by moving the selected points.
 *
 * \param factors: Strength of the smoothing for every point, between 0 and 1.
 * \param hard_edges: For every edge, whether its points shouldn't move.
 */
void mesh_smooth(Mesh &mesh,
                 const IndexMask &selection,
                 const VArray<float> &factors,
                 const VArray<bool> &hard_edges,
                 MeshSmoothMethod method,
                 const MeshSmoothParams &params);

/**
 * Even out the distances between the selected points by sliding them along the surface.
 *
 * \param keep_on_surface: Project the points back onto the original surface after every iteration.
 */
void mesh_relax(Mesh &mesh,
                const IndexMask &selection,
                const VArray<float> &factors,
                const VArray<bool> &hard_edges,
                bool keep_on_surface,
                const MeshSmoothParams &params);

}  // namespace blender::geometry
