/* SPDX-FileCopyrightText: 2026 Blender Authors
 * SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "BLI_string_ref.hh"
#include "BLI_virtual_array.hh"
#include <string>

struct Mesh;

namespace blender::geometry {

/** Values match DecimateModifierBackend in DNA_modifier_types.h. */
enum class DecimateBackend { PMP = 1, Meshoptimizer = 2, Aggressive = 3, CGAL = 4, Wild = 5 };

struct DecimateBackendParams {
  DecimateBackend backend = DecimateBackend::PMP;
  int target_faces = 1000;
  float error_limit = 0;
  float feature_angle = 0.785398163f;
  float uv_weight = 1;
  float normal_weight = 0.2f;
  float virtual_distance = 0;
  float max_normal_change = 0;
  float max_aspect_ratio = 10;
  bool preserve_boundary = true;
  bool preserve_uv = true;
  bool preserve_materials = true;
  bool preserve_sharp = true;
  bool merge_coincident = true;
  StringRef uv_map;
  VArray<bool> selection;
};

/** Triangle-based alternatives to BMesh decimation. On failure return null and leave input intact.
 * Attributes are transferred from the original surface, with corner-aware sampling at UV seams. */
Mesh *decimate_with_backend(const Mesh &mesh,
                            const DecimateBackendParams &params,
                            std::string &message);

}  // namespace blender::geometry
