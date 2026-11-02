/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include <optional>
#include <string>

#include "BLI_math_vector_types.hh"
#include "BLI_span.hh"

struct Mesh;

namespace blender::geometry {

enum class MeshVoronoiFractureError {
  NoError = 0,
  /** The input is not a closed volume, so it has no inside to fill the pieces with. */
  NonManifold = 1,
  SolverNotAvailable = 2,
  UnknownError = 3,
};

struct MeshVoronoiFractureParams {
  /** Distance between the inside faces of neighboring pieces. */
  float cut_gap = 0.0f;
  /** Make a separate piece of every disconnected part of a cell. */
  bool separate_islands = true;
  /** Optional names of the point-domain integer attributes for the piece and its cell point. */
  std::optional<std::string> piece_index_id;
  std::optional<std::string> cell_index_id;
  /** Optional name of a face-domain boolean attribute, true for faces made by the cuts. */
  std::optional<std::string> interior_id;
};

/**
 * Cut a closed mesh into the Voronoi cells of \a cell_points. The vertices of the pieces are not
 * shared with other pieces, so every piece is a closed mesh of its own.
 *
 * The attributes of the input are kept. Like the Manifold solver of the mesh boolean, new vertices
 * get default values for point attributes, while corner attributes are interpolated.
 *
 * \return A newly allocated, main-database-free #Mesh, or null if \a r_error was set. The mesh is
 * empty if no cell contains any part of the input.
 */
Mesh *mesh_voronoi_fracture(const Mesh &mesh,
                            Span<float3> cell_points,
                            const MeshVoronoiFractureParams &params,
                            MeshVoronoiFractureError *r_error);

}  // namespace blender::geometry
