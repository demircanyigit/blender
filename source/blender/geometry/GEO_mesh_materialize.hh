/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

/** \file
 * \ingroup geo
 *
 * Materialize: give faces materials by rules (curvature, direction, height, slope, noise, random,
 * attributes, proximity, occlusion, thickness, textures), applied in order.
 */

#include <optional>
#include <string>

#include "BLI_array.hh"
#include "BLI_math_matrix_types.hh"
#include "BLI_span.hh"

struct Mesh;

namespace blender::geometry {

enum class MaterializeType : int8_t {
  Curvature = 0,
  Direction = 1,
  Height = 2,
  Slope = 3,
  Noise = 4,
  Random = 5,
  Attribute = 6,
  Proximity = 7,
  Occlusion = 8,
  Thickness = 9,
  Texture = 10,
};

enum class MaterializeCurvature : int8_t {
  Convex = 0,
  Concave = 1,
  Both = 2,
};

enum class MaterializeDirection : int8_t {
  Up = 0,
  Down = 1,
  Sides = 2,
  PosX = 3,
  NegX = 4,
  PosY = 5,
  NegY = 6,
  Custom = 7,
};

enum class MaterializeRandom : int8_t {
  Face = 0,
  Island = 1,
  Object = 2,
};

struct MaterializeRule {
  MaterializeType type = MaterializeType::Direction;
  /** Material index the faces get. */
  int material = 0;
  /** Only faces that had this material index, or -1 for all. */
  int only_on = -1;
  bool invert = false;
  /** Rings of faces added (or removed when negative). */
  int grow = 0;
  /** Patches of fewer faces are dropped. */
  int min_island = 0;
  float breakup = 0.0f;
  float breakup_scale = 5.0f;
  int seed = 0;
  /** Use world space for directions, heights and noise. */
  bool global = false;

  MaterializeCurvature curvature = MaterializeCurvature::Convex;
  float curvature_angle = 0.5235988f;

  MaterializeDirection direction = MaterializeDirection::Up;
  float3 custom_direction = float3(0.0f, 0.0f, 1.0f);
  float direction_angle = 0.7853982f;

  /** 0, 1 or 2 for X, Y or Z. */
  int axis = 2;
  float height_from = 0.0f;
  float height_to = 0.5f;
  /** Heights from 0 at the bottom of the mesh to 1 at the top. */
  bool relative = true;

  float slope_min = 0.0f;
  float slope_max = 0.5235988f;

  float noise_scale = 2.0f;
  float noise_detail = 2.0f;
  float noise_threshold = 0.5f;

  MaterializeRandom random = MaterializeRandom::Face;
  float chance = 0.3f;

  /** Attribute and Texture: a value per face. */
  Span<float> values;
  float threshold = 0.5f;

  /** Proximity: the distance of each face to the target. */
  Span<float> distances;
  float distance = 0.5f;

  int samples = 16;
  float occlusion_distance = 0.5f;
  float occlusion = 0.5f;

  float thickness = 0.1f;
};

struct MeshMaterializeParams {
  Span<MaterializeRule> rules;
  float4x4 object_to_world = float4x4::identity();
  /** For Random per Object. */
  int object_seed = 0;
  std::optional<std::string> rule_id;
};

/** Sets the material index of the faces picked by the rules. Later rules win. */
void mesh_materialize(Mesh &mesh, const MeshMaterializeParams &params);

/** Distance from the center of each face to the surface of the target. */
Array<float> materialize_face_distances(const Mesh &mesh,
                                        const Mesh &target,
                                        const float4x4 &target_to_mesh);

}  // namespace blender::geometry
