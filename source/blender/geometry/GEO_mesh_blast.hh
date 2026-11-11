/* SPDX-FileCopyrightText: 2026 Blender Authors
 * SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <string>
namespace blender {
struct Mesh;
}
namespace blender::geometry {
enum class BlastMetric { FaceCount = 0, Area = 1, Volume = 2, BoundsSize = 3 };
enum class BlastFilter { Threshold = 0, Largest = 1, ThresholdAndLargest = 2 };
struct MeshBlastParams {
  BlastMetric metric = BlastMetric::Area;
  BlastFilter filter = BlastFilter::Threshold;
  double threshold = 0.01;
  int keep_count = 1;
  bool invert = false;
};
/** Filter entire connected components, including loose geometry. A null return bypasses the
 * modifier; an allocated empty mesh represents removal of all eligible geometry. */
Mesh *mesh_blast(const Mesh &mesh, const MeshBlastParams &params, std::string &message);
}  // namespace blender::geometry
