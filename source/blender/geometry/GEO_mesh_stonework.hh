/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

/** \file
 * \ingroup geo
 *
 * Stonework: fills a mesh with whole stones, stacked one at a time from the bottom up.
 */

#include <optional>
#include <string>

#include "BLI_span.hh"

struct Mesh;

namespace blender::geometry {

enum class StoneworkFill : int8_t {
  /** The inside of the mesh. */
  Volume = 0,
  /** The bounding box of the mesh. */
  Box = 1,
  /** One layer of stones just under the surface. */
  Surface = 2,
};

enum class StoneworkShape : int8_t {
  Round = 0,
  Angular = 1,
  Blocks = 2,
  Slabs = 3,
  Wedges = 4,
  Mixed = 5,
  DryWall = 6,
};

/** How the mortar mesh is made dense. */
enum class StoneworkRemesh : int8_t {
  Isotropic = 0,
  Voxel = 1,
  Divide = 2,
  None = 3,
};

enum class StoneworkColorMode : int8_t {
  /** Random hues. */
  Hue = 0,
  /** Random shades of gray. */
  Gray = 1,
};

struct StoneworkParams {
  StoneworkFill fill = StoneworkFill::Volume;
  StoneworkShape shape = StoneworkShape::DryWall;
  /** Your own stones. Each loose part is one stone. When empty, built-in stones are made. */
  Span<const Mesh *> custom_stones;
  float stone_size = 0.2f;
  float size_variation = 0.35f;
  /** 0 for chunky stones, 1 for flat ones. */
  float flatness = 0.5f;
  /** Space between stones. */
  float gap = 0.005f;
  /** Fill the holes left at the end with smaller stones. */
  bool fillers = true;
  /** How far stones may stick out past the surface. */
  float poke_out = 0.0f;
  /** Bumps on the stones, as a part of their size. */
  float roughness = 0.3f;
  /** How many faces each built-in stone has, 1 to 4. */
  int detail = 2;
  int max_stones = 2000;
  int seed = 0;
  /** Grow the stones into the space around them, 0 to 1. */
  float close_gaps = 1.0f;
  /** A random size per finished stone, around its center. */
  float scale_min = 1.0f;
  float scale_max = 1.0f;

  /** A mortar mesh: the filled shape, pulled in by #mortar_depth. */
  bool mortar = false;
  float mortar_depth = 0.05f;
  StoneworkRemesh mortar_remesh = StoneworkRemesh::Isotropic;
  /** Edge length, voxel size or grid size of the mortar remesh. */
  float mortar_resolution = 0.05f;
  /** Join the stones and the mortar into one closed surface. */
  bool merge = false;

  int material_index = 0;
  int mortar_material_index = 0;

  std::optional<std::string> stone_id_id;
  std::optional<std::string> random_id;
  /** A random color per stone, on the face corners. The mortar is gray. */
  std::optional<std::string> color_id;
  StoneworkColorMode color_mode = StoneworkColorMode::Hue;
  float hue_min = 0.0f, hue_max = 1.0f;
  float saturation_min = 0.45f, saturation_max = 0.85f;
  float value_min = 0.65f, value_max = 0.95f;
  /** True on the mortar's faces. */
  std::optional<std::string> mortar_id;
};

/**
 * Returns the stones as one mesh, or null if none fit. The number of stones is written to
 * #r_stones_num, and anything that went wrong to #r_message.
 */
Mesh *mesh_stonework(const Mesh &mesh,
                     const StoneworkParams &params,
                     int *r_stones_num,
                     std::string *r_message = nullptr);

}  // namespace blender::geometry
