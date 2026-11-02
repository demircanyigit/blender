/* SPDX-FileCopyrightText: 2025 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "GEO_mesh_boolean.hh"

namespace blender::geometry::boolean {

Mesh *mesh_boolean_manifold(Span<const Mesh *> meshes,
                            Span<float4x4> transforms,
                            Span<Array<short>> material_remaps,
                            BooleanOpParameters op_params,
                            Vector<int> *r_intersecting_edges,
                            BooleanError *r_error);

/** A convex volume, as the positions on its surface and the normals of its sides. */
struct FractureCell {
  int point_index = -1;
  Vector<double3> points;
  Vector<double3> normals;
};

/**
 * Create a closed piece of \a mesh for each part of it that is inside of one of \a cells. The cells
 * must not overlap.
 *
 * \param r_vert_pieces: The index of the piece of each vertex in the result.
 * \param r_piece_cells: The #FractureCell::point_index of each piece.
 * \param r_interior_faces: For each face in the result, whether it comes from a cell.
 */
Mesh *mesh_fracture_manifold(const Mesh &mesh,
                             Span<FractureCell> cells,
                             bool separate_islands,
                             Array<int> &r_vert_pieces,
                             Vector<int> &r_piece_cells,
                             Array<bool> &r_interior_faces,
                             BooleanError *r_error);

}
