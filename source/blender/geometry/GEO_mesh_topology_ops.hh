/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

/** \file
 * \ingroup geo
 *
 * Edit-mode mesh tools as non-destructive operations: Inset, Panelize, Poke, Beautify Faces,
 * Rotate Edges, Symmetrize and Mark by Angle. Shared by the modifiers and the nodes.
 */

#include <cstdint>
#include <string>

#include "BLI_index_mask_fwd.hh"
#include "BLI_math_vector_types.hh"
#include "BLI_span.hh"

namespace blender {
struct Mesh;
}

namespace blender::geometry {

/** Names of boolean face attributes to write. Empty names are skipped. */
struct TopologyOutputs {
  /** The faces the operation started from (or their new versions). */
  std::string top;
  /** The faces the operation made. */
  std::string side;
};

struct InsetParams {
  /** Each face on its own, or all selected faces as one region. */
  bool individual = false;
  float thickness = 0.1f;
  float depth = 0.0f;
  bool use_even_offset = true;
  bool use_relative_offset = false;
  bool use_boundary = true;
  bool use_edge_rail = false;
  bool use_outset = false;
  /** Added to the material index of the new faces. */
  int side_material_offset = 0;
};

Mesh *mesh_inset(const Mesh &mesh,
                 const IndexMask &selection,
                 const InsetParams &params,
                 const TopologyOutputs &outputs);

struct PanelizeParams {
  /** Width of the gap between panels. */
  float gap = 0.02f;
  /** Width of the chamfer on the edge of each panel. */
  float bevel = 0.01f;
  /** How far the panels stand out (or sink, when negative). */
  float height = 0.01f;
  bool use_even_offset = true;
  bool use_relative_offset = false;
  /** Added to the material index of the gap faces. */
  int gap_material_offset = 0;
};

Mesh *mesh_panelize(const Mesh &mesh,
                    const IndexMask &selection,
                    const PanelizeParams &params,
                    const TopologyOutputs &outputs);

enum class PokeCenter : int8_t {
  MeanWeighted = 0,
  Mean = 1,
  Bounds = 2,
};

struct PokeParams {
  PokeCenter center = PokeCenter::MeanWeighted;
  float offset = 0.0f;
  bool use_relative_offset = false;
};

Mesh *mesh_poke(const Mesh &mesh,
                const IndexMask &selection,
                const PokeParams &params,
                const TopologyOutputs &outputs);

enum class BeautifyMethod : int8_t {
  Area = 0,
  Angle = 1,
};

/** Triangulates the selected faces first when \a triangulate is set; otherwise only triangles
 * change. */
Mesh *mesh_beautify(const Mesh &mesh,
                    const IndexMask &selection,
                    BeautifyMethod method,
                    bool triangulate);

/** Rotate the edges between two selected faces. */
Mesh *mesh_rotate_edges(const Mesh &mesh,
                        const IndexMask &selection,
                        bool counter_clockwise,
                        int steps);

enum class SymmetrizeDirection : int8_t {
  NegativeX = 0,
  NegativeY = 1,
  NegativeZ = 2,
  PositiveX = 3,
  PositiveY = 4,
  PositiveZ = 5,
};

Mesh *mesh_symmetrize(const Mesh &mesh, SymmetrizeDirection direction, float merge_distance);

enum class MarkAngleSide : int8_t {
  Both = 0,
  Convex = 1,
  Concave = 2,
};

struct MarkByAngleParams {
  float angle = 0.523599f;
  MarkAngleSide side = MarkAngleSide::Both;
  /** Also mark edges with only one face. */
  bool use_boundary = false;
  /** Clear the marks of edges below the angle, instead of keeping them. */
  bool clear_others = true;

  bool mark_sharp = true;
  bool mark_seam = false;
  bool mark_crease = false;
  bool mark_bevel_weight = false;
  /** Value for crease and bevel weight. */
  float value = 1.0f;
  /** Set the faces around marked edges to flat shading. */
  bool flat_faces = false;
};

/** Edges that the angle test selects, for the node. */
void mesh_edges_by_angle(const Mesh &mesh,
                         const MarkByAngleParams &params,
                         MutableSpan<bool> r_selection);

/** Returns null when nothing is marked. */
Mesh *mesh_mark_by_angle(const Mesh &mesh,
                         const IndexMask &edge_mask,
                         const MarkByAngleParams &params);

}  // namespace blender::geometry
