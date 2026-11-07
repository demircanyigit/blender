/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include <optional>
#include <string>

#include "BLI_math_matrix_types.hh"
#include "BLI_math_vector_types.hh"

namespace blender {
struct Mesh;
}

namespace blender::geometry {

enum class MeshDiceMode {
  /** A number of cells on each axis. */
  Count = 0,
  /** Cells of a size. The last cell on each axis is smaller if the box doesn't divide evenly. */
  Size = 1,
};

struct MeshDiceParams {
  MeshDiceMode mode = MeshDiceMode::Count;
  int3 counts = int3(2);
  /** Zero doesn't cut along that axis. */
  float3 size = float3(0.5f);
  /** Distance between neighboring pieces. */
  float gap = 0.0f;
  /** Close the holes left by the cuts. */
  bool fill = true;
  /** Number every connected part of a cell as its own piece. */
  bool separate_islands = false;
  /** From the mesh to the space whose bounding box and axes are cut along. */
  float4x4 mesh_to_space = float4x4::identity();
  /** Optional point attributes with the piece number and the grid cell, and a face attribute
   * that is true for the faces that fill the cuts. */
  std::optional<std::string> piece_index_id;
  std::optional<std::string> cell_index_id;
  std::optional<std::string> interior_id;
};

/** Pieces with more cells than this are probably a mistake, and would take very long. */
constexpr int64_t mesh_dice_max_cells = 1000000;

/**
 * Cut a mesh into a grid of pieces along the axes of its bounding box.
 * \return The new mesh, or null if there would be more than #mesh_dice_max_cells cells.
 */
Mesh *mesh_dice(const Mesh &mesh, const MeshDiceParams &params);

}  // namespace blender::geometry
