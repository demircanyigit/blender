/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup geo
 *
 * Masonry: cover faces with blocks in a brick, stone or paving pattern.
 *
 * Every face gets a 2D frame in its plane: "u" along the rows and "v" across them. The pattern is
 * laid out in that frame with absolute coordinates (the dot products of the points with u and v),
 * so faces in the same plane with the same frame continue each other's pattern. The blocks are
 * cut to the outline of the face, and given thickness along its normal.
 *
 * Random values come from hashes of the block's place in the pattern, not from the face, so a
 * block that is split between two faces gets the same depth, tilt and outline in both.
 */

#include <algorithm>
#include <cfloat>
#include <climits>
#include <cmath>
#include <iterator>

#include "DNA_mesh_types.h"

#include "BLI_array.hh"
#include "BLI_map.hh"
#include "BLI_math_base.hh"
#include "BLI_math_vector.hh"
#include "BLI_noise.hh"
#include "BLI_vector.hh"

#include "BKE_attribute.hh"
#include "BKE_geometry_set.hh"
#include "BKE_mesh.h"
#include "BKE_mesh.hh"

#include "GEO_join_geometries.hh"
#include "GEO_mesh_copy_selection.hh"
#include "GEO_mesh_masonry.hh"

namespace blender::geometry {

namespace {

using Polygon = Vector<float2, 8>;

constexpr float PI_F = 3.14159265358979f;
/** Above this many blocks on one face, the face is left alone. */
constexpr int MAX_BLOCKS_PER_FACE = 250000;

struct Block {
  Polygon outline;
  uint32_t id;
};

struct Box2 {
  float2 min;
  float2 max;
};

/* -------------------------------------------------------------------- */
/** \name 2D Polygons
 * \{ */

float cross2(const float2 &a, const float2 &b)
{
  return a.x * b.y - a.y * b.x;
}

float signed_area(const Span<float2> poly)
{
  float area = 0.0f;
  for (const int i : poly.index_range()) {
    area += cross2(poly[i], poly[(i + 1) % poly.size()]);
  }
  return area * 0.5f;
}

float2 centroid(const Span<float2> poly)
{
  float2 sum(0.0f);
  for (const float2 &p : poly) {
    sum += p;
  }
  return sum / float(std::max<int64_t>(poly.size(), 1));
}

bool is_convex(const Span<float2> poly)
{
  for (const int i : poly.index_range()) {
    const float2 &a = poly[i];
    const float2 &b = poly[(i + 1) % poly.size()];
    const float2 &c = poly[(i + 2) % poly.size()];
    if (cross2(b - a, c - b) < -1e-9f) {
      return false;
    }
  }
  return true;
}

bool point_inside(const Span<float2> poly, const float2 &p)
{
  bool inside = false;
  for (int i = 0, j = int(poly.size()) - 1; i < poly.size(); j = i++) {
    const float2 &a = poly[i];
    const float2 &b = poly[j];
    if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) {
      inside = !inside;
    }
  }
  return inside;
}

bool segments_cross(const float2 &a, const float2 &b, const float2 &c, const float2 &d)
{
  const float d1 = cross2(b - a, c - a);
  const float d2 = cross2(b - a, d - a);
  const float d3 = cross2(d - c, a - c);
  const float d4 = cross2(d - c, b - c);
  return ((d1 > 0.0f) != (d2 > 0.0f)) && ((d3 > 0.0f) != (d4 > 0.0f)) && d1 != 0.0f &&
         d2 != 0.0f && d3 != 0.0f && d4 != 0.0f;
}

bool fully_inside(const Span<float2> block, const Span<float2> face)
{
  for (const float2 &p : block) {
    if (!point_inside(face, p)) {
      return false;
    }
  }
  for (const int i : block.index_range()) {
    const float2 &a = block[i];
    const float2 &b = block[(i + 1) % block.size()];
    for (const int j : face.index_range()) {
      if (segments_cross(a, b, face[j], face[(j + 1) % face.size()])) {
        return false;
      }
    }
  }
  return true;
}

/** The part of \a subject inside \a clipper, which must be convex and counter-clockwise. */
Polygon clip(const Span<float2> subject, const Span<float2> clipper)
{
  Polygon result(subject);
  for (const int i : clipper.index_range()) {
    if (result.is_empty()) {
      break;
    }
    const float2 &a = clipper[i];
    const float2 &b = clipper[(i + 1) % clipper.size()];
    const Polygon input = std::move(result);
    result.clear();
    for (const int j : input.index_range()) {
      const float2 &p = input[j];
      const float2 &q = input[(j + 1) % input.size()];
      const float dp = cross2(b - a, p - a);
      const float dq = cross2(b - a, q - a);
      if (dp >= 0.0f) {
        result.append(p);
      }
      if ((dp >= 0.0f) != (dq >= 0.0f)) {
        result.append(p + (q - p) * (dp / (dp - dq)));
      }
    }
  }
  return result;
}

/** Move every edge of a convex counter-clockwise polygon inward by \a distance. Empty when the
 * polygon would turn inside out. */
Polygon inset_convex(const Span<float2> poly, const float distance)
{
  if (distance <= 0.0f) {
    return Polygon(poly);
  }
  const int n = poly.size();
  Array<float2> line_point(n);
  Array<float2> line_dir(n);
  for (const int i : IndexRange(n)) {
    const float2 edge = poly[(i + 1) % n] - poly[i];
    const float length = math::length(edge);
    if (length < 1e-12f) {
      return {};
    }
    line_dir[i] = edge / length;
    line_point[i] = poly[i] + float2(-line_dir[i].y, line_dir[i].x) * distance;
  }
  Polygon result;
  for (const int i : IndexRange(n)) {
    const int prev = (i + n - 1) % n;
    const float denom = cross2(line_dir[prev], line_dir[i]);
    if (std::abs(denom) < 1e-9f) {
      result.append(line_point[i]);
      continue;
    }
    const float t = cross2(line_point[i] - line_point[prev], line_dir[i]) / denom;
    result.append(line_point[prev] + line_dir[prev] * t);
  }
  for (const int i : IndexRange(n)) {
    const float2 new_edge = result[(i + 1) % n] - result[i];
    if (math::dot(new_edge, line_dir[i]) <= 0.0f) {
      return {};
    }
  }
  return result;
}

Polygon rect(const float x0, const float y0, const float x1, const float y1)
{
  return {float2(x0, y0), float2(x1, y0), float2(x1, y1), float2(x0, y1)};
}

/** Cut the corners off, for cobbles. */
Polygon chamfer(const Span<float2> poly, const float factor)
{
  Polygon result;
  for (const int i : poly.index_range()) {
    const float2 &prev = poly[(i + poly.size() - 1) % poly.size()];
    const float2 &p = poly[i];
    const float2 &next = poly[(i + 1) % poly.size()];
    result.append(math::interpolate(p, prev, factor));
    result.append(math::interpolate(p, next, factor));
  }
  return result;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Pattern Generation
 * \{ */

struct PatternContext {
  const MeshMasonryParams &params;
  Box2 box;
  /** Center of Fan and Radial in pattern coordinates. */
  float2 center;
  uint32_t seed_hash;
  float gap;
  Vector<Block> &blocks;
  bool too_many = false;

  uint32_t id(const int kind, const int a, const int b) const
  {
    return noise::hash(uint32_t(a), uint32_t(b), uint32_t(kind) * 7919u + seed_hash);
  }

  float random(const uint32_t id, const uint32_t k) const
  {
    return noise::hash_to_float(id, k);
  }

  bool overlaps(const Span<float2> poly) const
  {
    float2 min(FLT_MAX), max(-FLT_MAX);
    for (const float2 &p : poly) {
      min = math::min(min, p);
      max = math::max(max, p);
    }
    return !(max.x < box.min.x || min.x > box.max.x || max.y < box.min.y || min.y > box.max.y);
  }

  void add(Polygon outline, const uint32_t id)
  {
    if (outline.size() < 3 || !this->overlaps(outline)) {
      return;
    }
    if (blocks.size() >= MAX_BLOCKS_PER_FACE) {
      too_many = true;
      return;
    }
    blocks.append({std::move(outline), id});
  }

  /** Add a convex counter-clockwise block, made smaller by half the mortar on every side. */
  void add_convex(const Span<float2> outline, const uint32_t id)
  {
    Polygon inset = inset_convex(outline, gap * 0.5f);
    this->add(std::move(inset), id);
  }
};

int floor_div(const float value, const float step)
{
  return int(std::floor(value / step));
}

int mod_positive(const int a, const int n)
{
  const int r = a % n;
  return r < 0 ? r + n : r;
}

struct Row {
  float y0;
  float y1;
  /** Seed of the row, the same for the same row on every face. */
  uint32_t id;
  int index;
  bool soldier;
};

/** Rows of equal height (taller soldier rows every #soldier_every rows), with random boundaries.
 */
Vector<Row> uniform_rows(const PatternContext &ctx,
                         const float height,
                         const int soldier_every,
                         const float soldier_height,
                         const float random_height,
                         const int kind)
{
  const int n = std::max(soldier_every, 0);
  const float group = n > 0 ? float(n - 1) * height + soldier_height : height;
  const float average = n > 0 ? group / float(n) : height;
  auto base = [&](const int r) {
    if (n == 0) {
      return float(r) * height;
    }
    const int group_index = int(std::floor(float(r) / float(n)));
    return float(group_index) * group + float(r - group_index * n) * height;
  };
  auto boundary = [&](const int r) {
    const float jitter = (ctx.random(ctx.id(kind, r, 0), 1) - 0.5f) * random_height * height *
                         0.6f;
    return base(r) + jitter;
  };
  Vector<Row> rows;
  const int first = floor_div(ctx.box.min.y, average) - 2;
  for (int r = first;; r++) {
    const float y0 = boundary(r);
    if (y0 > ctx.box.max.y) {
      break;
    }
    const float y1 = boundary(r + 1);
    if (y1 < ctx.box.min.y) {
      continue;
    }
    const bool soldier = n > 0 && mod_positive(r, n) == n - 1;
    rows.append({y0, y1, ctx.id(kind, r, 1), r, soldier});
    if (rows.size() > 100000) {
      break;
    }
  }
  return rows;
}

/** Cut a band into pieces of random length between \a min_length and \a max_length. */
Vector<float> partition(const PatternContext &ctx,
                        const uint32_t band_id,
                        const float band_length,
                        const float min_length,
                        const float max_length)
{
  Vector<float> cuts;
  float t = 0.0f;
  uint32_t k = 0;
  while (t < band_length - 1e-6f) {
    float length = min_length + (max_length - min_length) * ctx.random(band_id, k++);
    if (band_length - (t + length) < min_length * 0.5f) {
      length = band_length - t;
    }
    t += length;
    cuts.append(std::min(t, band_length));
    if (cuts.size() > 1000) {
      break;
    }
  }
  return cuts;
}

/** Rows of random height in bands of fixed height, so the rows line up on every face. */
Vector<Row> banded_rows(const PatternContext &ctx,
                        const float band_height,
                        const float min_height,
                        const float max_height,
                        const int kind)
{
  Vector<Row> rows;
  const int first = floor_div(ctx.box.min.y, band_height) - 1;
  const int last = floor_div(ctx.box.max.y, band_height) + 1;
  for (int band = first; band <= last; band++) {
    const uint32_t band_id = ctx.id(kind, band, 2);
    const Vector<float> cuts = partition(ctx, band_id, band_height, min_height, max_height);
    float y0 = float(band) * band_height;
    for (const int i : cuts.index_range()) {
      const float y1 = float(band) * band_height + cuts[i];
      rows.append({y0, y1, noise::hash(band_id, uint32_t(i)), band * 64 + i, false});
      y0 = y1;
    }
  }
  return rows;
}

/** Blocks along a row between joints spaced \a length apart, shifted by \a offset. */
void row_joints(PatternContext &ctx,
                const Row &row,
                const float length,
                const float offset,
                const float random_length,
                const int kind,
                const bool cobble)
{
  const float g = ctx.gap * 0.5f;
  auto joint = [&](const int k) {
    const float jitter = (ctx.random(row.id, uint32_t(k) * 2 + 7) - 0.5f) * random_length *
                         length * 0.8f;
    return float(k) * length + offset + jitter;
  };
  const int first = floor_div(ctx.box.min.x - offset, length) - 2;
  const int last = floor_div(ctx.box.max.x - offset, length) + 2;
  for (int k = first; k <= last; k++) {
    const float x0 = joint(k);
    const float x1 = joint(k + 1);
    if (x1 - x0 <= ctx.gap) {
      continue;
    }
    Polygon block = rect(x0 + g, row.y0 + g, x1 - g, row.y1 - g);
    if (cobble) {
      block = chamfer(block, 0.22f);
    }
    ctx.add(std::move(block), ctx.id(kind, row.index, k));
  }
}

/** Blocks along a row with lengths from a repeating list, shifted by \a offset. */
void row_sequence(PatternContext &ctx,
                  const Row &row,
                  const Span<float> lengths,
                  const float offset,
                  const int kind)
{
  float period = 0.0f;
  for (const float l : lengths) {
    period += l;
  }
  const float g = ctx.gap * 0.5f;
  const int first = floor_div(ctx.box.min.x - offset, period) - 1;
  const int last = floor_div(ctx.box.max.x - offset, period) + 1;
  for (int p = first; p <= last; p++) {
    float x = float(p) * period + offset;
    for (const int i : lengths.index_range()) {
      ctx.add(rect(x + g, row.y0 + g, x + lengths[i] - g, row.y1 - g),
              ctx.id(kind, row.index, p * 8 + i));
      x += lengths[i];
    }
  }
}

/** Blocks along a row cut into random lengths in fixed bands, so they line up on every face. */
void row_banded(PatternContext &ctx,
                const Row &row,
                const float band_length,
                const float min_length,
                const float max_length,
                const int /*kind*/)
{
  const float g = ctx.gap * 0.5f;
  const int first = floor_div(ctx.box.min.x, band_length) - 1;
  const int last = floor_div(ctx.box.max.x, band_length) + 1;
  /* Shift each row's bands so the joints don't line up from row to row. */
  const float shift = ctx.random(row.id, 3) * band_length;
  for (int band = first - 1; band <= last; band++) {
    const uint32_t band_id = noise::hash(row.id, uint32_t(band));
    const Vector<float> cuts = partition(ctx, band_id, band_length, min_length, max_length);
    float x0 = float(band) * band_length + shift;
    for (const int i : cuts.index_range()) {
      const float x1 = float(band) * band_length + shift + cuts[i];
      ctx.add(rect(x0 + g, row.y0 + g, x1 - g, row.y1 - g),
              noise::hash(band_id, uint32_t(i) + 99));
      x0 = x1;
    }
  }
}

/** Running, Stack, Flemish and English bonds, with soldier rows. */
void pattern_bond(PatternContext &ctx, const MasonryPattern pattern)
{
  const MeshMasonryParams &p = ctx.params;
  const int kind = int(pattern);
  const int soldier_every = p.soldier_rows > 0 ? p.soldier_rows + 1 : 0;
  for (const Row &row :
       uniform_rows(ctx, p.height, soldier_every, p.length, p.random_height, kind))
  {
    if (row.soldier) {
      row_joints(ctx, row, p.height, 0.0f, p.random_length, kind, false);
      continue;
    }
    const bool odd = (row.index & 1) != 0;
    switch (pattern) {
      case MasonryPattern::Running:
        row_joints(ctx, row, p.length, odd ? p.length * 0.5f : 0.0f, p.random_length, kind, false);
        break;
      case MasonryPattern::Stack:
        row_joints(ctx, row, p.length, 0.0f, p.random_length, kind, false);
        break;
      case MasonryPattern::Flemish: {
        const float lengths[2] = {p.length, p.length * 0.5f};
        row_sequence(ctx, row, Span<float>(lengths, 2), odd ? p.length * 0.75f : 0.0f, kind);
        break;
      }
      case MasonryPattern::English:
        if (odd) {
          row_joints(ctx, row, p.length * 0.5f, p.length * 0.25f, p.random_length, kind, false);
        }
        else {
          row_joints(ctx, row, p.length, 0.0f, p.random_length, kind, false);
        }
        break;
      default:
        break;
    }
  }
}

/** A triangle wave from 0 to 1 with the given period. */
float triangle_wave(const float x, const float period)
{
  const float t = x / period - std::floor(x / period);
  return 1.0f - std::abs(t * 2.0f - 1.0f);
}

void pattern_interlocking(PatternContext &ctx)
{
  const MeshMasonryParams &p = ctx.params;
  const int kind = int(MasonryPattern::Interlocking);
  const float g = ctx.gap * 0.5f;
  const float tooth = p.length * 0.25f;
  const float amplitude = p.height * 0.18f;
  auto wave = [&](const float x) { return triangle_wave(x, tooth * 2.0f) * amplitude; };
  for (const Row &row : uniform_rows(ctx, p.height, 0, 0.0f, p.random_height, kind)) {
    const float offset = (row.index & 1) ? p.length * 0.5f : 0.0f;
    const int first = floor_div(ctx.box.min.x - offset, p.length) - 2;
    const int last = floor_div(ctx.box.max.x - offset, p.length) + 2;
    for (int k = first; k <= last; k++) {
      const float x0 = float(k) * p.length + offset + g;
      const float x1 = float(k + 1) * p.length + offset - g;
      Polygon block;
      /* Bottom, left to right, then top, right to left, both following the same wave. */
      Vector<float> xs = {x0};
      for (float x = std::ceil(x0 / tooth) * tooth; x < x1; x += tooth) {
        if (x > x0 + 1e-6f) {
          xs.append(x);
        }
      }
      xs.append(x1);
      for (const float x : xs) {
        block.append(float2(x, row.y0 + g + wave(x)));
      }
      for (int i = int(xs.size()) - 1; i >= 0; i--) {
        block.append(float2(xs[i], row.y1 - g + wave(xs[i])));
      }
      ctx.add(std::move(block), ctx.id(kind, row.index, k));
    }
  }
}

void pattern_chevron(PatternContext &ctx)
{
  const MeshMasonryParams &p = ctx.params;
  const int kind = int(MasonryPattern::Chevron);
  const float g = ctx.gap * 0.5f;
  const float column = p.length * 0.7f;
  auto shift = [&](const float x) { return triangle_wave(x, column * 2.0f) * column; };
  const int first_col = floor_div(ctx.box.min.x, column) - 1;
  const int last_col = floor_div(ctx.box.max.x, column) + 1;
  const int first_row = floor_div(ctx.box.min.y - column, p.height) - 1;
  const int last_row = floor_div(ctx.box.max.y, p.height) + 1;
  for (int j = first_col; j <= last_col; j++) {
    const float x0 = float(j) * column + g;
    const float x1 = float(j + 1) * column - g;
    for (int k = first_row; k <= last_row; k++) {
      const float y = float(k) * p.height;
      ctx.add({float2(x0, y + shift(x0) + g),
               float2(x1, y + shift(x1) + g),
               float2(x1, y + p.height + shift(x1) - g),
               float2(x0, y + p.height + shift(x0) - g)},
              ctx.id(kind, j, k));
    }
  }
}

void pattern_herringbone(PatternContext &ctx)
{
  const MeshMasonryParams &p = ctx.params;
  const int kind = int(MasonryPattern::Herringbone);
  const float w = p.height;
  const float l = std::max(p.length, w * 1.01f);
  const float2 a(w, w);
  const float2 b(l, -l);
  const float det = cross2(a, b);
  /* The lattice cells around the corners of the box. */
  int k_min = INT_MAX, k_max = INT_MIN, m_min = INT_MAX, m_max = INT_MIN;
  for (const float2 &corner : {ctx.box.min,
                               ctx.box.max,
                               float2(ctx.box.min.x, ctx.box.max.y),
                               float2(ctx.box.max.x, ctx.box.min.y)})
  {
    const float k = cross2(corner, b) / det;
    const float m = cross2(a, corner) / det;
    k_min = std::min(k_min, int(std::floor(k)));
    k_max = std::max(k_max, int(std::ceil(k)));
    m_min = std::min(m_min, int(std::floor(m)));
    m_max = std::max(m_max, int(std::ceil(m)));
  }
  for (int k = k_min - 2; k <= k_max + 2; k++) {
    for (int m = m_min - 2; m <= m_max + 2; m++) {
      const float2 o = a * float(k) + b * float(m);
      ctx.add_convex(rect(o.x, o.y, o.x + l, o.y + w), ctx.id(kind, k, m * 2));
      ctx.add_convex(rect(o.x, o.y + w, o.x + w, o.y + w + l), ctx.id(kind, k, m * 2 + 1));
      if (ctx.too_many) {
        return;
      }
    }
  }
}

void pattern_basket_weave(PatternContext &ctx)
{
  const MeshMasonryParams &p = ctx.params;
  const int kind = int(MasonryPattern::BasketWeave);
  const float s = p.length;
  const int n = std::max(2, int(std::round(p.length / std::max(p.height, 1e-4f))));
  const float strip = s / float(n);
  for (int i = floor_div(ctx.box.min.x, s) - 1; i <= floor_div(ctx.box.max.x, s) + 1; i++) {
    for (int j = floor_div(ctx.box.min.y, s) - 1; j <= floor_div(ctx.box.max.y, s) + 1; j++) {
      const float x = float(i) * s;
      const float y = float(j) * s;
      const bool across = ((i + j) & 1) != 0;
      for (int t = 0; t < n; t++) {
        const float a = float(t) * strip;
        ctx.add_convex(across ? rect(x + a, y, x + a + strip, y + s) :
                                rect(x, y + a, x + s, y + a + strip),
                       ctx.id(kind, i * 64 + t, j));
      }
    }
  }
}

void pattern_pinwheel(PatternContext &ctx)
{
  const MeshMasonryParams &p = ctx.params;
  const int kind = int(MasonryPattern::Pinwheel);
  const float l = p.length;
  const float t = std::min(p.height, l * 0.45f);
  for (int i = floor_div(ctx.box.min.x, l) - 1; i <= floor_div(ctx.box.max.x, l) + 1; i++) {
    for (int j = floor_div(ctx.box.min.y, l) - 1; j <= floor_div(ctx.box.max.y, l) + 1; j++) {
      const float x = float(i) * l;
      const float y = float(j) * l;
      ctx.add_convex(rect(x, y, x + l - t, y + t), ctx.id(kind, i, j * 8));
      ctx.add_convex(rect(x + l - t, y, x + l, y + l - t), ctx.id(kind, i, j * 8 + 1));
      ctx.add_convex(rect(x + t, y + l - t, x + l, y + l), ctx.id(kind, i, j * 8 + 2));
      ctx.add_convex(rect(x, y + t, x + t, y + l), ctx.id(kind, i, j * 8 + 3));
      ctx.add_convex(rect(x + t, y + t, x + l - t, y + l - t), ctx.id(kind, i, j * 8 + 4));
    }
  }
}

void pattern_versailles(PatternContext &ctx)
{
  const MeshMasonryParams &p = ctx.params;
  const int kind = int(MasonryPattern::Versailles);
  const float u = p.length * 0.5f;
  /* A repeat of 6 by 4 units with squares and rectangles of four sizes. */
  static const int4 pieces[] = {
      {0, 0, 2, 2},
      {2, 0, 2, 1},
      {2, 1, 1, 1},
      {3, 1, 1, 1},
      {4, 0, 2, 2},
      {0, 2, 1, 2},
      {1, 2, 2, 2},
      {3, 2, 1, 1},
      {3, 3, 1, 1},
      {4, 2, 2, 1},
      {4, 3, 2, 1},
  };
  const float wx = 6.0f * u;
  const float wy = 4.0f * u;
  for (int i = floor_div(ctx.box.min.x, wx) - 1; i <= floor_div(ctx.box.max.x, wx) + 1; i++) {
    for (int j = floor_div(ctx.box.min.y, wy) - 1; j <= floor_div(ctx.box.max.y, wy) + 1; j++) {
      for (const int k : IndexRange(int(std::size(pieces)))) {
        const int4 &r = pieces[k];
        const float x = float(i) * wx + float(r.x) * u;
        const float y = float(j) * wy + float(r.y) * u;
        ctx.add_convex(rect(x, y, x + float(r.z) * u, y + float(r.w) * u),
                       ctx.id(kind, i, j * 16 + k));
      }
    }
  }
}

void pattern_hexagon(PatternContext &ctx)
{
  const MeshMasonryParams &p = ctx.params;
  const int kind = int(MasonryPattern::Hexagon);
  const float r = p.length * 0.5f;
  const float dx = r * 1.5f;
  const float dy = r * std::sqrt(3.0f);
  for (int i = floor_div(ctx.box.min.x, dx) - 1; i <= floor_div(ctx.box.max.x, dx) + 1; i++) {
    for (int j = floor_div(ctx.box.min.y, dy) - 2; j <= floor_div(ctx.box.max.y, dy) + 1; j++) {
      const float2 c(float(i) * dx, (float(j) + ((i & 1) ? 0.5f : 0.0f)) * dy);
      Polygon hex;
      for (int k = 0; k < 6; k++) {
        const float angle = PI_F / 3.0f * float(k);
        hex.append(c + float2(std::cos(angle), std::sin(angle)) * r);
      }
      ctx.add_convex(hex, ctx.id(kind, i, j));
    }
  }
}

void pattern_octagon(PatternContext &ctx)
{
  const MeshMasonryParams &p = ctx.params;
  const int kind = int(MasonryPattern::Octagon);
  const float pitch = p.length;
  const float cut = pitch / (2.0f + std::sqrt(2.0f));
  const float h = pitch * 0.5f;
  const float a = h - cut;
  for (int i = floor_div(ctx.box.min.x, pitch) - 1; i <= floor_div(ctx.box.max.x, pitch) + 1; i++)
  {
    for (int j = floor_div(ctx.box.min.y, pitch) - 1; j <= floor_div(ctx.box.max.y, pitch) + 1;
         j++)
    {
      const float2 c((float(i) + 0.5f) * pitch, (float(j) + 0.5f) * pitch);
      ctx.add_convex({c + float2(h, -a),
                      c + float2(h, a),
                      c + float2(a, h),
                      c + float2(-a, h),
                      c + float2(-h, a),
                      c + float2(-h, -a),
                      c + float2(-a, -h),
                      c + float2(a, -h)},
                     ctx.id(kind, i, j * 2));
      const float2 corner(float(i) * pitch, float(j) * pitch);
      ctx.add_convex({corner + float2(cut, 0.0f),
                      corner + float2(0.0f, cut),
                      corner + float2(-cut, 0.0f),
                      corner + float2(0.0f, -cut)},
                     ctx.id(kind, i, j * 2 + 1));
    }
  }
}

void pattern_diamond(PatternContext &ctx)
{
  const MeshMasonryParams &p = ctx.params;
  const int kind = int(MasonryPattern::Diamond);
  const float hx = p.length * 0.5f;
  const float hy = std::max(p.height, p.length * 0.3f);
  /* Centers on a lattice of (hx, hy) and (hx, -hy): in rows of hy, every other one shifted. */
  for (int j = floor_div(ctx.box.min.y, hy) - 2; j <= floor_div(ctx.box.max.y, hy) + 2; j++) {
    const float shift = (j & 1) ? hx : 0.0f;
    for (int i = floor_div(ctx.box.min.x - shift, hx * 2.0f) - 1;
         i <= floor_div(ctx.box.max.x - shift, hx * 2.0f) + 1;
         i++)
    {
      const float2 c(float(i) * hx * 2.0f + shift, float(j) * hy);
      ctx.add_convex(
          {c + float2(hx, 0.0f), c + float2(0.0f, hy), c - float2(hx, 0.0f), c - float2(0.0f, hy)},
          ctx.id(kind, i, j));
    }
  }
}

/** The Voronoi cells of points on a jittered grid. */
void pattern_cells(PatternContext &ctx, const float2 pitch, const float jitter, const int kind)
{
  auto site = [&](const int i, const int j) {
    const uint32_t id = ctx.id(kind, i, j);
    return float2((float(i) + 0.5f + (ctx.random(id, 1) - 0.5f) * jitter) * pitch.x,
                  (float(j) + 0.5f + (ctx.random(id, 2) - 0.5f) * jitter) * pitch.y);
  };
  for (int i = floor_div(ctx.box.min.x, pitch.x) - 1; i <= floor_div(ctx.box.max.x, pitch.x) + 1;
       i++)
  {
    for (int j = floor_div(ctx.box.min.y, pitch.y) - 1; j <= floor_div(ctx.box.max.y, pitch.y) + 1;
         j++)
    {
      const float2 s = site(i, j);
      Polygon cell = rect(
          s.x - pitch.x * 2.0f, s.y - pitch.y * 2.0f, s.x + pitch.x * 2.0f, s.y + pitch.y * 2.0f);
      for (int di = -2; di <= 2 && !cell.is_empty(); di++) {
        for (int dj = -2; dj <= 2 && !cell.is_empty(); dj++) {
          if (di == 0 && dj == 0) {
            continue;
          }
          const float2 other = site(i + di, j + dj);
          const float2 mid = (s + other) * 0.5f;
          const float2 dir = other - s;
          /* Keep the side of the bisector toward this site: a clip edge with the site on its left.
           */
          const float2 along(-dir.y, dir.x);
          const float2 big = along * 1000.0f;
          const float2 back = -dir * 1000.0f;
          const Polygon half = {mid - big, mid + big, mid + big + back, mid - big + back};
          cell = clip(cell, half);
        }
      }
      if (cell.size() >= 3) {
        ctx.add_convex(cell, ctx.id(kind, i, j));
      }
      if (ctx.too_many) {
        return;
      }
    }
  }
}

/** A ring sector from \a a0 to \a a1, between radius \a r0 and \a r1 around \a c. */
Polygon sector(const float2 &c, const float r0, const float r1, const float a0, const float a1)
{
  Polygon poly;
  const int steps = std::max(2, int(std::ceil((a1 - a0) / (PI_F / 12.0f))));
  for (int s = 0; s <= steps; s++) {
    const float a = a0 + (a1 - a0) * float(s) / float(steps);
    poly.append(c + float2(std::cos(a), std::sin(a)) * r1);
  }
  if (r0 <= 1e-6f) {
    poly.append(c);
  }
  else {
    for (int s = steps; s >= 0; s--) {
      const float a = a0 + (a1 - a0) * float(s) / float(steps);
      poly.append(c + float2(std::cos(a), std::sin(a)) * r0);
    }
  }
  return poly;
}

float distance_to_box(const float2 &p, const Box2 &box)
{
  const float dx = std::max({box.min.x - p.x, 0.0f, p.x - box.max.x});
  const float dy = std::max({box.min.y - p.y, 0.0f, p.y - box.max.y});
  return std::sqrt(dx * dx + dy * dy);
}

float farthest_in_box(const float2 &p, const Box2 &box)
{
  const float dx = std::max(std::abs(p.x - box.min.x), std::abs(p.x - box.max.x));
  const float dy = std::max(std::abs(p.y - box.min.y), std::abs(p.y - box.max.y));
  return std::sqrt(dx * dx + dy * dy);
}

void pattern_radial(PatternContext &ctx)
{
  const MeshMasonryParams &p = ctx.params;
  const int kind = int(MasonryPattern::Radial);
  const float g = ctx.gap * 0.5f;
  const float ring = p.height;
  const float near = distance_to_box(ctx.center, ctx.box);
  const float far = farthest_in_box(ctx.center, ctx.box);
  const int first = std::max(0, int(near / ring) - 1);
  const int last = int(far / ring) + 1;
  for (int k = first; k <= last; k++) {
    const float r0 = float(k) * ring;
    const float r1 = r0 + ring;
    if (k == 0) {
      Polygon disc;
      for (int s = 0; s < 24; s++) {
        const float a = PI_F * 2.0f * float(s) / 24.0f;
        disc.append(ctx.center + float2(std::cos(a), std::sin(a)) * (r1 - g));
      }
      ctx.add(std::move(disc), ctx.id(kind, 0, 0));
      continue;
    }
    const float middle = (r0 + r1) * 0.5f;
    const int count = std::max(1, int(std::round(2.0f * PI_F * middle / p.length)));
    const float step = 2.0f * PI_F / float(count);
    const float shift = (k & 1) ? step * 0.5f : 0.0f;
    const float angle_gap = g / middle;
    for (int s = 0; s < count; s++) {
      const float a0 = shift + float(s) * step + angle_gap;
      const float a1 = shift + float(s + 1) * step - angle_gap;
      ctx.add(sector(ctx.center, r0 + g, r1 - g, a0, a1), ctx.id(kind, k, s));
      if (ctx.too_many) {
        return;
      }
    }
  }
}

void pattern_fan(PatternContext &ctx)
{
  const MeshMasonryParams &p = ctx.params;
  const int kind = int(MasonryPattern::Fan);
  const float g = ctx.gap * 0.5f;
  const float radius = p.length * 2.0f;
  const float reach = radius * 1.25f;
  const float row_step = radius * 0.7f;
  const float ring = p.height;
  auto fan_center = [&](const int i, const int j) {
    return ctx.center +
           float2(float(i) * radius * 2.0f + ((j & 1) ? radius : 0.0f), float(j) * row_step);
  };
  /* The fan a point belongs to: the highest one at or below it that reaches it. */
  auto owner = [&](const float2 &q) {
    const float2 local = q - ctx.center;
    const int top = floor_div(local.y, row_step);
    for (int j = top; j >= top - 3; j--) {
      const float shift = (j & 1) ? radius : 0.0f;
      const int around = int(std::round((local.x - shift) / (radius * 2.0f)));
      for (int i = around - 1; i <= around + 1; i++) {
        if (math::distance(q, fan_center(i, j)) <= reach) {
          return int2(i, j);
        }
      }
    }
    return int2(INT_MAX, INT_MAX);
  };
  const float2 low = ctx.box.min - ctx.center;
  const float2 high = ctx.box.max - ctx.center;
  for (int j = floor_div(low.y - reach, row_step) - 1; j <= floor_div(high.y, row_step) + 1; j++) {
    for (int i = floor_div(low.x - reach, radius * 2.0f) - 1;
         i <= floor_div(high.x + reach, radius * 2.0f) + 1;
         i++)
    {
      const float2 c = fan_center(i, j);
      for (int k = 0; float(k) * ring < reach; k++) {
        const float r0 = float(k) * ring;
        const float r1 = r0 + ring;
        const float middle = (r0 + r1) * 0.5f;
        const int count = k == 0 ? 1 : std::max(1, int(std::round(PI_F * middle / (ring * 1.1f))));
        const float step = PI_F / float(count);
        const float angle_gap = k == 0 ? 0.0f : g / middle;
        for (int s = 0; s < count; s++) {
          Polygon block = sector(c,
                                 k == 0 ? 0.0f : r0 + g,
                                 r1 - g,
                                 float(s) * step + angle_gap,
                                 float(s + 1) * step - angle_gap);
          if (owner(centroid(block)) != int2(i, j)) {
            continue;
          }
          ctx.add(std::move(block), ctx.id(kind, i * 4096 + k, j * 256 + s));
          if (ctx.too_many) {
            return;
          }
        }
      }
    }
  }
}

void pattern_custom(PatternContext &ctx)
{
  const MeshMasonryParams &p = ctx.params;
  const int kind = int(MasonryPattern::Custom);
  const float2 size = math::max(p.tile_size, float2(1e-4f));
  for (int i = floor_div(ctx.box.min.x, size.x) - 1; i <= floor_div(ctx.box.max.x, size.x) + 1;
       i++)
  {
    for (int j = floor_div(ctx.box.min.y, size.y) - 1; j <= floor_div(ctx.box.max.y, size.y) + 1;
         j++)
    {
      const float2 shift(float(i) * size.x, float(j) * size.y);
      for (const int k : p.tile.index_range()) {
        Polygon poly;
        for (const float2 &q : p.tile[k]) {
          poly.append(q + shift);
        }
        if (signed_area(poly) < 0.0f) {
          std::reverse(poly.begin(), poly.end());
        }
        if (is_convex(poly)) {
          ctx.add_convex(poly, ctx.id(kind, i * 1024 + k, j));
        }
        else {
          ctx.add(std::move(poly), ctx.id(kind, i * 1024 + k, j));
        }
        if (ctx.too_many) {
          return;
        }
      }
    }
  }
}

void generate(PatternContext &ctx)
{
  const MeshMasonryParams &p = ctx.params;
  const int kind = int(p.pattern);
  switch (p.pattern) {
    case MasonryPattern::Running:
    case MasonryPattern::Stack:
    case MasonryPattern::Flemish:
    case MasonryPattern::English:
      pattern_bond(ctx, p.pattern);
      break;
    case MasonryPattern::Ashlar:
      for (const Row &row : banded_rows(ctx, p.height * 6.0f, p.height, p.height * 2.0f, kind)) {
        row_banded(ctx, row, p.length * 4.0f, p.length * 0.6f, p.length * 1.6f, kind);
      }
      break;
    case MasonryPattern::Ledgestone:
      for (const Row &row :
           banded_rows(ctx, p.height * 4.0f, p.height * 0.35f, p.height * 0.9f, kind))
      {
        row_banded(ctx, row, p.length * 5.0f, p.length * 0.8f, p.length * 2.5f, kind);
      }
      break;
    case MasonryPattern::Rubble:
      pattern_cells(ctx, float2(p.length * 0.8f, p.height * 1.6f), 0.8f, kind);
      break;
    case MasonryPattern::Herringbone:
      pattern_herringbone(ctx);
      break;
    case MasonryPattern::Chevron:
      pattern_chevron(ctx);
      break;
    case MasonryPattern::BasketWeave:
      pattern_basket_weave(ctx);
      break;
    case MasonryPattern::Pinwheel:
      pattern_pinwheel(ctx);
      break;
    case MasonryPattern::Versailles:
      pattern_versailles(ctx);
      break;
    case MasonryPattern::Hexagon:
      pattern_hexagon(ctx);
      break;
    case MasonryPattern::Octagon:
      pattern_octagon(ctx);
      break;
    case MasonryPattern::Diamond:
      pattern_diamond(ctx);
      break;
    case MasonryPattern::Interlocking:
      pattern_interlocking(ctx);
      break;
    case MasonryPattern::Cobblestone: {
      const float cobble = p.height * 1.15f;
      for (const Row &row :
           uniform_rows(ctx, p.height, 0, 0.0f, std::max(p.random_height, 0.3f), kind))
      {
        row_joints(ctx,
                   row,
                   cobble,
                   ctx.random(row.id, 5) * cobble,
                   std::max(p.random_length, 0.5f),
                   kind,
                   true);
      }
      break;
    }
    case MasonryPattern::Flagstone:
      pattern_cells(ctx, float2(p.length * 1.5f, p.length * 1.5f), 0.9f, kind);
      break;
    case MasonryPattern::Fan:
      pattern_fan(ctx);
      break;
    case MasonryPattern::Radial:
      pattern_radial(ctx);
      break;
    case MasonryPattern::Custom:
      pattern_custom(ctx);
      break;
  }
}

/** Blocks along the outline of a convex face, and the smaller outline inside them. */
Polygon add_border(PatternContext &ctx, const Span<float2> face)
{
  const MeshMasonryParams &p = ctx.params;
  const Polygon inner = inset_convex(face, p.height);
  if (inner.size() != face.size()) {
    return Polygon(face);
  }
  for (const int i : face.index_range()) {
    const int next = (i + 1) % face.size();
    const float length = math::distance(face[i], face[next]);
    const int count = std::max(1, int(std::round(length / p.length)));
    for (int s = 0; s < count; s++) {
      const float t0 = float(s) / float(count);
      const float t1 = float(s + 1) / float(count);
      const Polygon quad = {math::interpolate(face[i], face[next], t0),
                            math::interpolate(face[i], face[next], t1),
                            math::interpolate(inner[i], inner[next], t1),
                            math::interpolate(inner[i], inner[next], t0)};
      ctx.add_convex(quad, noise::hash(ctx.seed_hash, uint32_t(i), uint32_t(s) + 77777u));
    }
  }
  return inner;
}

/** Irregular outline: more points, each pulled toward the middle by a random amount. */
Polygon stone_outline(const Span<float2> poly, const float amount, const uint32_t id)
{
  const float2 c = centroid(poly);
  Polygon result;
  uint32_t k = 0;
  for (const int i : poly.index_range()) {
    const float2 &a = poly[i];
    const float2 &b = poly[(i + 1) % poly.size()];
    for (const float t : {0.0f, 1.0f / 3.0f, 2.0f / 3.0f}) {
      const float2 q = math::interpolate(a, b, t);
      const float pull = noise::hash_to_float(id, k++) * amount * 0.35f;
      result.append(math::interpolate(q, c, pull));
    }
  }
  return result;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Building the Blocks
 * \{ */

struct BlockMeshData {
  Vector<float3> positions;
  Vector<int> vert_block;
  Vector<int> face_sizes;
  Vector<int> face_materials;
  Vector<int> corner_verts;
  Vector<float2> corner_uvs;
  int blocks_num = 0;
};

float3 rotate_around(const float3 &p, const float3 &pivot, const float3 &axis, const float angle)
{
  const float3 v = p - pivot;
  const float c = std::cos(angle);
  const float s = std::sin(angle);
  return pivot + v * c + math::cross(axis, v) * s + axis * (math::dot(axis, v) * (1.0f - c));
}

void build_block(BlockMeshData &data,
                 const MeshMasonryParams &params,
                 const Span<float2> outline,
                 const uint32_t id,
                 const float3 &u,
                 const float3 &v,
                 const float3 &n,
                 const float plane,
                 const int material)
{
  const int count = outline.size();
  const float area = std::abs(signed_area(outline));
  const float depth_random = (noise::hash_to_float(id, 11) - 0.5f) * params.random_depth;
  const float depth = std::max(params.depth * (1.0f + depth_random), params.depth * 0.2f);
  const float base = -params.offset * params.depth;
  auto to_3d = [&](const float2 &q, const float w) { return u * q.x + v * q.y + n * (plane + w); };

  const int first_vert = data.positions.size();
  for (const float2 &q : outline) {
    data.positions.append(to_3d(q, base));
  }
  for (const float2 &q : outline) {
    data.positions.append(to_3d(q, base + depth));
  }
  /* Dome: a ring halfway in and a point in the middle, raised. */
  const bool dome = params.dome > 0.0f;
  const float2 middle = centroid(outline);
  if (dome) {
    const float lift = params.dome * std::sqrt(area / PI_F) * 0.5f;
    for (const float2 &q : outline) {
      data.positions.append(
          to_3d(math::interpolate(q, middle, 0.5f), base + depth + lift * 0.75f));
    }
    data.positions.append(to_3d(middle, base + depth + lift));
  }
  const int verts_num = data.positions.size() - first_vert;

  if (params.tilt > 0.0f) {
    const float turn = noise::hash_to_float(id, 12) * 2.0f * PI_F;
    const float3 axis = u * std::cos(turn) + v * std::sin(turn);
    const float angle = noise::hash_to_float(id, 13) * params.tilt;
    const float3 pivot = to_3d(middle, base + depth * 0.5f);
    for (const int i : IndexRange(first_vert, verts_num)) {
      data.positions[i] = rotate_around(data.positions[i], pivot, axis, angle);
    }
  }
  const int block = data.blocks_num++;
  for ([[maybe_unused]] const int i : IndexRange(verts_num)) {
    data.vert_block.append(block);
  }

  auto add_face = [&](const Span<int> verts, const Span<float2> uvs) {
    data.face_sizes.append(verts.size());
    data.face_materials.append(material);
    for (const int i : verts.index_range()) {
      data.corner_verts.append(first_vert + verts[i]);
      data.corner_uvs.append(uvs[i]);
    }
  };

  /* Bottom, facing into the face. */
  {
    Vector<int, 16> verts;
    Vector<float2, 16> uvs;
    for (int i = count - 1; i >= 0; i--) {
      verts.append(i);
      uvs.append(outline[i]);
    }
    add_face(verts, uvs);
  }
  /* Sides. */
  float along = 0.0f;
  for (const int i : IndexRange(count)) {
    const int next = (i + 1) % count;
    const float length = math::distance(outline[i], outline[next]);
    const int verts[4] = {i, next, count + next, count + i};
    const float2 uvs[4] = {float2(along, 0.0f),
                           float2(along + length, 0.0f),
                           float2(along + length, depth),
                           float2(along, depth)};
    add_face(Span<int>(verts, 4), Span<float2>(uvs, 4));
    along += length;
  }
  /* Top. */
  if (dome) {
    const int ring = count * 2;
    const int tip = count * 3;
    for (const int i : IndexRange(count)) {
      const int next = (i + 1) % count;
      const int verts[4] = {count + i, count + next, ring + next, ring + i};
      const float2 uvs[4] = {outline[i],
                             outline[next],
                             math::interpolate(outline[next], middle, 0.5f),
                             math::interpolate(outline[i], middle, 0.5f)};
      add_face(Span<int>(verts, 4), Span<float2>(uvs, 4));
      const int fan[3] = {ring + i, ring + next, tip};
      const float2 fan_uvs[3] = {math::interpolate(outline[i], middle, 0.5f),
                                 math::interpolate(outline[next], middle, 0.5f),
                                 middle};
      add_face(Span<int>(fan, 3), Span<float2>(fan_uvs, 3));
    }
  }
  else {
    Vector<int, 16> verts;
    for (const int i : IndexRange(count)) {
      verts.append(count + i);
    }
    add_face(verts, outline);
  }
}

/** The frame of a face: u along the rows, v across them, n out of the face. */
void face_frame(const MeshMasonryParams &params,
                const float3 &normal,
                const Span<float3> face_positions,
                float3 &r_u,
                float3 &r_v)
{
  const float3 n = normal;
  auto project = [&](const float3 &dir) { return dir - n * math::dot(n, dir); };
  auto longest_edge = [&]() {
    float3 best(1.0f, 0.0f, 0.0f);
    float best_length = -1.0f;
    for (const int i : face_positions.index_range()) {
      const float3 edge = face_positions[(i + 1) % face_positions.size()] - face_positions[i];
      const float length = math::length_squared(edge);
      if (length > best_length) {
        best_length = length;
        best = edge;
      }
    }
    return best;
  };
  float3 u(0.0f);
  switch (params.direction) {
    case MasonryDirection::Auto:
      u = std::abs(n.z) < 0.7f ? math::cross(float3(0.0f, 0.0f, 1.0f), n) :
                                 project(float3(1.0f, 0.0f, 0.0f));
      break;
    case MasonryDirection::WorldX:
      u = project(float3(1.0f, 0.0f, 0.0f));
      break;
    case MasonryDirection::WorldY:
      u = project(float3(0.0f, 1.0f, 0.0f));
      break;
    case MasonryDirection::Face:
      u = project(longest_edge());
      break;
  }
  if (math::length_squared(u) < 1e-8f) {
    u = project(float3(0.0f, 1.0f, 0.0f));
  }
  if (math::length_squared(u) < 1e-8f) {
    u = project(float3(1.0f, 0.0f, 0.0f));
  }
  u = math::normalize(u);
  float3 v = math::cross(n, u);
  float angle = params.angle;
  if (params.pattern == MasonryPattern::Herringbone) {
    angle += PI_F * 0.25f;
  }
  if (angle != 0.0f) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    const float3 u2 = u * c + v * s;
    v = v * c - u * s;
    u = u2;
  }
  r_u = u;
  r_v = v;
}

/** A wall laid out in distance along the wall and height. Internal mesh edges do not clip bricks.
 */
struct WallPatch {
  Vector<int> faces;
  Vector<Polygon> face_outlines;
  Polygon outline;
};

struct WallBoundary {
  int start_vert, end_vert;
  float2 start, end;
};

Vector<WallPatch> wall_patches(const Mesh &mesh,
                               const MeshMasonryParams &params,
                               MutableSpan<int> face_patch,
                               MutableSpan<float> shifts,
                               MutableSpan<float3> directions)
{
  Vector<WallPatch> patches;
  if (!params.continuous_walls || params.direction != MasonryDirection::Auto ||
      int(params.pattern) > int(MasonryPattern::Rubble))
  {
    return patches;
  }
  const Span<float3> positions = mesh.vert_positions();
  const Span<int2> edges = mesh.edges();
  const OffsetIndices faces = mesh.faces();
  const Span<int> corner_verts = mesh.corner_verts();
  const Span<int> corner_edges = mesh.corner_edges();
  const Span<float3> normals = mesh.face_normals();
  const VArraySpan<bool> sharp = *mesh.attributes().lookup_or_default<bool>(
      "sharp_edge", bke::AttrDomain::Edge, false);
  Array<int> edge_a(mesh.edges_num, -1), edge_b(mesh.edges_num, -1);
  Array<bool> eligible(mesh.faces_num, false), visited(mesh.faces_num, false);
  Array<int> component_id(mesh.faces_num, -1);
  const float tolerance = std::max(params.length, params.height) * 1e-5f + 1e-7f;
  for (const int face : faces.index_range()) {
    eligible[face] = std::abs(normals[face].z) < 1e-5f &&
                     (params.selection.is_empty() || params.selection[face]);
    directions[face] = math::normalize(math::cross(float3(0, 0, 1), normals[face]));
    for (const int edge : corner_edges.slice(faces[face])) {
      if (edge_a[edge] == -1) {
        edge_a[edge] = face;
      }
      else if (edge_b[edge] == -1) {
        edge_b[edge] = face;
      }
      else {
        edge_b[edge] = -2; /* Non-manifold edges break the layout. */
      }
    }
  }
  auto neighbor = [&](const int edge, const int face) {
    return edge_b[edge] < 0 ? -1 : (edge_a[edge] == face ? edge_b[edge] : edge_a[edge]);
  };
  auto can_cross = [&](const int a, const int b, const int edge) {
    if (b < 0 || !eligible[b] || sharp[edge] || math::dot(normals[a], normals[b]) <= 0.707107f) {
      return false;
    }
    const float3 delta = positions[edges[edge][1]] - positions[edges[edge][0]];
    return std::abs(math::dot(delta, directions[a] - directions[b])) < tolerance;
  };
  auto coordinate = [&](const int face, const int vert) {
    return float2(math::dot(positions[vert], directions[face]) + shifts[face], positions[vert].z);
  };
  for (const int root : faces.index_range()) {
    if (!eligible[root] || visited[root]) {
      continue;
    }
    WallPatch patch;
    patch.faces.append(root);
    shifts[root] = 0;
    visited[root] = true;
    component_id[root] = root;
    for (int i = 0; i < patch.faces.size(); i++) {
      const int face = patch.faces[i];
      for (const int edge : corner_edges.slice(faces[face])) {
        const int other = neighbor(edge, face);
        if (!can_cross(face, other, edge) || visited[other]) {
          continue;
        }
        const float3 point = positions[edges[edge][0]];
        shifts[other] = shifts[face] + math::dot(point, directions[face] - directions[other]);
        visited[other] = true;
        component_id[other] = root;
        patch.faces.append(other);
      }
    }
    if (patch.faces.size() < 2) {
      continue;
    }
    Vector<WallBoundary> boundary;
    Map<int, Vector<int>> starts;
    for (const int face : patch.faces) {
      Polygon outline;
      const IndexRange corners = faces[face];
      for (const int corner : corners) {
        const int next = corners.start() + (corner - corners.start() + 1) % corners.size();
        const int a = corner_verts[corner], b = corner_verts[next];
        const int edge = corner_edges[corner];
        const int other = neighbor(edge, face);
        const float2 qa = coordinate(face, a), qb = coordinate(face, b);
        outline.append(qa);
        const bool internal = can_cross(face, other, edge) && component_id[other] == root &&
                              math::distance_squared(qa, coordinate(other, a)) <
                                  tolerance * tolerance &&
                              math::distance_squared(qb, coordinate(other, b)) <
                                  tolerance * tolerance;
        if (!internal) {
          starts.lookup_or_add_default(a).append(boundary.size());
          boundary.append({a, b, qa, qb});
        }
      }
      patch.face_outlines.append(std::move(outline));
    }
    if (boundary.is_empty()) {
      continue;
    }
    Array<bool> used(boundary.size(), false);
    int current = 0;
    bool valid = true;
    for (const int step : boundary.index_range()) {
      const WallBoundary &segment = boundary[current];
      used[current] = true;
      patch.outline.append(segment.start);
      if (segment.end_vert == boundary[0].start_vert &&
          math::distance_squared(segment.end, boundary[0].start) < tolerance * tolerance)
      {
        valid = step ==
                boundary.size() - 1; /* Multiple boundary loops, such as holes, fall back. */
        break;
      }
      int next = -1;
      if (const Vector<int> *candidates = starts.lookup_ptr(segment.end_vert)) {
        for (const int candidate : *candidates) {
          if (!used[candidate] && math::distance_squared(segment.end, boundary[candidate].start) <
                                      tolerance * tolerance)
          {
            if (next != -1) {
              valid = false;
              break;
            }
            next = candidate;
          }
        }
      }
      if (!valid || next == -1 || step == boundary.size() - 1) {
        valid = false;
        break;
      }
      current = next;
    }
    if (!valid || patch.outline.size() < 3) {
      continue;
    }
    /* Remove straight subdivision points before testing convexity and clipping. */
    Polygon simplified;
    const int count = patch.outline.size();
    for (const int i : patch.outline.index_range()) {
      const float2 a = patch.outline[(i + count - 1) % count];
      const float2 b = patch.outline[i];
      const float2 c = patch.outline[(i + 1) % count];
      if (math::dot(b - a, c - b) >= 0 &&
          std::abs(cross2(b - a, c - b)) <=
              tolerance * (math::distance(a, b) + math::distance(b, c)))
      {
        continue;
      }
      simplified.append(b);
    }
    patch.outline = std::move(simplified);
    if (signed_area(patch.outline) < 0) {
      std::reverse(patch.outline.begin(), patch.outline.end());
    }
    if (patch.outline.size() < 3 || !is_convex(patch.outline)) {
      continue;
    }
    for (const int face : patch.faces) {
      face_patch[face] = patches.size();
    }
    patches.append(std::move(patch));
  }
  return patches;
}

/** Choose the surface frame under a brick's center, extrapolating at toothed boundaries. */
int wall_sample_face(const WallPatch &patch, const float2 &point)
{
  int result = patch.faces[0];
  float best = FLT_MAX;
  for (const int i : patch.faces.index_range()) {
    const Polygon &outline = patch.face_outlines[i];
    if (point_inside(outline, point)) {
      return patch.faces[i];
    }
    for (const int j : outline.index_range()) {
      const float2 a = outline[j], delta = outline[(j + 1) % outline.size()] - a;
      const float length_squared = math::length_squared(delta);
      const float t = length_squared > 0 ?
                          std::clamp(math::dot(point - a, delta) / length_squared, 0.0f, 1.0f) :
                          0;
      const float distance = math::distance_squared(point, a + t * delta);
      if (distance < best) {
        best = distance;
        result = patch.faces[i];
      }
    }
  }
  return result;
}

/** \} */

}  // namespace

Mesh *mesh_masonry(const Mesh &mesh, const MeshMasonryParams &params)
{
  if (mesh.faces_num == 0 || params.length <= 0.0f || params.height <= 0.0f) {
    return nullptr;
  }
  const Span<float3> positions = mesh.vert_positions();
  const OffsetIndices<int> faces = mesh.faces();
  const Span<int> corner_verts = mesh.corner_verts();
  const Span<float3> face_normals = mesh.face_normals();
  const bke::AttributeAccessor attributes = mesh.attributes();
  const VArraySpan<int> materials = *attributes.lookup_or_default<int>(
      "material_index", bke::AttrDomain::Face, 0);

  float3 center;
  if (params.center) {
    center = *params.center;
  }
  else {
    const Bounds<float3> bounds = *mesh.bounds_min_max();
    center = (bounds.min + bounds.max) * 0.5f;
  }

  const uint32_t seed_hash = noise::hash(uint32_t(params.seed) + 1u);
  const float gap = std::max(params.mortar, 0.0f);
  BlockMeshData data;
  Array<bool> covered(mesh.faces_num, false);
  Array<int> face_patch(mesh.faces_num, -1);
  Array<float> wall_shifts(mesh.faces_num, 0.0f);
  Array<float3> wall_directions(mesh.faces_num);
  const Vector<WallPatch> patches = wall_patches(
      mesh, params, face_patch, wall_shifts, wall_directions);

  for (const int face : faces.index_range()) {
    if (!params.selection.is_empty() && !params.selection[face]) {
      continue;
    }
    const IndexRange face_range = faces[face];
    if (face_range.size() < 3) {
      continue;
    }
    const WallPatch *patch = face_patch[face] >= 0 ? &patches[face_patch[face]] : nullptr;
    if (patch && patch->faces[0] != face) {
      continue;
    }
    Vector<float3, 16> face_positions;
    if (patch) {
      for (const float2 &point : patch->outline) {
        face_positions.append(float3(point.x, point.y, 0));
      }
    }
    else {
      for (const int corner : face_range) {
        face_positions.append(positions[corner_verts[corner]]);
      }
    }
    const float3 n = patch ? float3(0, 0, 1) : face_normals[face];
    float3 u, v;
    face_frame(params, n, face_positions, u, v);
    float3 face_center(0.0f);
    for (const float3 &p : face_positions) {
      face_center += p;
    }
    face_center /= float(face_positions.size());
    const float plane = math::dot(face_center, n);

    Polygon outline;
    for (const float3 &p : face_positions) {
      outline.append(float2(math::dot(p, u), math::dot(p, v)));
    }
    if (signed_area(outline) < 0.0f) {
      std::reverse(outline.begin(), outline.end());
    }
    if (std::abs(signed_area(outline)) < 1e-10f) {
      continue;
    }

    Vector<Block> blocks;
    PatternContext ctx{
        params, {}, float2(math::dot(center, u), math::dot(center, v)), seed_hash, gap, blocks};
    ctx.box.min = float2(FLT_MAX);
    ctx.box.max = float2(-FLT_MAX);
    for (const float2 &q : outline) {
      ctx.box.min = math::min(ctx.box.min, q);
      ctx.box.max = math::max(ctx.box.max, q);
    }
    const float block_area = std::max(params.length * params.height, 1e-8f);
    const float2 box_size = ctx.box.max - ctx.box.min;
    if (box_size.x * box_size.y / block_area > float(MAX_BLOCKS_PER_FACE)) {
      continue;
    }

    const bool face_convex = is_convex(outline);
    Polygon fill_outline = outline;
    if (params.border && face_convex) {
      fill_outline = add_border(ctx, outline);
      for (Block &block : blocks) {
        /* Border blocks are already inside the face. */
        block.id = ~block.id;
      }
    }
    const int border_count = blocks.size();
    generate(ctx);
    if (ctx.too_many) {
      continue;
    }

    const bool fill_convex = is_convex(fill_outline);
    for (const int i : blocks.index_range()) {
      Block &block = blocks[i];
      Polygon shape = std::move(block.outline);
      if (params.stone_shape > 0.0f) {
        shape = stone_outline(shape, params.stone_shape, block.id);
      }
      if (i >= border_count) {
        if (params.toothed) {
          if (!point_inside(fill_outline, centroid(shape))) {
            continue;
          }
        }
        else if (!fully_inside(shape, fill_outline)) {
          if (is_convex(shape)) {
            shape = clip(fill_outline, shape);
          }
          else if (fill_convex) {
            shape = clip(shape, fill_outline);
          }
          else if (!point_inside(fill_outline, centroid(shape))) {
            continue;
          }
        }
      }
      if (shape.size() < 3 || std::abs(signed_area(shape)) < block_area * 1e-3f) {
        continue;
      }
      const float2 middle = centroid(shape);
      const float3 flat_middle = u * middle.x + v * middle.y;
      const int sample = patch ? wall_sample_face(*patch, float2(flat_middle.x, flat_middle.y)) :
                                 face;
      const int material = params.block_material >= 0 ? params.block_material : materials[sample];
      const int first_vert = data.positions.size();
      build_block(data, params, shape, block.id, u, v, n, plane, material);
      if (patch) {
        const float3 wall_u = wall_directions[sample];
        const float3 wall_n = face_normals[sample];
        const float surface_plane = math::dot(positions[corner_verts[faces[sample].start()]],
                                              wall_n);
        const float3 anchor = wall_u * (flat_middle.x - wall_shifts[sample]) +
                              float3(0, 0, flat_middle.y) + wall_n * surface_plane;
        for (const int vert : IndexRange(first_vert, data.positions.size() - first_vert)) {
          const float3 p = data.positions[vert];
          data.positions[vert] = anchor + wall_u * (p.x - flat_middle.x) +
                                 float3(0, 0, p.y - flat_middle.y) + wall_n * p.z;
        }
      }
    }
    if (patch) {
      for (const int patch_face : patch->faces) {
        covered[patch_face] = true;
      }
    }
    else {
      covered[face] = true;
    }
  }

  if (data.blocks_num == 0) {
    return nullptr;
  }

  /* The blocks. */
  Mesh *blocks_mesh = bke::mesh_new_no_attributes(
      data.positions.size(), 0, data.face_sizes.size(), data.corner_verts.size());
  blocks_mesh->vert_positions_for_write().copy_from(data.positions);
  {
    MutableSpan<int> offsets = blocks_mesh->face_offsets_for_write();
    offsets[0] = 0;
    for (const int i : data.face_sizes.index_range()) {
      offsets[i + 1] = offsets[i] + data.face_sizes[i];
    }
  }
  blocks_mesh->corner_verts_for_write().copy_from(data.corner_verts);
  bke::mesh_calc_edges(*blocks_mesh, false, false);
  {
    bke::MutableAttributeAccessor block_attributes = blocks_mesh->attributes_for_write();
    if (attributes.contains("material_index") || params.block_material >= 0) {
      bke::SpanAttributeWriter<int> material_index =
          block_attributes.lookup_or_add_for_write_only_span<int>("material_index",
                                                                  bke::AttrDomain::Face);
      material_index.span.copy_from(data.face_materials);
      material_index.finish();
    }
    const StringRef uv_name = mesh.active_or_default_uv_map_name();
    if (!uv_name.is_empty()) {
      bke::SpanAttributeWriter<float2> uv =
          block_attributes.lookup_or_add_for_write_only_span<float2>(uv_name,
                                                                     bke::AttrDomain::Corner);
      uv.span.copy_from(data.corner_uvs);
      uv.finish();
    }
    if (params.block_index_id) {
      bke::SpanAttributeWriter<int> index =
          block_attributes.lookup_or_add_for_write_only_span<int>(*params.block_index_id,
                                                                  bke::AttrDomain::Point);
      index.span.copy_from(data.vert_block);
      index.finish();
    }
    if (params.mortar_id) {
      block_attributes.add<bool>(
          *params.mortar_id, bke::AttrDomain::Face, bke::AttributeInitDefaultValue());
    }
  }

  /* The mortar: the faces under the blocks, moved down with them. The other faces stay. */
  Mesh *backing = nullptr;
  if (params.backing) {
    backing = BKE_mesh_copy_for_eval(mesh);
  }
  else {
    Array<bool> keep(mesh.faces_num);
    for (const int i : keep.index_range()) {
      keep[i] = !covered[i];
    }
    const std::optional<Mesh *> kept = mesh_copy_selection(
        mesh, VArray<bool>::from_span(keep), bke::AttrDomain::Face);
    backing = kept ? *kept : BKE_mesh_copy_for_eval(mesh);
  }
  if (backing != nullptr && params.backing) {
    const float base = -params.offset * params.depth;
    if (base != 0.0f) {
      const Span<float3> vert_normals = mesh.vert_normals();
      Array<bool> moved(mesh.verts_num, false);
      for (const int face : faces.index_range()) {
        if (covered[face]) {
          for (const int corner : faces[face]) {
            moved[corner_verts[corner]] = true;
          }
        }
      }
      MutableSpan<float3> backing_positions = backing->vert_positions_for_write();
      for (const int i : backing_positions.index_range()) {
        if (moved[i]) {
          backing_positions[i] += vert_normals[i] * base;
        }
      }
      backing->tag_positions_changed();
    }
    bke::MutableAttributeAccessor backing_attributes = backing->attributes_for_write();
    if (params.mortar_material >= 0) {
      bke::SpanAttributeWriter<int> material_index =
          backing_attributes.lookup_or_add_for_write_span<int>("material_index",
                                                               bke::AttrDomain::Face);
      for (const int face : material_index.span.index_range()) {
        if (covered[face]) {
          material_index.span[face] = params.mortar_material;
        }
      }
      material_index.finish();
    }
    if (params.mortar_id) {
      bke::SpanAttributeWriter<bool> mortar =
          backing_attributes.lookup_or_add_for_write_only_span<bool>(*params.mortar_id,
                                                                     bke::AttrDomain::Face);
      mortar.span.copy_from(covered);
      mortar.finish();
    }
  }
  if (backing != nullptr && params.block_index_id) {
    bke::MutableAttributeAccessor backing_attributes = backing->attributes_for_write();
    bke::SpanAttributeWriter<int> index =
        backing_attributes.lookup_or_add_for_write_only_span<int>(*params.block_index_id,
                                                                  bke::AttrDomain::Point);
    index.span.fill(-1);
    index.finish();
  }

  if (backing == nullptr) {
    return blocks_mesh;
  }
  const Vector<bke::GeometrySet> parts = {bke::GeometrySet::from_mesh(backing),
                                          bke::GeometrySet::from_mesh(blocks_mesh)};
  bke::GeometrySet joined = join_geometries(parts, {});
  return joined.get_component_for_write<bke::MeshComponent>().release();
}

}  // namespace blender::geometry
