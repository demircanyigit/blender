/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "BLI_virtual_array.hh"

#include "BKE_attribute_filter.hh"

struct Mesh;

namespace blender::geometry {

enum class MeshDecimateMode {
  /** Keep a fraction of the selected vertices. */
  Ratio = 0,
  /** Aim for a number of triangles in the result. */
  FaceCount = 1,
};

struct MeshDecimateParams {
  MeshDecimateMode mode = MeshDecimateMode::Ratio;
  /** Fraction of the selected vertices to keep, in [0, 1]. */
  float ratio = 0.5f;
  /** Desired number of triangles in the result. */
  int face_count = 1000;
  /** Largest allowed distance to the original surface. Zero disables the limit. */
  float max_error = 0.0f;
  /** Largest allowed change of the face normals, in radians. Zero disables the limit. */
  float max_normal_change = 0.0f;
  /** Triangles may not get more elongated than this, unless they improve. Zero disables it. */
  float max_aspect_ratio = 10.0f;
  /** Keep the shape of open boundaries. */
  bool preserve_boundary = true;
  /** Optional edge-domain selection of edges to keep. */
  VArray<bool> hard_edges;
  /** Optional point-domain selection of the vertices that may be removed. */
  VArray<bool> selection;
};

/**
 * Reduce the number of triangles of a mesh by repeatedly removing the vertex whose removal changes
 * the surface the least. Non-triangle faces are triangulated first. The limits can stop the
 * operation before the target is reached.
 *
 * Point, face and corner attributes that pass \a attribute_filter are resampled onto the result by
 * nearest-surface interpolation. Edge-domain attributes are dropped.
 *
 * \param r_detached_tris: Optionally returns how many non-manifold triangles had to be detached
 *   from their neighbors, which leaves seams in the result.
 * \return A newly allocated, main-database-free #Mesh, or null if the mesh could not be decimated.
 *   On null the caller should keep the original input unchanged.
 */
Mesh *decimate_mesh(const Mesh &mesh,
                    const MeshDecimateParams &params,
                    const bke::AttributeFilter &attribute_filter =
                        bke::AttributeFilter::default_filter(),
                    int *r_detached_tris = nullptr);

}  // namespace blender::geometry
