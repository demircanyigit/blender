/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

/** \file
 * \ingroup geo
 *
 * Operations on the elements of a mesh: its faces, connected pieces, UV islands, material groups
 * or face sets. Used by the Randomize Elements, Exploded View, Element Order and Store Element
 * Pivot modifiers and nodes.
 */

#include <cstdint>
#include <optional>
#include <string>

#include "BLI_math_vector_types.hh"
#include "BLI_span.hh"
#include "BLI_string_ref.hh"

namespace blender {
struct Mesh;
}

namespace blender::geometry {

enum class MeshElement : int8_t {
  Face = 0,
  Island = 1,
  UVIsland = 2,
  Material = 3,
  FaceSet = 4,
};

/**
 * Which element each face belongs to, numbered from zero in the order they are met.
 * UV islands use \a uv_map (or the active UV map when it is empty). When there is no UV map or no
 * face sets, connected pieces are used instead. Returns the number of elements.
 */
int mesh_face_elements(const Mesh &mesh,
                       MeshElement element,
                       StringRef uv_map,
                       MutableSpan<int> r_face_elements);

enum class ElementPivot : int8_t {
  /** Average of the element's points. */
  Center = 0,
  /** Center of the element's bounding box. */
  BoundsCenter = 1,
  /** The origin of the object. */
  Origin = 2,
};

enum class ElementColorMode : int8_t {
  /** A random hue with a fixed saturation and a random value in a range. */
  RandomHue = 0,
  /** A random grey between two values. */
  Grayscale = 1,
  /** Pick one of the palette colors. */
  Palette = 2,
};

struct RandomizeElementsParams {
  MeshElement element = MeshElement::Island;
  std::string uv_map;
  int seed = 0;
  /** Weight of each point (empty for all 1). An element uses the average of its points. */
  Span<float> weights;

  bool use_geometry = true;
  bool use_uvs = false;
  bool use_color = false;

  /* Geometry. */
  ElementPivot pivot = ElementPivot::Center;
  /** Largest rotation around X, Y and Z, in radians, both ways. */
  float3 rotation = float3(0.0f);
  /** Largest rotation around the element's normal. */
  float normal_rotation = 0.0f;
  float scale_min = 1.0f;
  float scale_max = 1.0f;
  /** One random scale for all axes, or one for each. */
  bool uniform_scale = true;
  /** Largest offset on X, Y and Z, both ways. */
  float3 offset = float3(0.0f);
  /** Largest offset along the element's normal, both ways. */
  float normal_offset = 0.0f;

  /* UVs, around the center of each element's UVs. */
  float uv_rotation = 0.0f;
  float uv_scale_min = 1.0f;
  float uv_scale_max = 1.0f;
  float2 uv_offset = float2(0.0f);

  /* Color, written to a face corner color attribute. */
  std::string color_attribute = "Color";
  ElementColorMode color_mode = ElementColorMode::RandomHue;
  float saturation = 0.8f;
  float value_min = 0.5f;
  float value_max = 1.0f;
  Span<float4> palette;
  /** How much the new color replaces the existing one. */
  float color_blend = 1.0f;

  /** Optional face attribute that gets a random value from 0 to 1 per element. */
  std::optional<std::string> random_id;
};

/** Returns null when nothing changes. */
Mesh *randomize_elements(const Mesh &mesh, const RandomizeElementsParams &params);

enum class ExplodeDirection : int8_t {
  /** Away from a center point. */
  FromCenter = 0,
  /** Along the normal of each element. */
  Normal = 1,
};

struct ExplodedViewParams {
  MeshElement element = MeshElement::Island;
  std::string uv_map;
  ExplodeDirection direction = ExplodeDirection::FromCenter;
  float3 center = float3(0.0f);
  /** How far the explosion has gone, from 0 to 1. */
  float factor = 1.0f;
  /** How far the elements move at a factor of 1. */
  float distance = 1.0f;
  /** How much the distance varies between elements, from 0 to 1. */
  float randomness = 0.0f;
  /** Largest tumble of each element at a factor of 1, in radians. */
  float tumble = 0.0f;
  int seed = 0;
  Span<float> weights;
};

Mesh *exploded_view(const Mesh &mesh, const ExplodedViewParams &params);

enum class ElementOrderMode : int8_t {
  Random = 0,
  /** Along X, Y or Z. */
  Axis = 1,
  /** By distance from a point. */
  Distance = 2,
};

struct ElementOrderParams {
  MeshElement element = MeshElement::Face;
  std::string uv_map;
  ElementOrderMode mode = ElementOrderMode::Random;
  int axis = 2;
  float3 point = float3(0.0f);
  bool reverse = false;
  int seed = 0;
};

/** Reorder the faces of the mesh, keeping the faces of each element together. */
Mesh *element_order(const Mesh &mesh, const ElementOrderParams &params);

struct ElementPivotParams {
  MeshElement element = MeshElement::Island;
  std::string uv_map;
  ElementPivot pivot = ElementPivot::Center;
  /** Point attributes to write. Empty names are skipped. */
  std::string center_name = "pivot";
  std::string x_axis_name = "pivot_x";
  std::string z_axis_name = "pivot_z";
  std::string index_name = "";
};

/** Write the pivot of each element to the points of the element. */
void store_element_pivot(Mesh &mesh, const ElementPivotParams &params);

}  // namespace blender::geometry
