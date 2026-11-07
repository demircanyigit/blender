/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include <optional>
#include <string>

#include "BLI_math_vector_types.hh"

namespace blender {
struct Mesh;
}

namespace blender::geometry {

/** How the material breaks. The values are stored in files. */
enum class ShatterMaterial {
  /** Chunky pieces of mixed sizes, with small chips. */
  Stone = 0,
  /** Shards between cracks running out from the impact and rings around it. */
  Glass = 1,
  /** Long splinters along the grain. */
  Wood = 2,
  /** A few big pieces. */
  Ceramic = 3,
  /** Cracks from the impact like glass, with chunks like stone. */
  Ice = 4,
};

struct MeshShatterParams {
  ShatterMaterial material = ShatterMaterial::Stone;
  int pieces = 20;
  int seed = 0;
  /** Where it was hit, in mesh space. The middle of the bounding box when unset. */
  std::optional<float3> impact;
  /** 0 to 1: how much smaller the pieces get near the impact. */
  float focus = 0.5f;
  /** Axis of the wood grain: 0, 1 or 2, or -1 for the longest side of the bounding box. */
  int grain_axis = -1;
  /** How many times longer than wide the wood splinters are. */
  float grain = 4.0f;
  /** How jagged the broken faces are, relative to the size of the pieces. */
  float roughness = 0.0f;
  /** Cuts across each edge of the broken faces, so roughness has points to move. */
  int detail = 2;
  /** Distance between the inside faces of neighboring pieces. */
  float cut_gap = 0.0f;
  /** Optional name of a point-domain integer attribute with the piece of each point. */
  std::optional<std::string> piece_index_id;
  /** Optional name of a face-domain boolean attribute, true for the broken faces. */
  std::optional<std::string> interior_id;
};

/**
 * Break a closed mesh into solid pieces in the way \a params.material breaks.
 * \return A new mesh, or null with \a r_error set.
 */
Mesh *mesh_shatter(const Mesh &mesh, const MeshShatterParams &params, const char **r_error);

}  // namespace blender::geometry
