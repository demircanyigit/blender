/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

/** \file
 * \ingroup geo
 *
 * Carve: displace a mesh with up to two grayscale layers (textures, patterns or attributes),
 * combined and masked by noise, weights, curvature and facing.
 */

#include <optional>
#include <string>

#include "BLI_math_vector_types.hh"
#include "BLI_span.hh"

struct Mesh;

namespace blender::geometry {

enum class CarvePattern : int8_t {
  Noise = 0,
  Voronoi = 1,
  RandomStone = 2,
  Bricks = 3,
  Waves = 4,
  CoursedBlocks = 5,
  Slate = 6,
  DryStone = 7,
  Banded = 8,
  Fieldstone = 9,
  Rubble = 10,
  Cobblestone = 11,
  Ashlar = 12,
  FittedRubble = 13,
  RoughCourses = 14,
  SquaredStone = 15,
  MixedRubble = 16,
  WornStone = 17,
  ThinCourses = 18,
  PackedRound = 19,
  PackedAngular = 20,
  PackedBlocks = 21,
  PackedSlabs = 22,
  PackedWedges = 23,
  PackedMixed = 24,
  DryWall = 25,
};

enum class CarveBlend : int8_t {
  Mix = 0,
  Add = 1,
  Subtract = 2,
  Multiply = 3,
  Lighter = 4,
  Darker = 5,
  Difference = 6,
};

enum class CarveCurvature : int8_t {
  Off = 0,
  Convex = 1,
  Concave = 2,
  Both = 3,
};

enum class CarveDirection : int8_t {
  Normal = 0,
  X = 1,
  Y = 2,
  Z = 3,
  Custom = 4,
};

struct CarvePatternParams {
  CarvePattern pattern = CarvePattern::RandomStone;
  float scale = 5.0f;
  float3 offset = float3(0.0f);
  float rotation = 0.0f;
  float detail = 2.0f;
  float roughness = 0.5f;
  float distortion = 0.0f;
  float stone_length = 0.8f;
  float row_height = 0.25f;
  float mortar = 0.02f;
  float roundness = 0.5f;
  float variation = 0.5f;
  int seed = 0;
};

struct CarveLayer {
  bool enabled = false;
  /** Values per point from a texture or attribute. When empty, the pattern is used. */
  Span<float> values;
  /** Coordinates per point for the pattern. */
  Span<float3> coords;
  /** Use X and Y of the coordinates for flat patterns (UV). Otherwise the plane is picked by the
   * normal, so walls facing any way get the pattern upright. */
  bool flat = false;
  CarvePatternParams pattern;
  float black = 0.0f;
  float white = 1.0f;
  float contrast = 0.0f;
  bool invert = false;
  float strength = 1.0f;

  /** Material index for faces above and below #material_split, or -1 to leave them. */
  int material_high = -1;
  int material_low = -1;
  float material_split = 0.5f;
  /** Only give the materials where the mask is on. */
  bool material_masked = true;
};

struct MeshCarveParams {
  CarveLayer a;
  CarveLayer b;
  CarveBlend blend = CarveBlend::Mix;
  float blend_factor = 1.0f;

  /** Weight per point (vertex group, selection, attribute), multiplied into the mask. */
  Span<float> weights;

  bool noise_mask = false;
  float mask_scale = 2.0f;
  float mask_detail = 2.0f;
  float mask_threshold = 0.5f;
  float mask_softness = 0.1f;
  bool mask_invert = false;
  int seed = 0;

  CarveCurvature curvature = CarveCurvature::Off;
  float curvature_threshold = 0.1f;
  float curvature_softness = 0.1f;
  bool curvature_invert = false;

  /* Carve only surfaces facing these ways. Several can be on together. None means no mask. */
  bool facing_top = false;
  bool facing_sides = false;
  bool facing_bottom = false;
  float facing_angle = 0.7853982f;
  float facing_softness = 0.1745329f;
  bool facing_invert = false;

  CarveDirection direction = CarveDirection::Normal;
  float3 custom_direction = float3(0.0f, 0.0f, 1.0f);
  float strength = 0.1f;
  float midlevel = 0.5f;
  float max_depth = 0.0f;
  int smooth = 0;

  /** Material index for faces where the mask is above the threshold, or -1. */
  int mask_material = -1;
  float mask_material_threshold = 0.5f;

  std::optional<std::string> height_id;
  std::optional<std::string> mask_id;
  std::optional<std::string> curvature_id;
};

/** A pattern's grayscale value at a point, from 0 to 1. */
float carve_pattern_value(const CarvePatternParams &params,
                          const float3 &coord,
                          const float3 &normal,
                          bool flat);

/** Moves the points of the mesh. Returns false if nothing was done. */
bool mesh_carve(Mesh &mesh, const MeshCarveParams &params);

}  // namespace blender::geometry
