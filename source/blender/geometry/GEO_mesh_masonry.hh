/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include <optional>
#include <string>

#include "BLI_math_vector_types.hh"
#include "BLI_span.hh"
#include "BLI_vector.hh"

namespace blender {
struct Mesh;
}

namespace blender::geometry {

/** The layout of the blocks. The values are stored in files. */
enum class MasonryPattern {
  /* Walls. */
  Running = 0,
  Stack = 1,
  Flemish = 2,
  English = 3,
  Ashlar = 4,
  Ledgestone = 5,
  Rubble = 6,
  /* Paving. */
  Herringbone = 7,
  Chevron = 8,
  BasketWeave = 9,
  Pinwheel = 10,
  Versailles = 11,
  Hexagon = 12,
  Octagon = 13,
  Diamond = 14,
  Interlocking = 15,
  Cobblestone = 16,
  Flagstone = 17,
  Fan = 18,
  Radial = 19,
  /** The polygons of #MeshMasonryParams::tile, repeated. */
  Custom = 20,
};

/** Which way the rows run. The values are stored in files. */
enum class MasonryDirection {
  /** Level rows on walls, along X on floors. */
  Auto = 0,
  WorldX = 1,
  WorldY = 2,
  /** Along the longest edge of each face. */
  Face = 3,
};

struct MeshMasonryParams {
  MasonryPattern pattern = MasonryPattern::Running;
  MasonryDirection direction = MasonryDirection::Auto;
  /** Size of a block along the rows, across them, and out of the face. */
  float length = 0.3f;
  float height = 0.1f;
  float depth = 0.05f;
  /** Width of the joints between blocks. */
  float mortar = 0.01f;
  /** 0: the blocks stand on the face. 1: their tops are at the face, so they sink in. */
  float offset = 0.0f;
  /** Keep the faces under the blocks, as the mortar. */
  bool backing = true;
  /** Turn of the whole layout around the face normal, in radians. */
  float angle = 0.0f;
  /** Random changes per block, 0 to 1. */
  float random_length = 0.0f;
  float random_height = 0.0f;
  float random_depth = 0.0f;
  /** Largest random tilt of a block, in radians. */
  float tilt = 0.0f;
  /** 0: clean outlines. Higher: irregular, chunky outlines. */
  float stone_shape = 0.0f;
  /** Rounds the tops of the blocks. */
  float dome = 0.0f;
  /** Every this many rows the bricks stand upright. 0 for none. Only row patterns use it. */
  int soldier_rows = 0;
  /** A row of blocks along the outline of each face. Only convex faces get it. */
  bool border = false;
  /** Blocks at the edge of a face stick out whole instead of being cut. */
  bool toothed = false;
  int seed = 0;
  /** Center of Fan and Radial, in mesh space. The middle of the mesh when unset. */
  std::optional<float3> center;
  /** Custom pattern: block outlines and the size of the repeat. */
  Vector<Vector<float2>> tile;
  float2 tile_size = float2(1.0f);
  /** Per face, which faces get blocks. Empty for all. */
  Span<bool> selection;
  /** Material indices of the blocks and the mortar, or -1 to keep the face's own. */
  int block_material = -1;
  int mortar_material = -1;
  /** Optional point attribute with the block of each point (-1 for the mortar), and face
   * attribute that is true on the mortar. */
  std::optional<std::string> block_index_id;
  std::optional<std::string> mortar_id;
};

/**
 * Cover the selected faces with blocks laid out in a brick, stone or paving pattern. Every block
 * is a closed mesh of its own.
 * \return A new mesh, or null if no face got blocks.
 */
Mesh *mesh_masonry(const Mesh &mesh, const MeshMasonryParams &params);

}  // namespace blender::geometry
