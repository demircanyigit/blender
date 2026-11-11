/* SPDX-FileCopyrightText: 2026 Blender Authors
 * SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "BLI_math_vector_types.hh"
#include <array>
#include <string>
namespace blender {
struct Mesh;
}
namespace blender::geometry {
struct MeshDivideParams {
  float3 size = float3(0.5f);
  float3 offset = float3(0);
  /** XYZ Euler angles in radians; grid axes rotate about offset. */
  float3 rotation = float3(0);
  std::array<bool, 3> axes = {true, true, true};
  float tolerance = 1e-6f;
};
/** Split the connected surface with a transformed regular grid. No side is deleted, filled,
 * or detached. Returns a new mesh, or null to retain the input on bypass or failure. */
Mesh *mesh_divide(const Mesh &mesh, const MeshDivideParams &params, std::string &message);
}  // namespace blender::geometry
