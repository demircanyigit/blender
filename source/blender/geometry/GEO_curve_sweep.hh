/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

/** \file
 * \ingroup geo
 *
 * Sweep a shape along curves. Shared by the Sweep node and the Sweep modifier.
 */

#include <optional>
#include <string>

#include "BLI_generic_virtual_array.hh"

#include "BKE_curves.hh"

namespace blender {
struct Mesh;
namespace bke {
struct AttributeFilter;
}
}  // namespace blender

namespace blender::geometry {

enum class SweepShape : int8_t {
  RoundTube = 0,
  SquareTube = 1,
  Ribbon = 2,
  /** The shape comes from other curves; #sweep_shape_profile doesn't make it. */
  CrossSection = 3,
};

/** A single curve to sweep, for the built-in shapes. */
bke::CurvesGeometry sweep_shape_profile(SweepShape shape, int columns, float radius, float width);

/** Rotate the shape around the curve: the sweep gets its rotation from the tilt of the curve. */
void sweep_add_roll_and_twist(bke::CurvesGeometry &curves,
                              const VArray<float> &roll,
                              const VArray<float> &twist);

struct SweepOutputs {
  /** Corner attribute for a UV map: X goes around the shape and Y along the curve. */
  std::optional<std::string> uv_map;
  /** Face attribute that is true for the faces closing the ends. */
  std::optional<std::string> caps;
};

/** Sweep \a profile along \a main. Returns null when there is nothing to sweep. */
Mesh *curve_sweep(const bke::CurvesGeometry &main,
                  const bke::CurvesGeometry &profile,
                  const VArray<float> &scales,
                  bool fill_caps,
                  bool length_based_uvs,
                  const SweepOutputs &outputs,
                  const bke::AttributeFilter &attribute_filter);

}  // namespace blender::geometry
