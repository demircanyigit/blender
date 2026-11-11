/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "BLI_span.hh"

namespace blender {
struct Mesh;
}

namespace blender::geometry {

/** The steps run in the order they are listed here. */
struct MeshCleanupParams {
  /** Delete points with no edges, edges with no faces, and faces that touch no other face. */
  bool loose_verts = true;
  bool loose_edges = true;
  bool loose_faces = false;
  /** Delete faces that use the same points as another face. */
  bool duplicate_faces = true;
  /** Delete faces whose edges are all shared by more than two faces. */
  bool interior_faces = false;
  /** Remove edges shorter than the distance, and faces with no area. */
  bool degenerate = true;
  float degenerate_distance = 0.0001f;
  /** Remove points in the middle of straight lines, between only two edges. */
  bool extra_verts = false;
  float extra_verts_angle = 0.0174533f;
  /** Merge faces where the surface stays nearly flat. */
  bool limited = false;
  float limited_angle = 0.0872665f;
  bool limited_all_boundaries = false;
  /** #BMO_Delimit flags. */
  int limited_delimit = 1;
  /** Delete loose parts with fewer faces than this, or smaller than the size. */
  bool small_pieces = false;
  int small_pieces_faces = 4;
  float small_pieces_size = 0.0f;
  /** Fill holes with at most this many sides. Zero fills all holes. */
  bool fill_holes = false;
  int fill_holes_sides = 4;
  /** Flatten faces. */
  bool make_planar = false;
  float make_planar_factor = 1.0f;
  int make_planar_iterations = 1;
  /** Split faces that bend more than the angle. */
  bool split_nonplanar = false;
  float split_nonplanar_angle = 0.0872665f;
  /** Split faces that are not convex. */
  bool split_concave = false;
  /** Make all faces point outward. */
  bool recalc_normals = false;
};

/**
 * Clean up a mesh in one go.
 * \param selection: Per point. Only points in it, and the edges and faces made of them, are
 * cleaned up. Empty means all.
 * \return The new mesh, or null if there was nothing to do.
 */
Mesh *mesh_cleanup(const Mesh &mesh, Span<bool> selection, const MeshCleanupParams &params);

}  // namespace blender::geometry
