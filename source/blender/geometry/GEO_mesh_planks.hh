/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

/** \file
 * \ingroup geo
 *
 * Planks: floors and walls of boards on the faces of a mesh, and crates and barrels made of
 * boards.
 */

#include <optional>
#include <string>

#include "BLI_math_vector_types.hh"
#include "BLI_span.hh"

namespace blender {
struct Mesh;
}

namespace blender::geometry {

/** The values of these enums are stored in files. */
enum class PlanksType {
  Floor = 0,
  Wall = 1,
  Crate = 2,
  Barrel = 3,
};

enum class PlanksPattern {
  Straight = 0,
  Herringbone = 1,
  Chevron = 2,
  Basket = 3,
};

enum class PlanksJoints {
  Random = 0,
  Half = 1,
  Third = 2,
};

enum class PlanksWallStyle {
  Horizontal = 0,
  Vertical = 1,
  /** Horizontal boards, each lying over the top of the one below. */
  Lap = 2,
  /** Vertical boards with thin strips over the joints. */
  BoardAndBatten = 3,
};

enum class PlanksBrace {
  None = 0,
  Diagonal = 1,
  /** Boards along the top and bottom with a diagonal between them. */
  Z = 2,
};

enum class PlanksLid {
  Closed = 0,
  Open = 1,
  None = 2,
};

enum class PlanksBarrelEnds {
  Both = 0,
  Bottom = 1,
  None = 2,
};

/** Values of the part attribute. */
enum class PlanksPart {
  Plank = 0,
  /** Joists, studs, frames, braces and posts. */
  Frame = 1,
  /** Barrel hoops. */
  Metal = 2,
};

struct MeshPlanksParams {
  PlanksType type = PlanksType::Floor;

  /* All types. */
  float width = 0.12f;
  float thickness = 0.02f;
  float gap = 0.004f;

  /* Floor and wall. */
  PlanksPattern pattern = PlanksPattern::Straight;
  PlanksWallStyle wall_style = PlanksWallStyle::Horizontal;
  float length = 1.2f;
  /** 0 to 1. */
  float random_length = 0.3f;
  /** Shortest piece at the ends of a row. */
  float min_length = 0.2f;
  PlanksJoints joints = PlanksJoints::Random;
  /** Turn of the rows on the faces, in radians. */
  float angle = 0.0f;
  /** A ring of boards along the outline. Floor only, convex outlines only. */
  bool border = false;
  /** Joists under floors, studs behind walls. */
  bool supports = false;
  float support_spacing = 0.4f;
  float support_width = 0.05f;
  float support_depth = 0.15f;
  /** Lap: how much each board covers the one below. */
  float lap = 0.03f;
  /** Board and batten: width of the strips. */
  float batten_width = 0.05f;
  /** Keep the faces under the boards. */
  bool keep_surface = true;
  /** Per face, which faces get boards. Empty for all. */
  Span<bool> selection;

  /* Crate. Its bottom is at 0. */
  float3 crate_size = float3(0.8f, 0.6f, 0.6f);
  bool slatted = false;
  float slat_gap = 0.03f;
  bool frame = true;
  PlanksBrace brace = PlanksBrace::Diagonal;
  bool corner_posts = false;
  float post_width = 0.06f;
  PlanksLid lid = PlanksLid::Closed;
  float lid_angle = 1.2f;

  /* Barrel. Its bottom is at 0. */
  float barrel_height = 0.9f;
  /** Radius at the ends. */
  float barrel_radius = 0.28f;
  /** How much wider the middle is. */
  float bulge = 0.06f;
  int staves = 20;
  /** How far the staves reach past the top and bottom boards. */
  float chime = 0.03f;
  PlanksBarrelEnds barrel_ends = PlanksBarrelEnds::Both;
  int hoops = 4;
  float hoop_width = 0.04f;
  float hoop_thickness = 0.006f;
  /** 0 to 0.5: how far the outer hoops are from the ends, as a share of the height. */
  float hoop_offset = 0.12f;

  /* Variation. */
  float random_height = 0.0f;
  float tilt = 0.0f;
  float cup = 0.0f;
  float bow = 0.0f;
  /** 0 to 1. */
  float missing = 0.0f;
  int seed = 0;

  /** Boards pick a material at random from this many slots, starting at #material. -1 keeps the
   * material of the face under the board (floor and wall), or uses the first slot. */
  int material = -1;
  int material_count = 1;
  /** Hoops, or -1 to use #material. */
  int metal_material = -1;

  /** Optional point attributes with the board of each point and a random value per board, and a
   * face attribute with the #PlanksPart. Faces and points kept from the input get -1, 0 and -1. */
  std::optional<std::string> board_index_id;
  std::optional<std::string> random_id;
  std::optional<std::string> part_id;
};

/**
 * \param mesh: The faces to cover for floors and walls. Not used by crates and barrels.
 * \param r_message: Gets a warning when something was asked for that could not be made.
 * \return A new mesh, or null to keep the input.
 */
Mesh *mesh_planks(const Mesh *mesh, const MeshPlanksParams &params, std::string &r_message);

}  // namespace blender::geometry
