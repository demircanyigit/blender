/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup geo
 */

#include <algorithm>
#include <cmath>

#include "BLI_math_base.h"
#include "BLI_array.hh"
#include "BLI_math_base.hh"
#include "BLI_math_vector.hh"
#include "BLI_noise.hh"
#include "BLI_task.hh"

#include "DNA_mesh_types.h"

#include "BKE_attribute.hh"
#include "BKE_mesh.hh"

#include "GEO_mesh_carve.hh"

namespace blender::geometry {

/* -------------------------------------------------------------------- */
/** \name Patterns
 * \{ */

static float hash3(const float a, const float b, const float c)
{
  return noise::hash_float_to_float(float3(a, b, c));
}

static float smooth01(const float x)
{
  const float t = std::clamp(x, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

static float value_noise(const float x, const float y, const float channel)
{
  const float ix = std::floor(x);
  const float iy = std::floor(y);
  const float ux = smooth01(x - ix);
  const float uy = smooth01(y - iy);
  const float a = hash3(ix, iy, channel);
  const float b = hash3(ix + 1.0f, iy, channel);
  const float c = hash3(ix, iy + 1.0f, channel);
  const float d = hash3(ix + 1.0f, iy + 1.0f, channel);
  const float bottom = a + (b - a) * ux;
  const float top = c + (d - c) * ux;
  return bottom + (top - bottom) * uy;
}

static float value_fbm(const float x, const float y, const float channel)
{
  return (value_noise(x, y, channel) * 4.0f + value_noise(x * 2.0f, y * 2.0f, channel) * 2.0f +
          value_noise(x * 4.0f, y * 4.0f, channel)) /
         7.0f;
}

/**
 * Courses of stones (or regular bricks) as a height: 0 in the mortar, rising over a rounded
 * shoulder to the face of each stone.
 */
static float stone_wall(const float2 p, const CarvePatternParams &params, const bool bricks)
{
  const float rh = std::max(params.row_height, 1e-4f);
  const float ln = std::max(params.stone_length, 1e-4f);
  const float variation = std::clamp(params.variation, 0.0f, 1.0f);
  const float rv = bricks ? 0.0f : variation * 0.45f;
  const float lv = bricks ? 0.0f : variation * 0.45f;
  const float split = bricks ? 0.0f : 0.15f;
  const float irregularity = bricks ? 0.0f : 0.6f * variation;
  const float depth_variation = bricks ? 0.05f : 0.2f;
  const float surface = std::max(params.roughness, 0.0f) * 0.3f;

  auto row_edge = [&](const float k) { return (k + (hash3(k, 0.0f, 1.0f) - 0.5f) * 2.0f * rv) * rh; };

  float k = std::floor(p.y / rh);
  if (p.y < row_edge(k)) {
    k -= 1.0f;
  }
  else if (p.y >= row_edge(k + 1.0f)) {
    k += 1.0f;
  }
  float y0 = row_edge(k);
  float y1 = row_edge(k + 1.0f);

  const float off = bricks ? (std::fmod(std::abs(k), 2.0f) * 0.5f * ln) :
                             hash3(k, 0.0f, 2.0f) * ln;
  auto stone_edge = [&](const float i) {
    return (i + (hash3(k, i, 3.0f) - 0.5f) * 2.0f * lv) * ln - off;
  };
  float i = std::floor((p.x + off) / ln);
  if (p.x < stone_edge(i)) {
    i -= 1.0f;
  }
  else if (p.x >= stone_edge(i + 1.0f)) {
    i += 1.0f;
  }
  const float x0 = stone_edge(i);
  const float x1 = stone_edge(i + 1.0f);

  /* Tall stones are sometimes two smaller stones on top of each other. */
  float sub = 0.0f;
  if ((y1 - y0) > 1.05f * rh && hash3(k, i, 4.0f) < split) {
    const float ys = y0 + (y1 - y0) * (0.4f + 0.2f * hash3(k, i, 5.0f));
    if (p.y < ys) {
      y1 = ys;
    }
    else {
      y0 = ys;
      sub = 1.0f;
    }
  }
  const float sid = hash3(k, i, 6.0f + sub);

  const float m = std::max(params.mortar, 0.0f) * 0.5f;
  const float hx = (x1 - x0) * 0.5f - m;
  const float hy = (y1 - y0) * 0.5f - m;
  if (hx <= 0.0f || hy <= 0.0f) {
    return 0.0f;
  }
  const float cx = (x0 + x1) * 0.5f;
  const float cy = (y0 + y1) * 0.5f;
  const float small_side = std::min(hx, hy);
  const float rnd = std::clamp(params.roundness, 0.0f, 1.0f);
  const float r = rnd * small_side;
  const float qx = std::abs(p.x - cx) - (hx - r);
  const float qy = std::abs(p.y - cy) - (hy - r);
  float sdf = std::sqrt(math::square(std::max(qx, 0.0f)) + math::square(std::max(qy, 0.0f))) +
              std::min(std::max(qx, qy), 0.0f) - r;
  /* Irregular edges only eat into the stone, so it stays inside its cell. */
  sdf += value_noise(p.x / rh * 2.0f + sid * 50.0f, p.y / rh * 2.0f, 7.0f) * irregularity *
         small_side;
  const float inside = -sdf;
  if (inside <= 0.0f) {
    return 0.0f;
  }
  const float bevel = std::max(rnd * small_side, 1e-5f);
  const float t = std::min(inside / bevel, 1.0f);
  const float shoulder = t * (2.0f - t);
  float face = 1.0f - depth_variation * hash3(k, i, 8.0f + sub);
  face += surface * (value_fbm(p.x / rh * 4.0f, p.y / rh * 4.0f, 9.0f) - 0.5f);
  return std::clamp(shoulder * face, 0.0f, 1.0f);
}

static float value_fbm_octaves(const float x, const float y, const float channel, const int octaves)
{
  float sum = 0.0f;
  float weight = 1.0f;
  float total = 0.0f;
  float f = 1.0f;
  for (int o = 0; o < octaves; o++) {
    sum += value_noise(x * f, y * f, channel + float(o)) * weight;
    total += weight;
    weight *= 0.5f;
    f *= 2.0f;
  }
  return sum / total;
}

/* -------------------------------------------------------------------- */
/** \name Walls
 *
 * Each stone is a shape of its own: a rounded box (courses and ashlar) or a circle with a
 * wobbly outline (cells). A point belongs to the shape it is deepest inside, so joints fall
 * midway between neighbors: beds step up and down, mortar stays even, and corners round off
 * where joints meet. Only the stones near the point are looked at, so any point can be read on
 * its own.
 * \{ */

enum class WallKind : int8_t { Courses, Cells, Ashlar };

struct WallStyle {
  WallKind kind = WallKind::Courses;
  /* Courses. */
  float rh = 0.2f, rv = 0.0f, ln = 0.5f, lv = 0.0f, jl = 0.0f, step = 0.0f, tilt = 0.03f;
  float rad_lo = 0.3f, rad_hi = 0.6f;
  float tall = 0.0f, stack = 0.0f, filler = 0.0f, split = 0.0f;
  int band = 0;
  /* Cells. */
  float size = 0.4f, jitter = 0.8f, weight = 0.2f, stretch = 1.0f, wobble = 0.0f;
  /* Ashlar. */
  float unit = 0.3f;
  /* All. */
  float mortar = 0.03f, rc = 0.25f, depth = 0.3f, bulge = 0.3f;
};

static WallStyle wall_style(const CarvePattern pattern)
{
  WallStyle s;
  switch (pattern) {
    case CarvePattern::RandomStone:
      s.rh = 0.2f, s.rv = 0.4f, s.ln = 0.5f, s.lv = 0.45f, s.jl = 0.6f, s.step = 0.12f;
      s.rad_lo = 0.6f, s.rad_hi = 0.95f, s.stack = 0.08f, s.split = 0.35f;
      s.mortar = 0.035f, s.depth = 0.35f, s.bulge = 0.4f;
      break;
    case CarvePattern::CoursedBlocks:
      s.rh = 0.22f, s.rv = 0.1f, s.ln = 0.6f, s.lv = 0.15f, s.jl = 0.3f, s.step = 0.03f;
      s.tilt = 0.01f, s.rad_lo = 0.05f, s.rad_hi = 0.2f, s.split = 0.1f;
      s.mortar = 0.04f, s.rc = 0.08f, s.depth = 0.2f, s.bulge = 0.15f;
      break;
    case CarvePattern::Slate:
      s.rh = 0.09f, s.rv = 0.6f, s.ln = 0.6f, s.lv = 0.5f, s.jl = 0.7f, s.step = 0.15f;
      s.rad_lo = 0.6f, s.rad_hi = 1.0f, s.stack = 0.1f, s.split = 0.25f;
      s.mortar = 0.022f, s.depth = 0.4f, s.bulge = 0.2f;
      break;
    case CarvePattern::DryStone:
      s.rh = 0.12f, s.rv = 0.6f, s.ln = 0.42f, s.lv = 0.5f, s.jl = 0.6f, s.step = 0.15f;
      s.tilt = 0.04f, s.rad_lo = 0.4f, s.rad_hi = 0.9f, s.tall = 0.04f, s.stack = 0.12f;
      s.split = 0.3f, s.mortar = 0.015f, s.depth = 0.45f, s.bulge = 0.2f;
      break;
    case CarvePattern::Banded:
      s.rh = 0.12f, s.rv = 0.25f, s.ln = 0.15f, s.lv = 0.25f, s.jl = 0.3f, s.step = 0.12f;
      s.tilt = 0.04f, s.rad_lo = 0.2f, s.rad_hi = 0.5f, s.band = 5;
      s.mortar = 0.013f, s.depth = 0.4f, s.bulge = 0.25f;
      break;
    case CarvePattern::Fieldstone:
      s.kind = WallKind::Cells;
      s.size = 0.4f, s.jitter = 0.85f, s.weight = 0.18f, s.wobble = 0.1f;
      s.mortar = 0.055f, s.rc = 0.2f, s.depth = 0.35f, s.bulge = 0.5f;
      break;
    case CarvePattern::Rubble:
      s.kind = WallKind::Cells;
      s.size = 0.38f, s.jitter = 0.95f, s.weight = 0.3f, s.stretch = 1.2f, s.wobble = 0.06f;
      s.mortar = 0.045f, s.rc = 0.06f, s.depth = 0.35f, s.bulge = 0.3f;
      break;
    case CarvePattern::Cobblestone:
      s.kind = WallKind::Cells;
      s.size = 0.3f, s.jitter = 0.6f, s.weight = 0.12f, s.wobble = 0.05f;
      s.mortar = 0.05f, s.rc = 0.14f, s.depth = 0.3f, s.bulge = 0.6f;
      break;
    case CarvePattern::Ashlar:
      s.kind = WallKind::Ashlar;
      s.unit = 0.3f, s.rad_lo = 0.04f, s.rad_hi = 0.15f;
      s.mortar = 0.05f, s.rc = 0.05f, s.depth = 0.25f, s.bulge = 0.12f;
      break;
    default:
      break;
  }
  return s;
}

/** The shape a point is deepest inside, and how deep it is in the next two. */
struct WallNearest {
  float d1 = 1e9f, d2 = 1e9f, d3 = 1e9f;
  float u = 0.0f, v = 0.0f, id = 0.0f;
  bool crack = false;
  float2 crack_normal = float2(1.0f, 0.0f);
  float crack_offset = 0.0f;

  void add(const float d,
           const float su,
           const float sv,
           const float sid,
           const bool has_crack = false,
           const float2 normal = float2(1.0f, 0.0f),
           const float offset = 0.0f)
  {
    if (d < d1) {
      d3 = d2;
      d2 = d1;
      d1 = d;
      u = su;
      v = sv;
      id = sid;
      crack = has_crack;
      crack_normal = normal;
      crack_offset = offset;
    }
    else if (d < d2) {
      d3 = d2;
      d2 = d;
    }
    else if (d < d3) {
      d3 = d;
    }
  }
};

/** Signed distance to a turned, rounded box (negative inside), and the point in the box's own
 * coordinates from -1 to 1. */
static float box_sdf(const float2 p,
                     const float2 center,
                     const float2 half,
                     const float angle,
                     const float radius,
                     float2 &r_uv)
{
  const float c = std::cos(angle);
  const float s = std::sin(angle);
  const float2 d = p - center;
  const float lx = d.x * c + d.y * s;
  const float ly = -d.x * s + d.y * c;
  r_uv = float2(lx / std::max(half.x, 1e-6f), ly / std::max(half.y, 1e-6f));
  const float r = std::min(radius, std::min(half.x, half.y));
  const float qx = std::abs(lx) - (half.x - r);
  const float qy = std::abs(ly) - (half.y - r);
  return std::sqrt(math::square(std::max(qx, 0.0f)) + math::square(std::max(qy, 0.0f))) +
         std::min(std::max(qx, qy), 0.0f) - r;
}

static float fract(const float x)
{
  return x - std::floor(x);
}

static void wall_courses(const float2 p, const WallStyle &s, WallNearest &nearest)
{
  const float rh = s.rh;
  const float ln = s.ln;
  const int band = s.band;
  const float extra = band > 0 ? 0.5f * rh : 0.0f;

  auto is_band = [&](const float r) {
    return band > 0 && (r - float(band) * std::floor(r / float(band))) == 0.0f;
  };
  /* Band courses are taller: every band below pushes the courses up. */
  auto row_edge = [&](const float r) {
    const float bands_below = band > 0 ? std::floor((r - 1.0f) / float(band)) + 1.0f : 0.0f;
    const float jitter = (is_band(r) || is_band(r - 1.0f)) ?
                             0.0f :
                             (hash3(r, 0.0f, 32.0f) - 0.5f) * s.rv;
    return (r + jitter) * rh + bands_below * extra;
  };
  auto row_len = [&](const float r) {
    return is_band(r) ? ln * 3.0f : ln * (1.0f + (hash3(r, 0.0f, 31.0f) - 0.5f) * 2.0f * s.lv);
  };
  /* About half a stone from the course below, so head joints never line up. */
  auto row_off = [&](const float r) { return fract(r * 0.5f + (hash3(r, 0.0f, 1.0f) - 0.5f) * 0.5f); };

  float r0 = std::floor(p.y / (rh + (band > 0 ? extra / float(band) : 0.0f)));
  for (int iter = 0; iter < 3; iter++) {
    if (p.y < row_edge(r0)) {
      r0 -= 1.0f;
    }
    else if (p.y >= row_edge(r0 + 1.0f)) {
      r0 += 1.0f;
    }
  }

  for (int dr = -1; dr <= 1; dr++) {
    const float r = r0 + float(dr);
    const float lnr = row_len(r);
    const float off = row_off(r);
    const float e0 = row_edge(r);
    const float e1 = row_edge(r + 1.0f);
    const float e2 = row_edge(r + 2.0f);
    const float H = e1 - e0;
    const bool band_row = is_band(r);
    const float c0 = std::floor(p.x / lnr - off);
    auto joint = [&](const float i) {
      return (i + off + (hash3(r, i, 3.0f) - 0.5f) * s.jl) * lnr;
    };
    for (int dc = -2; dc <= 2; dc++) {
      const float c = c0 + float(dc);
      const float x0 = joint(c);
      const float x1 = joint(c + 1.0f);
      /* A tall stone from the course below takes this spot. */
      {
        const float rb = r - 1.0f;
        const float cb = std::floor((x0 + x1) * 0.5f / row_len(rb) - row_off(rb));
        if (!is_band(rb) && !band_row && hash3(rb, cb, 33.0f) < s.tall) {
          continue;
        }
      }
      /* Each stone's own bottom and top: stepped beds. */
      const float st = band_row ? s.step * 0.3f : s.step;
      const float b = e0 + (hash3(r, c, 4.0f) - 0.5f) * 2.0f * st * H;
      float t = e1 + (hash3(r, c, 5.0f) - 0.5f) * 2.0f * st * H;
      const bool tall = !band_row && !is_band(r + 1.0f) && hash3(r, c, 33.0f) < s.tall;
      if (tall) {
        t = e2 + (hash3(r, c, 5.0f) - 0.5f) * 2.0f * st * H;
      }
      const float angle = (hash3(r, c, 6.0f) - 0.5f) * 2.0f * (band_row ? s.tilt * 0.3f : s.tilt);
      const float radf = s.rad_lo + (s.rad_hi - s.rad_lo) * hash3(r, c, 7.0f);
      const float sid = hash3(r, c, 8.0f);
      const bool stack = !band_row && !tall && hash3(r, c, 34.0f) < s.stack;
      const bool fill = !band_row && !tall && !stack &&
                        (value_noise((c + off) * 0.45f, r * 0.45f, 40.0f) * 0.7f +
                         hash3(r, c, 41.0f) * 0.3f) < s.filler * 1.6f;
      const float2 half((x1 - x0) * 0.5f, (t - b) * 0.5f);
      const float2 center((x0 + x1) * 0.5f, (b + t) * 0.5f);
      float2 uv;
      if (stack) {
        /* Two thin stones stacked, the top one shifted. */
        const float split = b + (t - b) * (0.35f + 0.3f * hash3(r, c, 35.0f));
        const float shift = (hash3(r, c, 36.0f) - 0.5f) * 0.5f * (x1 - x0);
        const float ys[3] = {b, split, t};
        for (int k = 0; k < 2; k++) {
          const float hy = (ys[k + 1] - ys[k]) * 0.5f;
          const float2 sc(center.x + (k == 1 ? shift : 0.0f), (ys[k] + ys[k + 1]) * 0.5f);
          const float d = box_sdf(p,
                                  sc,
                                  float2(half.x, hy),
                                  angle + (hash3(r, c, 37.0f + float(k)) - 0.5f) * 0.06f,
                                  radf * std::min(half.x, hy),
                                  uv);
          nearest.add(d, uv.x, uv.y, hash3(r, c, 38.0f + float(k)));
        }
      }
      else if (fill) {
        /* A patch of small rounded stones. */
        for (int k = 0; k < 4; k++) {
          const float u = float(k % 2) * 0.5f + 0.25f;
          const float v = float(k / 2) * 0.5f + 0.25f;
          const float fx = x0 + (x1 - x0) * (u + (hash3(r, c, 42.0f + float(k)) - 0.5f) * 0.15f);
          const float fy = b + (t - b) * (v + (hash3(r, c, 46.0f + float(k)) - 0.5f) * 0.15f);
          const float fh = 0.3f * std::min(x1 - x0, t - b) *
                           (0.85f + 0.3f * hash3(r, c, 50.0f + float(k)));
          const float d = box_sdf(p,
                                  float2(fx, fy),
                                  float2(fh * 1.2f, fh),
                                  (hash3(r, c, 54.0f + float(k)) - 0.5f) * 0.6f,
                                  fh * 0.9f,
                                  uv);
          nearest.add(d, uv.x, uv.y, hash3(r, c, 58.0f + float(k)));
        }
      }
      else {
        /* Now and then a long stone is cracked in two. */
        const bool crack = (x1 - x0) > 1.15f * ln && hash3(r, c, 60.0f) < s.split;
        const float th = (hash3(r, c, 61.0f) - 0.5f) * 1.1f;
        const float ccx = center.x + (hash3(r, c, 62.0f) - 0.5f) * 0.35f * (x1 - x0);
        const float2 normal(std::cos(th), std::sin(th));
        const float d = box_sdf(p, center, half, angle, radf * std::min(half.x, half.y), uv);
        nearest.add(d, uv.x, uv.y, sid, crack, normal, normal.x * ccx + normal.y * center.y);
      }
    }
  }
}

static void wall_cells(const float2 p, const WallStyle &s, WallNearest &nearest)
{
  const float size = s.size;
  const float st = s.stretch;
  const float2 q(p.x / st, p.y);
  const float gx0 = std::floor(q.x / size);
  const float gy0 = std::floor(q.y / size);
  for (int dy = -2; dy <= 2; dy++) {
    for (int dx = -2; dx <= 2; dx++) {
      const float gx = gx0 + float(dx);
      const float gy = gy0 + float(dy);
      const float cx = (gx + 0.5f + (hash3(gx, gy, 1.0f) - 0.5f) * s.jitter) * size * st;
      const float cy = (gy + 0.5f + (hash3(gx, gy, 2.0f) - 0.5f) * s.jitter) * size;
      float radius = (hash3(gx, gy, 3.0f) - 0.5f) * 2.0f * s.weight * size;
      const float sid = hash3(gx, gy, 4.0f);
      const float2 d = p - float2(cx, cy);
      if (s.wobble > 0.0f) {
        /* A wobbly outline: a few waves around the center. */
        const float th = std::atan2(d.y, d.x);
        float wob = 0.0f;
        for (int k = 2; k <= 5; k++) {
          const float a = hash3(sid * 17.0f, float(k), 70.0f) / float(k);
          const float ph = hash3(sid * 23.0f, float(k), 71.0f) * float(M_PI) * 2.0f;
          wob += a * std::cos(float(k) * th + ph);
        }
        radius += s.wobble * size * wob;
      }
      const float dist = math::length(d) - radius;
      nearest.add(dist, d.x / (size * 0.6f), d.y / (size * 0.6f), sid);
    }
  }
}

static void wall_ashlar(const float2 p, const WallStyle &s, WallNearest &nearest)
{
  const float u = s.unit;
  const float M = 2.0f * u;
  /* Each 2x2 block is one stone, two halves, a half and two quarters, or four quarters. */
  static const float parts[5][4][4] = {
      {{0, 0, 2, 2}, {-1, 0, 0, 0}, {-1, 0, 0, 0}, {-1, 0, 0, 0}},
      {{0, 0, 2, 1}, {0, 1, 2, 1}, {-1, 0, 0, 0}, {-1, 0, 0, 0}},
      {{0, 0, 1, 2}, {1, 0, 1, 2}, {-1, 0, 0, 0}, {-1, 0, 0, 0}},
      {{0, 0, 2, 1}, {0, 1, 1, 1}, {1, 1, 1, 1}, {-1, 0, 0, 0}},
      {{0, 0, 1, 1}, {1, 0, 1, 1}, {0, 1, 1, 1}, {1, 1, 1, 1}},
  };
  const float gy0 = std::floor(p.y / M);
  for (int dy = -1; dy <= 1; dy++) {
    const float gy = gy0 + float(dy);
    const float off = hash3(gy, 0.0f, 1.0f) * M;
    const float gx0 = std::floor((p.x + off) / M);
    for (int dx = -1; dx <= 1; dx++) {
      const float gx = gx0 + float(dx);
      const float x0 = gx * M - off;
      const float y0 = gy * M;
      const int kind = std::min(int(hash3(gx, gy, 2.0f) * 5.0f), 4);
      for (int k = 0; k < 4; k++) {
        const float *part = parts[kind][k];
        if (part[0] < 0.0f) {
          break;
        }
        const float2 half(part[2] * u * 0.5f, part[3] * u * 0.5f);
        const float2 center(x0 + part[0] * u + half.x, y0 + part[1] * u + half.y);
        const float radf = s.rad_lo + (s.rad_hi - s.rad_lo) * hash3(gx, gy, 10.0f + float(k));
        float2 uv;
        const float d = box_sdf(p, center, half, 0.0f, radf * std::min(half.x, half.y), uv);
        nearest.add(d, uv.x, uv.y, hash3(gx * 4.0f + float(k), gy, 11.0f));
      }
    }
  }
}

/**
 * The wall patterns. The settings scale each style; their defaults give the style as designed.
 */
static float wall(const float2 p, const CarvePatternParams &params)
{
  WallStyle s = wall_style(params.pattern);
  const float lenf = std::max(params.stone_length, 1e-4f) / 0.8f;
  const float rowf = std::max(params.row_height, 1e-4f) / 0.25f;
  const float mortf = std::max(params.mortar, 0.0f) / 0.02f;
  const float roundf = std::clamp(params.roundness, 0.0f, 1.0f) / 0.5f;
  const float varf = std::clamp(params.variation, 0.0f, 1.0f) / 0.5f;
  s.rh *= rowf;
  s.ln *= lenf;
  s.size *= lenf;
  s.unit *= lenf;
  s.mortar *= mortf;
  s.rad_lo = std::min(s.rad_lo * roundf, 1.0f);
  s.rad_hi = std::min(s.rad_hi * roundf, 1.0f);
  s.rc *= roundf;
  s.rv = std::min(s.rv * varf, 0.9f);
  s.lv = std::min(s.lv * varf, 0.9f);
  s.jl = std::min(s.jl * varf, 0.95f);
  s.step *= varf;
  s.tilt *= varf;
  s.jitter = std::min(s.jitter * varf, 1.0f);
  s.weight *= varf;

  WallNearest nearest;
  float rh = s.rh;
  switch (s.kind) {
    case WallKind::Courses:
      wall_courses(p, s, nearest);
      break;
    case WallKind::Cells:
      wall_cells(p, s, nearest);
      rh = s.size;
      break;
    case WallKind::Ashlar:
      wall_ashlar(p, s, nearest);
      rh = s.unit;
      break;
  }

  /* Joints open and close along their length. */
  const float mortar = s.mortar * (0.5f + value_noise(p.x / rh * 1.3f, p.y / rh * 1.3f, 60.0f));
  float ja = (nearest.d2 - nearest.d1) * 0.5f - mortar * 0.5f;
  float jb = (nearest.d3 - nearest.d1) * 0.5f - mortar * 0.5f;
  float id = nearest.id;
  if (nearest.crack) {
    /* A crack is one more joint; the two pieces sit at their own heights. */
    const float side = math::dot(nearest.crack_normal, p) - nearest.crack_offset;
    const float crack = std::abs(side) - mortar * 0.4f;
    if (crack < ja) {
      jb = std::min(jb, ja);
      ja = crack;
    }
    if (side > 0.0f) {
      id = hash3(id * 9.0f, 3.0f, 72.0f);
    }
  }
  /* Round off corners where joints meet. */
  const float rc = s.rc * rh;
  const float a = rc - ja;
  const float b = rc - jb;
  float inside = (a > 0.0f && b > 0.0f) ? rc - std::sqrt(a * a + b * b) : std::min(ja, jb);
  if (s.kind == WallKind::Courses) {
    /* Where stones leave a wide gap, show a little mortar pool. */
    inside = std::min(inside, -nearest.d1 + 0.6f * rh);
  }
  if (inside <= 0.0f) {
    return 0.0f;
  }
  const float bevel = 0.18f * rh;
  const float t = std::min(inside / bevel, 1.0f);
  const float shoulder = std::sqrt(t * (2.0f - t));
  const float uv2 = nearest.u * nearest.u + nearest.v * nearest.v;
  float face = (1.0f - s.bulge * 0.4f * std::clamp(uv2, 0.0f, 2.0f)) *
               (1.0f - s.depth * hash3(id * 3.0f, 1.0f, 21.0f));
  face += std::max(params.roughness, 0.0f) * 0.08f *
          (value_fbm_octaves(p.x / rh * 6.0f, p.y / rh * 6.0f, 24.0f, 4) - 0.5f);
  return std::clamp(shoulder * std::clamp(face, 0.0f, 1.3f), 0.0f, 1.0f);
}

/** \} */

/** Fitted and packed walls, in mesh_carve_layout.cc. */
float carve_layout_wall(float2 p, const CarvePatternParams &params);

static float voronoi_stones(const float3 p, const CarvePatternParams &params)
{
  noise::VoronoiParams voronoi;
  voronoi.scale = 1.0f;
  voronoi.detail = 0.0f;
  voronoi.roughness = 0.5f;
  voronoi.lacunarity = 2.0f;
  voronoi.smoothness = 1.0f;
  voronoi.exponent = 0.5f;
  voronoi.randomness = std::clamp(params.variation, 0.0f, 1.0f);
  voronoi.max_distance = 0.0f;
  voronoi.normalize = false;
  voronoi.feature = 0;
  voronoi.metric = 0;
  const float edge = noise::voronoi_distance_to_edge(voronoi, p) - std::max(params.mortar, 0.0f);
  if (edge <= 0.0f) {
    return 0.0f;
  }
  const float bevel = std::max(std::clamp(params.roundness, 0.0f, 1.0f) * 0.5f, 1e-4f);
  const float t = std::min(edge / bevel, 1.0f);
  float h = t * (2.0f - t);
  h *= 1.0f + std::max(params.roughness, 0.0f) * 0.3f *
                  (noise::perlin_fbm(p * 4.0f, 2.0f, 0.5f, 2.0f, true) - 0.5f);
  return std::clamp(h, 0.0f, 1.0f);
}

float carve_pattern_value(const CarvePatternParams &params,
                          const float3 &coord,
                          const float3 &normal,
                          const bool flat)
{
  float3 seed_offset(0.0f);
  if (params.seed != 0) {
    seed_offset = noise::hash_float_to_float3(float(params.seed)) * 1000.0f;
  }
  const float3 c = coord + params.offset;

  /* The plane of flat patterns: the axis the normal points along most is dropped. */
  float2 plane(c.x, c.y);
  if (!flat) {
    const float3 n = math::abs(normal);
    if (n.x >= n.y && n.x >= n.z) {
      plane = float2(c.y, c.z);
    }
    else if (n.y >= n.x && n.y >= n.z) {
      plane = float2(c.x, c.z);
    }
  }
  const float sn = std::sin(params.rotation);
  const float cs = std::cos(params.rotation);
  const float2 turned(plane.x * cs - plane.y * sn, plane.x * sn + plane.y * cs);
  const float2 p2 = turned * params.scale + float2(seed_offset.x, seed_offset.y);
  const float3 c3(c.x * cs - c.y * sn, c.x * sn + c.y * cs, c.z);
  const float3 p3 = c3 * params.scale + seed_offset;

  switch (params.pattern) {
    case CarvePattern::Noise:
      return noise::perlin_fbm(
          p3, std::max(params.detail, 0.0f), std::clamp(params.roughness, 0.0f, 1.0f), 2.0f, true);
    case CarvePattern::Voronoi:
      return voronoi_stones(p3, params);
    case CarvePattern::RandomStone:
    case CarvePattern::CoursedBlocks:
    case CarvePattern::Slate:
    case CarvePattern::DryStone:
    case CarvePattern::Banded:
    case CarvePattern::Fieldstone:
    case CarvePattern::Rubble:
    case CarvePattern::Cobblestone:
    case CarvePattern::Ashlar:
      return wall(p2, params);
    case CarvePattern::FittedRubble:
    case CarvePattern::RoughCourses:
    case CarvePattern::SquaredStone:
    case CarvePattern::MixedRubble:
    case CarvePattern::WornStone:
    case CarvePattern::ThinCourses:
    case CarvePattern::PackedRound:
    case CarvePattern::PackedAngular:
    case CarvePattern::PackedBlocks:
    case CarvePattern::PackedSlabs:
    case CarvePattern::PackedWedges:
    case CarvePattern::PackedMixed:
    case CarvePattern::DryWall:
      return carve_layout_wall(p2, params);
    case CarvePattern::Bricks:
      return stone_wall(p2, params, true);
    case CarvePattern::Waves: {
      float s = p2.x;
      if (params.distortion != 0.0f) {
        s += params.distortion * (noise::perlin_fbm(p3,
                                                    std::max(params.detail, 0.0f),
                                                    std::clamp(params.roughness, 0.0f, 1.0f),
                                                    2.0f,
                                                    true) *
                                      2.0f -
                                  1.0f);
      }
      return 0.5f + 0.5f * std::sin(s * float(M_PI) * 2.0f);
    }
  }
  return 0.0f;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Carve
 * \{ */

static float apply_levels(float v, const CarveLayer &layer)
{
  const float range = layer.white - layer.black;
  v = range != 0.0f ? (v - layer.black) / range : (v >= layer.black ? 1.0f : 0.0f);
  v = std::clamp(v, 0.0f, 1.0f);
  if (layer.contrast != 0.0f) {
    const float c = std::clamp(layer.contrast, -0.99f, 0.99f);
    v = std::clamp(0.5f + (v - 0.5f) * std::tan((c + 1.0f) * float(M_PI) / 4.0f), 0.0f, 1.0f);
  }
  if (layer.invert) {
    v = 1.0f - v;
  }
  return v * layer.strength;
}

static void sample_layer(const CarveLayer &layer,
                         const Span<float3> normals,
                         MutableSpan<float> r_values)
{
  threading::parallel_for(r_values.index_range(), 1024, [&](const IndexRange range) {
    for (const int i : range) {
      float v = 0.0f;
      if (!layer.values.is_empty()) {
        v = layer.values[i];
      }
      else if (!layer.coords.is_empty()) {
        v = carve_pattern_value(layer.pattern, layer.coords[i], normals[i], layer.flat);
      }
      r_values[i] = apply_levels(v, layer);
    }
  });
}

static float blend(const CarveBlend mode, const float a, const float b, const float factor)
{
  float r = b;
  switch (mode) {
    case CarveBlend::Mix:
      r = b;
      break;
    case CarveBlend::Add:
      r = a + b;
      break;
    case CarveBlend::Subtract:
      r = a - b;
      break;
    case CarveBlend::Multiply:
      r = a * b;
      break;
    case CarveBlend::Lighter:
      r = std::max(a, b);
      break;
    case CarveBlend::Darker:
      r = std::min(a, b);
      break;
    case CarveBlend::Difference:
      r = std::abs(a - b);
      break;
  }
  return a + (r - a) * factor;
}

static float soft_step(const float value, const float threshold, const float softness)
{
  if (softness <= 0.0f) {
    return value >= threshold ? 1.0f : 0.0f;
  }
  return smooth01((value - (threshold - softness)) / (2.0f * softness));
}

/** Neighbors of each point, through edges. */
struct Neighbors {
  Array<int> offsets;
  Array<int> indices;
  Span<int> of(const int vert) const
  {
    return indices.as_span().slice(offsets[vert], offsets[vert + 1] - offsets[vert]);
  }
};

static Neighbors build_neighbors(const Mesh &mesh)
{
  const Span<int2> edges = mesh.edges();
  Neighbors n;
  n.offsets = Array<int>(mesh.verts_num + 1, 0);
  for (const int2 &e : edges) {
    n.offsets[e[0] + 1]++;
    n.offsets[e[1] + 1]++;
  }
  for (const int v : IndexRange(mesh.verts_num)) {
    n.offsets[v + 1] += n.offsets[v];
  }
  n.indices = Array<int>(edges.size() * 2);
  Array<int> fill(mesh.verts_num, 0);
  for (const int2 &e : edges) {
    n.indices[n.offsets[e[0]] + fill[e[0]]++] = e[1];
    n.indices[n.offsets[e[1]] + fill[e[1]]++] = e[0];
  }
  return n;
}

/** Positive where the surface bulges out, negative in hollows, about the height of the point
 * over its neighbors divided by the length of its edges. */
static Array<float> compute_curvature(const Span<float3> positions,
                                      const Span<float3> normals,
                                      const Neighbors &neighbors)
{
  Array<float> curvature(positions.size(), 0.0f);
  threading::parallel_for(positions.index_range(), 2048, [&](const IndexRange range) {
    for (const int v : range) {
      const Span<int> near = neighbors.of(v);
      if (near.is_empty()) {
        continue;
      }
      float3 center(0.0f);
      float length = 0.0f;
      for (const int other : near) {
        center += positions[other];
        length += math::distance(positions[v], positions[other]);
      }
      center /= float(near.size());
      length /= float(near.size());
      if (length > 0.0f) {
        curvature[v] = math::dot(normals[v], positions[v] - center) / length;
      }
    }
  });
  return curvature;
}

static void smooth_values(MutableSpan<float> values, const Neighbors &neighbors, const int steps)
{
  Array<float> next(values.size());
  for ([[maybe_unused]] const int step : IndexRange(steps)) {
    threading::parallel_for(values.index_range(), 2048, [&](const IndexRange range) {
      for (const int v : range) {
        const Span<int> near = neighbors.of(v);
        if (near.is_empty()) {
          next[v] = values[v];
          continue;
        }
        float sum = 0.0f;
        for (const int other : near) {
          sum += values[other];
        }
        next[v] = values[v] * 0.5f + sum / float(near.size()) * 0.5f;
      }
    });
    values.copy_from(next);
  }
}

static bool layer_has_materials(const CarveLayer &layer)
{
  return layer.enabled && (layer.material_high >= 0 || layer.material_low >= 0);
}

/** Materials by layer values and mask, averaged over each face. Later steps win: the mask
 * material, then Layer A, then Layer B. */
static void assign_materials(Mesh &mesh,
                             const MeshCarveParams &params,
                             const Span<float> a_values,
                             const Span<float> b_values,
                             const Span<float> mask)
{
  const bool use_a = layer_has_materials(params.a);
  const bool use_b = layer_has_materials(params.b);
  if (mesh.faces_num == 0 || (params.mask_material < 0 && !use_a && !use_b)) {
    return;
  }
  const OffsetIndices faces = mesh.faces();
  const Span<int> corner_verts = mesh.corner_verts();
  bke::MutableAttributeAccessor attributes = mesh.attributes_for_write();
  bke::SpanAttributeWriter<int> material_index = attributes.lookup_or_add_for_write_span<int>(
      "material_index", bke::AttrDomain::Face);
  if (!material_index) {
    return;
  }
  auto average = [&](const Span<float> values, const IndexRange face) {
    float sum = 0.0f;
    for (const int vert : corner_verts.slice(face)) {
      sum += values[vert];
    }
    return sum / float(face.size());
  };
  auto apply_layer = [&](const CarveLayer &layer,
                         const Span<float> values,
                         const IndexRange face,
                         const bool masked,
                         int &index) {
    if (layer.material_masked && !masked) {
      return;
    }
    const int material = average(values, face) > layer.material_split ? layer.material_high :
                                                                         layer.material_low;
    if (material >= 0) {
      index = material;
    }
  };
  threading::parallel_for(faces.index_range(), 1024, [&](const IndexRange range) {
    for (const int f : range) {
      const IndexRange face = faces[f];
      const bool masked = average(mask, face) > params.mask_material_threshold;
      int index = material_index.span[f];
      if (params.mask_material >= 0 && masked) {
        index = params.mask_material;
      }
      if (use_a) {
        apply_layer(params.a, a_values, face, masked, index);
      }
      if (use_b) {
        apply_layer(params.b, b_values, face, masked, index);
      }
      material_index.span[f] = index;
    }
  });
  material_index.finish();
}

bool mesh_carve(Mesh &mesh, const MeshCarveParams &params)
{
  const int verts_num = mesh.verts_num;
  if (verts_num == 0 || (!params.a.enabled && !params.b.enabled)) {
    return false;
  }
  MutableSpan<float3> positions = mesh.vert_positions_for_write();
  const Array<float3> normals(mesh.vert_normals());

  /* The combined grayscale. */
  Array<float> height(verts_num, 0.0f);
  Array<float> a_values;
  Array<float> b_values;
  if (params.a.enabled) {
    a_values.reinitialize(verts_num);
    sample_layer(params.a, normals, a_values);
    height.as_mutable_span().copy_from(a_values);
  }
  if (params.b.enabled) {
    b_values.reinitialize(verts_num);
    sample_layer(params.b, normals, b_values);
    if (params.a.enabled) {
      threading::parallel_for(height.index_range(), 4096, [&](const IndexRange range) {
        for (const int i : range) {
          height[i] = blend(params.blend, height[i], b_values[i], params.blend_factor);
        }
      });
    }
    else {
      height.as_mutable_span().copy_from(b_values);
    }
  }

  const bool need_neighbors = params.curvature != CarveCurvature::Off ||
                              params.curvature_id.has_value() || params.smooth > 0;
  Neighbors neighbors;
  if (need_neighbors) {
    neighbors = build_neighbors(mesh);
  }
  Array<float> curvature;
  if (params.curvature != CarveCurvature::Off || params.curvature_id) {
    curvature = compute_curvature(positions, normals, neighbors);
  }

  /* Where to carve, from 0 to 1. */
  Array<float> mask(verts_num, 1.0f);
  float3 mask_offset(0.0f);
  if (params.seed != 0) {
    mask_offset = noise::hash_float_to_float3(float(params.seed) + 0.5f) * 1000.0f;
  }
  threading::parallel_for(mask.index_range(), 1024, [&](const IndexRange range) {
    for (const int i : range) {
      float m = params.weights.is_empty() ? 1.0f : std::clamp(params.weights[i], 0.0f, 1.0f);
      if (params.noise_mask && m > 0.0f) {
        const float n = noise::perlin_fbm(positions[i] * params.mask_scale + mask_offset,
                                          std::max(params.mask_detail, 0.0f),
                                          0.5f,
                                          2.0f,
                                          true);
        const float s = soft_step(n, params.mask_threshold, params.mask_softness);
        m *= params.mask_invert ? 1.0f - s : s;
      }
      if (params.curvature != CarveCurvature::Off && m > 0.0f) {
        const float k = curvature[i];
        const float value = params.curvature == CarveCurvature::Convex  ? k :
                            params.curvature == CarveCurvature::Concave ? -k :
                                                                          std::abs(k);
        const float s = soft_step(value, params.curvature_threshold, params.curvature_softness);
        m *= params.curvature_invert ? 1.0f - s : s;
      }
      if ((params.facing_top || params.facing_sides || params.facing_bottom) && m > 0.0f) {
        /* Angle to the nearest way that is on, so several ways blend without seams. */
        const float z = std::clamp(normals[i].z, -1.0f, 1.0f);
        float angle = float(M_PI);
        if (params.facing_top) {
          angle = std::min(angle, std::acos(z));
        }
        if (params.facing_bottom) {
          angle = std::min(angle, std::acos(-z));
        }
        if (params.facing_sides) {
          angle = std::min(angle, std::asin(std::abs(z)));
        }
        const float s = 1.0f - soft_step(angle, params.facing_angle, params.facing_softness);
        m *= params.facing_invert ? 1.0f - s : s;
      }
      mask[i] = m;
    }
  });

  assign_materials(mesh, params, a_values, b_values, mask);

  /* How far each point moves. */
  Array<float> offset(verts_num);
  threading::parallel_for(offset.index_range(), 4096, [&](const IndexRange range) {
    for (const int i : range) {
      float d = (height[i] - params.midlevel) * params.strength * mask[i];
      if (params.max_depth > 0.0f) {
        d = std::clamp(d, -params.max_depth, params.max_depth);
      }
      offset[i] = d;
    }
  });
  if (params.smooth > 0) {
    smooth_values(offset, neighbors, params.smooth);
  }

  float3 axis(0.0f);
  switch (params.direction) {
    case CarveDirection::X:
      axis = float3(1.0f, 0.0f, 0.0f);
      break;
    case CarveDirection::Y:
      axis = float3(0.0f, 1.0f, 0.0f);
      break;
    case CarveDirection::Z:
      axis = float3(0.0f, 0.0f, 1.0f);
      break;
    case CarveDirection::Custom:
      axis = math::normalize(params.custom_direction);
      break;
    case CarveDirection::Normal:
      break;
  }
  threading::parallel_for(positions.index_range(), 4096, [&](const IndexRange range) {
    for (const int i : range) {
      const float3 dir = params.direction == CarveDirection::Normal ? normals[i] : axis;
      positions[i] += dir * offset[i];
    }
  });
  mesh.tag_positions_changed();

  bke::MutableAttributeAccessor attributes = mesh.attributes_for_write();
  auto write = [&](const std::optional<std::string> &id, const Span<float> values) {
    if (!id || values.is_empty()) {
      return;
    }
    bke::SpanAttributeWriter<float> writer = attributes.lookup_or_add_for_write_only_span<float>(
        *id, bke::AttrDomain::Point);
    if (writer) {
      writer.span.copy_from(values);
      writer.finish();
    }
  };
  write(params.height_id, height);
  write(params.mask_id, mask);
  write(params.curvature_id, curvature);
  return true;
}

/** \} */

}  // namespace blender::geometry
