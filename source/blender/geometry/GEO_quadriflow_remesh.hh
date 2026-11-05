/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

/** \file
 * \ingroup geo
 *
 * Rebuilding a mesh out of quads with QuadriFlow, shared by the QuadriFlow Remesh node and
 * modifier.
 */

namespace blender {
struct Mesh;
}

namespace blender::geometry {

enum class QuadriflowTarget {
  /** Aim for #QuadriflowRemeshParams::face_count quads. */
  FaceCount = 0,
  /** Aim for the input's face count times #QuadriflowRemeshParams::ratio. */
  Ratio = 1,
  /** Aim for quads with edges of #QuadriflowRemeshParams::edge_length. */
  EdgeLength = 2,
};

struct QuadriflowRemeshParams {
  QuadriflowTarget target = QuadriflowTarget::FaceCount;
  int face_count = 1000;
  float ratio = 1.0f;
  float edge_length = 0.1f;
  int seed = 0;
  bool preserve_sharp = false;
  bool preserve_boundary = false;
};

enum class QuadriflowRemeshResult {
  Success,
  /** Nothing to do (no faces, or a zero edge length), the input should be kept. */
  Skipped,
  /** The mesh isn't a consistent manifold, which QuadriFlow can crash on. */
  Unsupported,
  /** QuadriFlow ran but gave no result. */
  Failed,
  /** Blender was built without QuadriFlow. */
  Disabled,
};

/**
 * Rebuild #mesh out of quads. The input is read-only. Attributes are reprojected onto the
 * result. Returns null unless \a r_result is #QuadriflowRemeshResult::Success.
 */
Mesh *quadriflow_remesh(const Mesh &mesh,
                        const QuadriflowRemeshParams &params,
                        QuadriflowRemeshResult &r_result);

}  // namespace blender::geometry
