/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

/** \file
 * \ingroup geo
 *
 * Curve operations in the style of the 3ds Max spline modifiers: Normalize Spline, Relax,
 * Chamfer and Spline Morph. Used by the modifiers of the same names and by nodes.
 */

#include "BKE_curves.hh"

namespace blender::geometry {

enum class NormalizeCurvesMode {
  /** Points spaced by a fixed length. */
  Length = 0,
  /** A fixed number of points on each curve. */
  Count = 1,
};

bke::CurvesGeometry normalize_curves(const bke::CurvesGeometry &curves,
                                     NormalizeCurvesMode mode,
                                     float length,
                                     int count);

/** Smooth the positions of all curves. */
void relax_curves(bke::CurvesGeometry &curves,
                  int iterations,
                  float factor,
                  bool keep_ends,
                  bool keep_shape);

/** Cut the corners of poly curves. Zero \a segments makes a straight cut. */
bke::CurvesGeometry chamfer_curves(const bke::CurvesGeometry &curves,
                                   float radius,
                                   int segments,
                                   bool limit_radius);

/**
 * Move the points of each curve toward the matching curve of \a target, sampled at the same
 * fractions of its length. Curves without a match are left as they are.
 */
void morph_curves(bke::CurvesGeometry &curves, const bke::CurvesGeometry &target, float factor);

}  // namespace blender::geometry
