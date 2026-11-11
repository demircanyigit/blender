/* SPDX-FileCopyrightText: 2026 Blender Authors
 * SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "BLI_math_vector_types.hh"
#include "GEO_decimate_backends.hh"
#include <string>
#include <vector>

namespace blender::geometry::decimation {

struct TriangleInput {
  std::vector<float3> positions;
  std::vector<int3> triangles;
  std::vector<unsigned char> locked;
  /** UV and normal components, five floats per vertex. */
  std::vector<float> attributes;
};

struct TriangleResult {
  std::vector<float3> positions;
  std::vector<int3> triangles;
  std::string message;
  bool success = false;
};

TriangleResult simplify_cgal(const TriangleInput &input, const DecimateBackendParams &params);
TriangleResult simplify_wild(const TriangleInput &input, const DecimateBackendParams &params);

}  // namespace blender::geometry::decimation
