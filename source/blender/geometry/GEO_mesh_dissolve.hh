/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "BLI_span.hh"

namespace blender {
struct Mesh;
namespace bke {
enum class AttrDomain : int8_t;
}
}  // namespace blender

namespace blender::geometry {

/** What gets removed. The values are stored in files. */
enum class DissolveMode {
  /** Remove the selected points, and merge the faces around them. */
  Vertices = 0,
  /** Remove the selected edges, and merge the faces on both sides. */
  Edges = 1,
  /** Merge the selected faces that touch each other into one. */
  Faces = 2,
  /** Remove the selected edges and points where the surface stays nearly flat. */
  Limited = 3,
};

struct MeshDissolveParams {
  DissolveMode mode = DissolveMode::Edges;
  /** Also remove points left between only two edges. Not used in Vertices mode. */
  bool remove_inline_points = true;
  /** Split off the corners of faces to keep the surrounding shape (Vertices and Edges). */
  bool face_split = false;
  /** Split off corners on open boundaries instead of merging the faces (Vertices). */
  bool tear_boundary = false;
  /** Limited: only dissolve where the surface bends less than this. */
  float angle = 0.0872665f;
  /** Limited: also dissolve points on open boundaries, whatever their angle. */
  bool all_boundaries = false;
  /** Limited: edges that are kept. */
  bool keep_materials = false;
  bool keep_seams = false;
  bool keep_sharp = false;
  bool keep_uvs = false;
  /** Optional face attribute that is true on the faces made by merging. */
  std::optional<std::string> merged_faces_id;
};

/** The domain of the selection for a mode: points, edges or faces. */
bke::AttrDomain dissolve_selection_domain(DissolveMode mode);

/**
 * Remove points, edges or faces and merge the faces around them, without leaving holes.
 * \param selection: Per element of #dissolve_selection_domain.
 * \return The new mesh, or null if nothing is selected.
 */
Mesh *mesh_dissolve(const Mesh &mesh, Span<bool> selection, const MeshDissolveParams &params);

}  // namespace blender::geometry
