/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

/** \file
 * \ingroup geo
 *
 * Roof: build a roof on the outlines of faces, or dress faces modeled as a roof with a slab,
 * overhangs, tiles or other covering, ridge caps, valleys, fascia boards and gutters.
 */

#include <optional>
#include <string>

#include "BLI_span.hh"

namespace blender {
struct Mesh;
}

namespace blender::geometry {

/** The values of these enums are stored in files. */
enum class RoofMode {
  /** Each group of faces facing up is the outline of a building, and gets a roof. */
  Build = 0,
  /** The faces are the roof, and get dressed. */
  Cover = 1,
};

enum class RoofShape {
  Gable = 0,
  Hip = 1,
  Shed = 2,
  Flat = 3,
  Pyramid = 4,
  Gambrel = 5,
  Mansard = 6,
};

enum class RoofCovering {
  None = 0,
  Tiles = 1,
  Shingles = 2,
  Metal = 3,
  Planks = 4,
};

/** Values of the part attribute. */
enum class RoofPart {
  Slope = 0,
  Underside = 1,
  Fascia = 2,
  Covering = 3,
  Ridge = 4,
  Valley = 5,
  Gutter = 6,
  Wall = 7,
  Parapet = 8,
};

struct MeshRoofParams {
  RoofMode mode = RoofMode::Build;

  /* Build. */
  RoofShape shape = RoofShape::Gable;
  /** Slope of the roof, or of the upper part of Gambrel and Mansard, in radians. */
  float pitch = 0.6108652f;
  /** Slope of the lower part of Gambrel and Mansard. */
  float lower_pitch = 1.2217305f;
  /** 0 to 1: how much of the half width the lower part of Gambrel and Mansard covers. */
  float break_ratio = 0.4f;
  /** Ridge along the longest side of the outline, or at #ridge_angle from X. */
  bool auto_ridge = true;
  float ridge_angle = 0.0f;
  /** Flat roofs: low walls along the outline. 0 height for none. */
  float parapet_height = 0.4f;
  float parapet_width = 0.2f;

  /* Both. */
  /** How far the roof reaches past the low edges, measured level, and past the sloped edges. */
  float eave_overhang = 0.3f;
  float rake_overhang = 0.2f;
  /** Thickness of the slab. In Cover mode only the overhangs get it. */
  float thickness = 0.12f;
  /** Boards along the outer edges. 0 height for none. */
  float fascia_height = 0.2f;
  float fascia_thickness = 0.03f;
  bool gutters = false;
  float gutter_size = 0.12f;

  RoofCovering covering = RoofCovering::Tiles;
  /** How much of each row shows, up the slope. */
  float row_spacing = 0.2f;
  /** Width of a tile or shingle, or of a metal sheet. */
  float tile_width = 0.25f;
  float covering_thickness = 0.02f;
  /** How much each row covers of the row below. */
  float overlap = 0.08f;
  float covering_gap = 0.004f;

  bool ridge_caps = true;
  float cap_width = 0.25f;
  /** Edges that fold more than this get caps or valleys. */
  float cap_angle = 0.1745329f;
  bool valleys = true;
  float valley_width = 0.3f;

  /** Cover: faces flatter than this are skipped, or get #flat_roofing. Faces steeper than 80
   * degrees are walls and are skipped. */
  float flat_angle = 0.0872665f;
  bool flat_roofing = true;

  /* Variation of the covering. */
  float random_height = 0.0f;
  float tilt = 0.0f;
  /** 0 to 1. */
  float missing = 0.0f;
  int seed = 0;

  /** Material indices, or -1: the slab keeps the material of its face (0 in Build mode), the
   * others use 0. */
  int roof_material = -1;
  int covering_material = -1;
  /** Caps, valleys, fascia and gutters. */
  int trim_material = -1;
  /** Gable walls and parapets. */
  int wall_material = -1;

  /** Per face, which faces are used. Empty for all. */
  Span<bool> selection;

  /** Optional face attribute with the #RoofPart, and point attribute with a random value per
   * piece. Faces and points of the input get -1 and 0. */
  std::optional<std::string> part_id;
  std::optional<std::string> random_id;
};

/**
 * \param r_message: Gets a warning when the faces did not work for the mode.
 * \return A new mesh with the input and the roof, or null to keep the input.
 */
Mesh *mesh_roof(const Mesh &mesh, const MeshRoofParams &params, std::string &r_message);

}  // namespace blender::geometry
