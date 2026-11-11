/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup geo
 *
 * Carve wall patterns made from a laid-out set of stone outlines.
 *
 * Fitted walls start from rows of bricks. Their corners are shaken, with each corner shared by
 * the stones that meet there, so stones always fit their neighbors. Big stones are split along
 * Voronoi cells. Packed walls are built one stone at a time: each stone is dropped onto the wall
 * where it lands lowest, rests stably and leaves the fewest gaps, and the holes left at the end
 * are filled with small stones.
 *
 * Either way the result is a tile of outlines that repeats. Each point then finds the stones
 * around it and wears them away: wider mortar at corners, varying along the edges, with rough
 * outlines.
 */

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <tuple>

#include "BLI_array.hh"
#include "BLI_math_base.h"
#include "BLI_math_base.hh"
#include "BLI_math_vector.hh"
#include "BLI_noise.hh"
#include "BLI_rand.hh"
#include "BLI_allocator.hh"
#include "BLI_vector.hh"

#include "GEO_mesh_carve.hh"

namespace blender::geometry {

namespace carve_layout {

/* -------------------------------------------------------------------- */
/** \name Helpers
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

/** Noise around 0, from about -0.5 to 0.5. */
static float fbm(const float x, const float y, const float channel, const int octaves)
{
  float sum = 0.0f;
  float weight = 1.0f;
  float total = 0.0f;
  float f = 1.0f;
  for (int o = 0; o < octaves; o++) {
    sum += (value_noise(x * f + 17.3f * float(o), y * f - 9.1f * float(o), channel) - 0.5f) *
           weight;
    total += weight;
    weight *= 0.5f;
    f *= 2.03f;
  }
  return sum / total;
}

/* Plain allocations: layouts are cached until Blender quits. */
template<typename T> using Vec = Vector<T, 4, RawAllocator>;

using Poly = Vec<float2>;

static float poly_area(const Span<float2> p)
{
  float a = 0.0f;
  for (const int i : p.index_range()) {
    const float2 &u = p[i];
    const float2 &v = p[(i + 1) % p.size()];
    a += u.x * v.y - v.x * u.y;
  }
  return 0.5f * a;
}

static bool point_in(const Span<float2> poly, const float2 q)
{
  bool inside = false;
  const int n = poly.size();
  for (int i = 0; i < n; i++) {
    const float2 &a = poly[i];
    const float2 &b = poly[(i + 1) % n];
    if ((a.y > q.y) != (b.y > q.y)) {
      if (q.x < (b.x - a.x) * (q.y - a.y) / (b.y - a.y) + a.x) {
        inside = !inside;
      }
    }
  }
  return inside;
}

/** The part of the polygon with dot(normal, p) <= c. Empty if less than three points remain. */
static Poly clip_halfplane(const Span<float2> poly, const float2 normal, const float c)
{
  Poly out;
  const int n = poly.size();
  for (int i = 0; i < n; i++) {
    const float2 p = poly[i];
    const float2 q = poly[(i + 1) % n];
    const float dp = math::dot(normal, p) - c;
    const float dq = math::dot(normal, q) - c;
    if (dp <= 0.0f) {
      out.append(p);
    }
    if ((dp <= 0.0f) != (dq <= 0.0f)) {
      const float t = dp / (dp - dq);
      out.append(p + t * (q - p));
    }
  }
  if (out.size() < 3) {
    out.clear();
  }
  return out;
}

static Poly convex_hull(Poly p)
{
  std::sort(p.begin(), p.end(), [](const float2 &a, const float2 &b) {
    return a.x < b.x || (a.x == b.x && a.y < b.y);
  });
  auto cross = [](const float2 &o, const float2 &a, const float2 &b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
  };
  Poly h;
  for (const float2 &q : p) {
    while (h.size() >= 2 && cross(h[h.size() - 2], h.last(), q) <= 0.0f) {
      h.remove_last();
    }
    h.append(q);
  }
  const int lower = h.size();
  for (int i = int(p.size()) - 2; i >= 0; i--) {
    const float2 &q = p[i];
    while (int(h.size()) > lower && cross(h[h.size() - 2], h.last(), q) <= 0.0f) {
      h.remove_last();
    }
    h.append(q);
  }
  h.remove_last();
  return h;
}

struct Rng {
  RandomNumberGenerator rng;
  explicit Rng(const uint32_t seed) : rng(seed) {}
  /** 0 to 1. */
  float f()
  {
    return rng.get_float();
  }
  /** -1 to 1. */
  float s()
  {
    return rng.get_float() * 2.0f - 1.0f;
  }
  float range(const float a, const float b)
  {
    return a + (b - a) * rng.get_float();
  }
  /** a to b, both included. */
  int irange(const int a, const int b)
  {
    return a + rng.get_int32(b - a + 1);
  }
  float normal()
  {
    const float u1 = std::max(rng.get_float(), 1e-7f);
    const float u2 = rng.get_float();
    return std::sqrt(-2.0f * std::log(u1)) * std::cos(2.0f * float(M_PI) * u2);
  }
};

/** \} */

/* -------------------------------------------------------------------- */
/** \name Layout
 * \{ */

struct Stone {
  Poly pts;
  /** How sharp each corner is, 0 for a straight one. */
  Vec<float> sharp;
  float2 lo, hi, center, half;
  float id = 0.0f, erode = 1.0f, corner = 1.0f;
};

struct Bin {
  int stone;
  float2 shift;
};

struct Layout {
  Vec<Stone> stones;
  float2 tile = float2(1.0f);
  int nx = 1, ny = 1;
  Vec<Vec<Bin>> bins;
  /* How the stones are worn, in pattern units. */
  float mortar = 0.01f, corner = 0.01f, rough = 0.003f, bevel = 0.03f, unit = 0.4f;
};

static void add_stone(Layout &layout, Poly pts, Rng &rng)
{
  if (pts.size() < 3) {
    return;
  }
  if (poly_area(pts) < 0.0f) {
    std::reverse(pts.begin(), pts.end());
  }
  Stone s;
  const int n = pts.size();
  s.sharp.resize(n);
  for (int i = 0; i < n; i++) {
    const float2 v = pts[i];
    const float2 e1 = math::normalize(v - pts[(i + n - 1) % n]);
    const float2 e2 = math::normalize(pts[(i + 1) % n] - v);
    const float turn = std::atan2(e1.x * e2.y - e1.y * e2.x, math::dot(e1, e2));
    const float sharp = std::clamp(turn / (float(M_PI) * 0.5f), 0.0f, 1.6f);
    s.sharp[i] = sharp < 0.15f ? 0.0f : sharp;
  }
  s.lo = pts[0];
  s.hi = pts[0];
  for (const float2 &p : pts) {
    s.lo = math::min(s.lo, p);
    s.hi = math::max(s.hi, p);
  }
  s.center = (s.lo + s.hi) * 0.5f;
  s.half = math::max((s.hi - s.lo) * 0.5f, float2(1e-5f));
  s.pts = std::move(pts);
  s.id = rng.f();
  s.erode = rng.range(0.7f, 1.3f);
  s.corner = rng.range(0.6f, 1.4f);
  layout.stones.append(std::move(s));
}

/** Puts each stone into the bins it covers, with the shift that brings its copy there. */
static void bin_stones(Layout &layout, const int nx, const int ny)
{
  layout.nx = std::max(nx, 1);
  layout.ny = std::max(ny, 1);
  layout.bins.clear();
  layout.bins.resize(layout.nx * layout.ny);
  const float2 cell(layout.tile.x / layout.nx, layout.tile.y / layout.ny);
  const float margin = layout.rough * 3.0f + 1e-4f;
  for (const int si : layout.stones.index_range()) {
    const Stone &s = layout.stones[si];
    const int x0 = int(std::floor((s.lo.x - margin) / cell.x));
    const int x1 = int(std::floor((s.hi.x + margin) / cell.x));
    const int y0 = int(std::floor((s.lo.y - margin) / cell.y));
    const int y1 = int(std::floor((s.hi.y + margin) / cell.y));
    for (int iy = y0; iy <= y1; iy++) {
      const int wy = ((iy % layout.ny) + layout.ny) % layout.ny;
      const float sy = float((wy - iy) / layout.ny) * layout.tile.y;
      for (int ix = x0; ix <= x1; ix++) {
        const int wx = ((ix % layout.nx) + layout.nx) % layout.nx;
        const float sx = float((wx - ix) / layout.nx) * layout.tile.x;
        layout.bins[wy * layout.nx + wx].append({si, float2(sx, sy)});
      }
    }
  }
}

/** Splits a big polygon into Voronoi pieces around points inside it. */
static void voronoi_split(const Poly &poly, Rng &rng, const float area_threshold, Vec<Poly> &r)
{
  const float area = std::abs(poly_area(poly));
  if (area_threshold <= 0.0f || area < area_threshold * rng.range(0.8f, 1.5f)) {
    r.append(poly);
    return;
  }
  const int k = std::clamp(int(std::round(area / area_threshold * rng.range(0.7f, 1.3f))), 2, 6);
  float2 lo = poly[0], hi = poly[0];
  for (const float2 &p : poly) {
    lo = math::min(lo, p);
    hi = math::max(hi, p);
  }
  const float min_d = std::sqrt(area / float(k)) * 0.8f;
  Vec<float2> seeds;
  for (int tries = 0; tries < 400 && int(seeds.size()) < k; tries++) {
    const float2 s(rng.range(lo.x, hi.x), rng.range(lo.y, hi.y));
    if (!point_in(poly, s)) {
      continue;
    }
    bool close = false;
    for (const float2 &t : seeds) {
      close |= math::distance(s, t) < min_d;
    }
    if (!close) {
      seeds.append(s);
    }
  }
  if (seeds.size() < 2) {
    r.append(poly);
    return;
  }
  for (const int i : seeds.index_range()) {
    Poly piece = poly;
    for (const int j : seeds.index_range()) {
      if (i == j || piece.is_empty()) {
        continue;
      }
      const float2 si = seeds[i];
      const float2 sj = seeds[j];
      piece = clip_halfplane(piece, sj - si, (math::dot(sj, sj) - math::dot(si, si)) * 0.5f);
    }
    if (!piece.is_empty()) {
      r.append(std::move(piece));
    }
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Fitted Walls
 * \{ */

struct FittedStyle {
  float L, Hc, jl, jh, shake, extra, wave, vor, mortar, corner, rough, bevel, sieve;
  int cols, rows;
};

static FittedStyle fitted_style(const CarvePattern pattern)
{
  switch (pattern) {
    case CarvePattern::FittedRubble:
      return {0.34f, 0.26f, 0.5f, 0.35f, 0.32f, 0.5f, 0.15f, 0.05f, 0.010f, 0.030f, 0.006f,
              0.03f, 0.0015f, 26, 18};
    case CarvePattern::SquaredStone:
      return {0.46f, 0.22f, 0.3f, 0.08f, 0.05f, 0.1f, 0.05f, 0.0f, 0.006f, 0.008f, 0.0015f,
              0.012f, 0.001f, 22, 18};
    case CarvePattern::MixedRubble:
      return {0.5f, 0.34f, 0.55f, 0.3f, 0.3f, 0.5f, 0.1f, 0.03f, 0.011f, 0.025f, 0.005f, 0.03f,
              0.0004f, 20, 14};
    case CarvePattern::WornStone:
      return {0.42f, 0.22f, 0.4f, 0.2f, 0.18f, 0.3f, 0.15f, 0.05f, 0.007f, 0.045f, 0.003f, 0.05f,
              0.001f, 22, 18};
    case CarvePattern::ThinCourses:
      return {0.6f, 0.11f, 0.55f, 0.35f, 0.18f, 0.4f, 0.25f, 0.0f, 0.005f, 0.012f, 0.003f,
              0.015f, 0.0005f, 16, 36};
    case CarvePattern::RoughCourses:
    default:
      return {0.42f, 0.20f, 0.45f, 0.2f, 0.2f, 0.35f, 0.2f, 0.06f, 0.008f, 0.020f, 0.004f,
              0.025f, 0.001f, 22, 22};
  }
}

struct LineNode {
  float ox;
  float2 pos;
};

static std::unique_ptr<Layout> build_fitted(FittedStyle st,
                                            const uint32_t seed,
                                            const float lenf,
                                            const float rowf,
                                            const float varf)
{
  Rng rng(seed);
  st.L *= lenf;
  st.Hc *= rowf;
  st.jl = std::min(st.jl * varf, 0.8f);
  st.jh = std::min(st.jh * varf, 0.6f);
  st.shake = std::min(st.shake * varf, 0.45f);
  st.wave *= varf;
  const float area_f = lenf * rowf;
  st.vor *= area_f;
  st.sieve *= area_f;
  const float L = st.L;
  const float Hc = st.Hc;
  const int nrows = st.rows;
  const int ncols = st.cols;

  Array<float> heights(nrows);
  float Ht = 0.0f;
  for (const int j : heights.index_range()) {
    heights[j] = Hc * (1.0f + st.jh * rng.s());
    Ht += heights[j];
  }
  Array<float> line_y(nrows);
  {
    float y = 0.0f;
    for (int j = 0; j < nrows; j++) {
      line_y[j] = y;
      y += heights[j];
    }
  }
  const float Wt = float(ncols) * L;

  /* Head joints of each course, wrapping around the tile. */
  Array<Vec<float>> joints(nrows);
  for (int j = 0; j < nrows; j++) {
    Array<float> lens(ncols);
    float sum = 0.0f;
    for (float &l : lens) {
      l = L * (1.0f + st.jl * rng.s());
      sum += l;
    }
    float x = rng.f() * Wt;
    for (const float l : lens) {
      joints[j].append(std::fmod(x, Wt));
      x += l * Wt / sum;
    }
    std::sort(joints[j].begin(), joints[j].end());
  }

  /* Nodes on each bed line: the joints of the courses below and above, plus extra ones. */
  Array<Vec<LineNode>> lines(nrows);
  for (int li = 0; li < nrows; li++) {
    Vec<float> keys;
    keys.extend(joints[li]);
    keys.extend(joints[(li + nrows - 1) % nrows]);
    std::sort(keys.begin(), keys.end());
    {
      const int n = keys.size();
      Vec<float> extra;
      for (int i = 0; i < n; i++) {
        const float a = keys[i];
        const float b = i + 1 < n ? keys[i + 1] : keys[0] + Wt;
        if (b - a > 0.25f * L && rng.f() < st.extra) {
          extra.append(std::fmod(a + (b - a) * rng.range(0.3f, 0.7f), Wt));
        }
      }
      keys.extend(extra);
      std::sort(keys.begin(), keys.end());
    }
    const int n = keys.size();
    const float ph = rng.f() * 2.0f * float(M_PI);
    const float waves = std::max(1.0f, std::round(Wt / (L * 2.3f * 2.0f * float(M_PI))));
    const float room = 0.3f * std::min(heights[li], heights[(li + nrows - 1) % nrows]);
    for (int i = 0; i < n; i++) {
      const float ox = keys[i];
      const float gp = i > 0 ? ox - keys[i - 1] : ox + Wt - keys[n - 1];
      const float gn = i + 1 < n ? keys[i + 1] - ox : keys[0] + Wt - ox;
      const float lim = 0.45f * std::min(gp, gn);
      const float dx = std::clamp(st.shake * L * rng.s(), -lim, lim);
      const float wy = st.wave * Hc * std::sin(2.0f * float(M_PI) * waves * ox / Wt + ph);
      const float dy = std::clamp(wy + st.shake * Hc * 0.5f * rng.s(), -room, room);
      lines[li].append({ox, float2(ox + dx, line_y[li] + dy)});
    }
  }

  auto find = [&](const Vec<LineNode> &line, const float ox) {
    int best = 0;
    float bd = 1e30f;
    for (const int i : line.index_range()) {
      const float d = std::abs(line[i].ox - ox);
      if (d < bd) {
        bd = d;
        best = i;
      }
    }
    return best;
  };
  /* The nodes of a line from key a to key b, going right and wrapping around. */
  auto walk = [&](const Vec<LineNode> &line, const float a, const float b, const float yshift) {
    Vec<float2> out;
    const int n = line.size();
    const int ia = find(line, a);
    const int ib = find(line, b);
    float shift = 0.0f;
    int i = ia;
    out.append(line[i].pos + float2(0.0f, yshift));
    for (int guard = 0; guard < n; guard++) {
      i++;
      if (i == n) {
        i = 0;
        shift += Wt;
      }
      out.append(line[i].pos + float2(shift, yshift));
      if (i == ib) {
        break;
      }
    }
    return out;
  };

  auto layout = std::make_unique<Layout>();
  layout->tile = float2(Wt, Ht);
  const float u = std::sqrt(area_f);
  layout->unit = std::min(L, Hc * 2.0f);
  layout->rough = st.rough * u;
  layout->bevel = st.bevel * u;
  layout->corner = st.corner * u;
  layout->mortar = st.mortar;

  for (int j = 0; j < nrows; j++) {
    const Vec<float> &js = joints[j];
    const int n = js.size();
    /* A point halfway up some head joints, so they bend. */
    Array<float> mid_t(n, -1.0f), mid_dx(n, 0.0f);
    for (int i = 0; i < n; i++) {
      if (rng.f() < st.extra) {
        mid_t[i] = rng.range(0.35f, 0.65f);
        mid_dx[i] = st.shake * L * 0.6f * rng.s();
      }
    }
    const int jt = (j + 1) % nrows;
    const float top_shift = j + 1 == nrows ? Ht : 0.0f;
    for (int i = 0; i < n; i++) {
      const float a = js[i];
      const float b = js[(i + 1) % n];
      Vec<float2> bottom = walk(lines[j], a, b, 0.0f);
      Vec<float2> top = walk(lines[jt], a, b, top_shift);
      if (bottom.size() < 2 || top.size() < 2) {
        continue;
      }
      /* Line up the top with the bottom when one wrapped and the other did not. */
      const float dx0 = top[0].x - bottom[0].x;
      if (std::abs(dx0) > Wt * 0.5f) {
        const float fix = dx0 > 0.0f ? -Wt : Wt;
        for (float2 &p : top) {
          p.x += fix;
        }
      }
      Poly poly;
      poly.extend(bottom);
      const int ib = (i + 1) % n;
      if (mid_t[ib] >= 0.0f) {
        const float2 p0 = bottom.last();
        const float2 p1 = top.last();
        poly.append(p0 + mid_t[ib] * (p1 - p0) + float2(mid_dx[ib], 0.0f));
      }
      for (int k = int(top.size()) - 1; k >= 0; k--) {
        poly.append(top[k]);
      }
      if (mid_t[i] >= 0.0f) {
        const float2 p0 = bottom[0];
        const float2 p1 = top[0];
        poly.append(p0 + mid_t[i] * (p1 - p0) + float2(mid_dx[i], 0.0f));
      }
      Vec<Poly> pieces;
      voronoi_split(poly, rng, st.vor, pieces);
      for (Poly &piece : pieces) {
        if (std::abs(poly_area(piece)) >= st.sieve) {
          add_stone(*layout, std::move(piece), rng);
        }
      }
    }
  }
  bin_stones(*layout, ncols * 2, nrows * 2);
  return layout;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Packed Walls
 * \{ */

enum class Shape : int8_t { Round, Angular, Blocky, Slab, Wedge, Upright };

struct PackedStyle {
  /** Shapes and how often each is picked. */
  Vec<std::pair<Shape, float>> shapes;
  float size = 0.13f, size_var = 0.35f;
  float aspect_lo = 1.1f, aspect_hi = 2.2f;
  int facets_lo = 1, facets_hi = 3;
  float wobble = 0.14f;
  float tilt = 10.0f;
  int tries = 10;
  float w_gap = 2.5f, w_side = 0.5f, w_match = 0.12f;
  int fillers = 250, filler_min = 4;
  Shape filler = Shape::Angular;
};

static PackedStyle packed_style(const CarvePattern pattern)
{
  PackedStyle s;
  switch (pattern) {
    case CarvePattern::PackedRound:
      s.shapes = {{Shape::Round, 1.0f}};
      break;
    case CarvePattern::PackedAngular:
      s.shapes = {{Shape::Angular, 1.0f}};
      s.aspect_hi = 2.4f;
      break;
    case CarvePattern::PackedBlocks:
      s.shapes = {{Shape::Blocky, 1.0f}};
      s.aspect_lo = 1.3f, s.aspect_hi = 2.6f, s.tilt = 4.0f, s.w_match = 0.3f, s.size = 0.12f;
      break;
    case CarvePattern::PackedSlabs:
      s.shapes = {{Shape::Slab, 1.0f}};
      s.aspect_lo = 3.0f, s.aspect_hi = 6.0f, s.tilt = 3.0f, s.w_match = 0.35f;
      s.size = 0.11f, s.w_gap = 3.0f;
      break;
    case CarvePattern::PackedWedges:
      s.shapes = {{Shape::Wedge, 1.0f}};
      s.aspect_lo = 1.2f, s.aspect_hi = 2.4f, s.tilt = 6.0f;
      break;
    case CarvePattern::PackedMixed:
      s.shapes = {{Shape::Angular, 3.0f},
                  {Shape::Blocky, 2.0f},
                  {Shape::Slab, 1.0f},
                  {Shape::Wedge, 2.0f},
                  {Shape::Round, 1.0f}};
      s.aspect_lo = 1.2f, s.aspect_hi = 3.0f, s.tilt = 6.0f, s.size = 0.14f, s.size_var = 0.45f;
      break;
    case CarvePattern::DryWall:
    default:
      s.shapes = {{Shape::Blocky, 4.0f},
                  {Shape::Slab, 3.0f},
                  {Shape::Wedge, 2.0f},
                  {Shape::Upright, 1.0f},
                  {Shape::Angular, 1.0f}};
      s.aspect_lo = 1.4f, s.aspect_hi = 3.5f, s.tilt = 3.0f, s.size = 0.12f, s.size_var = 0.45f;
      s.tries = 14, s.w_gap = 3.0f, s.w_side = 0.7f, s.w_match = 0.3f, s.filler = Shape::Wedge;
      break;
  }
  return s;
}

static Poly chip(const Poly &pts, float2 normal, const float c)
{
  normal = math::normalize(normal);
  Poly q = clip_halfplane(pts, normal, c);
  return q.is_empty() ? pts : q;
}

/** A stone outline about 2 units across. */
static Poly shape_unit(Rng &rng, const Shape shape, const PackedStyle &st)
{
  switch (shape) {
    case Shape::Round: {
      const int n = 56;
      Poly pts;
      float amp[3], ph[3];
      for (int k = 0; k < 3; k++) {
        amp[k] = rng.normal() * st.wobble / std::pow(float(k + 2), 1.3f);
        ph[k] = rng.f() * 2.0f * float(M_PI);
      }
      for (int i = 0; i < n; i++) {
        const float th = float(i) / float(n) * 2.0f * float(M_PI);
        float r = 1.0f;
        for (int k = 0; k < 3; k++) {
          r += amp[k] * std::cos(float(k + 2) * th + ph[k]);
        }
        pts.append(float2(std::cos(th), std::sin(th)) * r);
      }
      pts = convex_hull(pts);
      const int facets = rng.irange(std::max(st.facets_lo, 1), st.facets_hi + 1);
      for (int i = 0; i < facets; i++) {
        const float a = rng.f() * 2.0f * float(M_PI);
        Poly q = clip_halfplane(pts, float2(std::cos(a), std::sin(a)), rng.range(0.62f, 0.92f));
        if (q.size() >= 5) {
          pts = std::move(q);
        }
      }
      return pts;
    }
    case Shape::Angular: {
      const int k = rng.irange(5, 8);
      Vec<float> angles;
      for (int i = 0; i < k; i++) {
        angles.append((float(i) + rng.range(-0.35f, 0.35f)) / float(k) * 2.0f * float(M_PI));
      }
      std::sort(angles.begin(), angles.end());
      Poly pts;
      for (const float a : angles) {
        pts.append(float2(std::cos(a), std::sin(a)) * rng.range(0.75f, 1.05f));
      }
      pts = convex_hull(pts);
      const int chips = rng.irange(0, 2);
      for (int i = 0; i < chips; i++) {
        const float t = rng.f() * 2.0f * float(M_PI);
        pts = chip(pts, float2(std::cos(t), std::sin(t)), rng.range(0.7f, 0.95f));
      }
      return pts;
    }
    case Shape::Blocky:
    case Shape::Slab:
    case Shape::Wedge:
    case Shape::Upright: {
      Poly pts;
      const float2 corners[4] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
      for (const float2 &c : corners) {
        pts.append(c + float2(rng.range(-0.07f, 0.07f), rng.range(-0.07f, 0.07f)));
      }
      for (const float sx : {-1.0f, 1.0f}) {
        for (const float sy : {-1.0f, 1.0f}) {
          if (rng.f() < 0.7f) {
            pts = chip(pts, float2(sx, sy * rng.range(0.6f, 1.6f)), rng.range(1.05f, 1.32f));
          }
        }
      }
      if (shape == Shape::Slab) {
        for (const float sx : {-1.0f, 1.0f}) {
          pts = chip(pts, float2(sx, rng.range(-0.7f, 0.7f)), rng.range(0.82f, 0.97f));
        }
      }
      if (shape == Shape::Wedge) {
        const float sx = rng.f() < 0.5f ? -1.0f : 1.0f;
        const float sy = rng.f() < 0.5f ? -1.0f : 1.0f;
        pts = chip(pts, float2(sx * rng.range(0.5f, 1.5f), sy), rng.range(0.3f, 0.75f));
      }
      if (rng.f() < 0.5f) {
        const float t = rng.f() * 2.0f * float(M_PI);
        pts = chip(pts, float2(std::cos(t), std::sin(t)), rng.range(0.85f, 1.0f));
      }
      return pts;
    }
  }
  return {};
}

static Poly stone_shape(Rng &rng, const float size, const float aspect, Shape shape,
                        const PackedStyle &st)
{
  Poly pts = shape_unit(rng, shape, st);
  float s = std::sqrt(aspect);
  if (shape == Shape::Upright) {
    s = 1.0f / s;
  }
  for (float2 &p : pts) {
    p.x *= size * s;
    p.y *= size / s;
  }
  return pts;
}

static Shape pick_shape(Rng &rng, const PackedStyle &st)
{
  float total = 0.0f;
  for (const auto &[shape, w] : st.shapes) {
    total += w;
  }
  float r = rng.f() * total;
  for (const auto &[shape, w] : st.shapes) {
    if (r < w) {
      return shape;
    }
    r -= w;
  }
  return st.shapes.last().first;
}

static Poly rotate(const Span<float2> pts, const float a)
{
  const float c = std::cos(a);
  const float s = std::sin(a);
  Poly out;
  for (const float2 &p : pts) {
    out.append(float2(p.x * c - p.y * s, p.x * s + p.y * c));
  }
  return out;
}

/** A stone outline drawn into cells, with what placing needs. */
struct Pose {
  Poly pts;
  float2 mn;
  int w = 0, h = 0;
  Vec<uint8_t> mask;
  Vec<int> bottom, top;
  Vec<int> rows, left, right;
  int cx = 0;
  int area = 0;
  bool ok = false;
};

static Pose make_pose(Poly pts, const float cell)
{
  Pose pose;
  float2 mn = pts[0], mx = pts[0];
  for (const float2 &p : pts) {
    mn = math::min(mn, p);
    mx = math::max(mx, p);
  }
  int w = int(std::ceil((mx.x - mn.x) / cell)) + 1;
  int h = int(std::ceil((mx.y - mn.y) / cell)) + 1;
  Vec<uint8_t> m(w * h, 0);
  int c0 = w, c1 = -1, r0 = h, r1 = -1;
  for (int r = 0; r < h; r++) {
    for (int c = 0; c < w; c++) {
      const float2 q = mn + (float2(float(c), float(r)) + 0.5f) * cell;
      if (point_in(pts, q)) {
        m[r * w + c] = 1;
        c0 = std::min(c0, c);
        c1 = std::max(c1, c);
        r0 = std::min(r0, r);
        r1 = std::max(r1, r);
      }
    }
  }
  if (c1 < 0) {
    return pose;
  }
  /* Crop to the filled cells. */
  pose.w = c1 - c0 + 1;
  pose.h = r1 - r0 + 1;
  pose.mask.resize(pose.w * pose.h);
  for (int r = 0; r < pose.h; r++) {
    for (int c = 0; c < pose.w; c++) {
      pose.mask[r * pose.w + c] = m[(r + r0) * w + c + c0];
    }
  }
  pose.mn = mn + float2(float(c0), float(r0)) * cell;
  pose.pts = std::move(pts);
  w = pose.w;
  h = pose.h;
  pose.bottom.resize(w);
  pose.top.resize(w);
  long sx = 0;
  for (int c = 0; c < w; c++) {
    int lo = h, hi = -1;
    for (int r = 0; r < h; r++) {
      if (pose.mask[r * w + c]) {
        lo = std::min(lo, r);
        hi = std::max(hi, r);
        pose.area++;
        sx += c;
      }
    }
    if (hi < 0) {
      return pose;
    }
    pose.bottom[c] = lo;
    pose.top[c] = hi + 1;
  }
  for (int r = 0; r < h; r++) {
    int lo = -1, hi = -1;
    for (int c = 0; c < w; c++) {
      if (pose.mask[r * w + c]) {
        if (lo < 0) {
          lo = c;
        }
        hi = c;
      }
    }
    if (lo >= 0) {
      pose.rows.append(r);
      pose.left.append(lo);
      pose.right.append(hi);
    }
  }
  pose.cx = int(std::round(float(sx) / float(std::max(pose.area, 1))));
  pose.ok = pose.area > 3;
  return pose;
}

static std::unique_ptr<Layout> build_packed(PackedStyle st,
                                            const uint32_t seed,
                                            const float lenf,
                                            const float varf)
{
  Rng rng(seed);
  st.size *= lenf;
  st.size_var = std::min(st.size_var * varf, 0.9f);
  st.tilt *= varf;
  const float cell = st.size / 8.0f;
  const int W = 320;
  const int Hgrid = 200;
  const int Htot = Hgrid + 64;
  const int GAP = 1;
  const float Wt = float(W) * cell;
  const float Ht = float(Hgrid) * cell;

  Vec<uint8_t> occ(W * Htot, 0);
  Vec<Poly> placed;

  auto wrap = [&](const int c) { return ((c % W) + W) % W; };

  /* The wall starts on a wavy floor. Stones that reach past the top of the tile come back in at
   * the bottom, under the floor, so the tile repeats upward without a level joint. */
  Array<int> floor(W);
  {
    const float k1 = float(rng.irange(2, 4));
    const float k2 = float(rng.irange(5, 9));
    const float p1 = rng.f() * 2.0f * float(M_PI);
    const float p2 = rng.f() * 2.0f * float(M_PI);
    for (int c = 0; c < W; c++) {
      const float t = float(c) / float(W) * 2.0f * float(M_PI);
      const float v = 0.6f * std::sin(k1 * t + p1) + 0.4f * std::sin(k2 * t + p2);
      floor[c] = int(std::round(6.0f + 6.0f * v));
    }
  }
  Vec<int> surf(W);
  for (int c = 0; c < W; c++) {
    surf[c] = floor[c];
  }

  auto make_stone = [&]() {
    const float size = std::clamp(std::exp(rng.normal() * st.size_var), 0.45f, 2.2f) * st.size;
    const float aspect = rng.range(st.aspect_lo, st.aspect_hi);
    return stone_shape(rng, size, aspect, pick_shape(rng, st), st);
  };

  auto stamp = [&](const Pose &pose, const int x, const int y) {
    for (int r = 0; r < pose.h; r++) {
      for (int c = 0; c < pose.w; c++) {
        if (!pose.mask[r * pose.w + c]) {
          continue;
        }
        for (int dy = -GAP; dy <= GAP; dy++) {
          const int rr = y + r + dy;
          for (int dx = -GAP; dx <= GAP; dx++) {
            const int cc = wrap(x + c + dx);
            /* Mark the cell and its copies a tile above and below. */
            for (const int k : {rr - Hgrid, rr, rr + Hgrid}) {
              if (k >= 0 && k < Htot) {
                occ[k * W + cc] = 1;
              }
            }
            if (rr >= 0 && rr < Htot) {
              surf[cc] = std::max(surf[cc], rr + 1);
            }
          }
        }
      }
    }
    Poly world;
    for (const float2 &p : pose.pts) {
      world.append(p - pose.mn + float2(float(x), float(y)) * cell);
    }
    placed.append(std::move(world));
  };

  Vec<Poly> pool;
  for (int i = 0; i < 24; i++) {
    pool.append(make_stone());
  }
  Array<int> Ld(W * Htot), Rd(W * Htot);
  const int cap = 10;

  for (int step = 0; step < 3000; step++) {
    bool full = true;
    for (int c = 0; c < W; c++) {
      full &= surf[c] >= Hgrid + floor[c];
    }
    if (full) {
      break;
    }
    /* Free cells to the nearest stone on the left and right of each cell. */
    for (int r = 0; r < Htot; r++) {
      int run = cap;
      for (int k = 0; k < 2 * W; k++) {
        const int c = k % W;
        run = occ[r * W + c] ? 0 : std::min(run + 1, cap);
        if (k >= W) {
          Ld[r * W + c] = run;
        }
      }
      run = cap;
      for (int k = 2 * W - 1; k >= 0; k--) {
        const int c = k % W;
        run = occ[r * W + c] ? 0 : std::min(run + 1, cap);
        if (k < W) {
          Rd[r * W + c] = run;
        }
      }
    }

    float best_score = 1e30f;
    Pose best_pose;
    int best_x = 0, best_y = 0, best_pool = -1;
    for (int t = 0; t < st.tries; t++) {
      const int pi = rng.irange(0, int(pool.size()) - 1);
      for (const float a : {0.0f, float(M_PI)}) {
        for (const float tilt : {0.0f, -st.tilt, st.tilt}) {
          for (const bool mirror : {false, true}) {
            Poly q = rotate(pool[pi], a + tilt * float(M_PI) / 180.0f);
            if (mirror) {
              for (float2 &p : q) {
                p.x = -p.x;
              }
            }
            Pose pose = make_pose(std::move(q), cell);
            if (!pose.ok || pose.w >= W || pose.h >= Htot - Hgrid) {
              continue;
            }
            const int w = pose.w;
            for (int x = 0; x < W; x += (w > 16 ? 2 : 1)) {
              int land = 0;
              for (int c = 0; c < w; c++) {
                land = std::max(land, surf[wrap(x + c)] - pose.bottom[c]);
              }
              if (land + pose.h >= Htot) {
                continue;
              }
              /* Near the top, the stone has to fit under the stones of the next tile. */
              if (land + pose.h + GAP > Hgrid) {
                bool hit = false;
                for (int r = std::max(Hgrid - land, 0); r < pose.h && !hit; r++) {
                  for (int c = 0; c < w; c++) {
                    if (pose.mask[r * w + c] && occ[(land + r) * W + wrap(x + c)]) {
                      hit = true;
                      break;
                    }
                  }
                }
                if (hit) {
                  continue;
                }
              }
              int gaps = 0, lft = -1, rgt = -1;
              for (int c = 0; c < w; c++) {
                const int under = land + pose.bottom[c] - surf[wrap(x + c)];
                gaps += under;
                if (under <= 1) {
                  if (lft < 0) {
                    lft = c;
                  }
                  rgt = c;
                }
              }
              const bool stable = (lft >= 0 && lft <= pose.cx && rgt >= pose.cx);
              if (!stable) {
                continue;
              }
              float side = 0.0f;
              for (const int k : pose.rows.index_range()) {
                const int rr = std::min(land + pose.rows[k], Htot - 1);
                side += Ld[rr * W + wrap(x + pose.left[k] - 1)] +
                        Rd[rr * W + wrap(x + pose.right[k] + 1)];
              }
              side /= float(std::max(int(pose.rows.size()), 1));
              const int top_l = land + pose.top[0];
              const int top_r = land + pose.top[w - 1];
              const float match = float(std::min(std::abs(top_l - surf[wrap(x - 1)]), 12) +
                                        std::min(std::abs(top_r - surf[wrap(x + w)]), 12));
              const float score = float(land) + float(gaps) / float(w) * st.w_gap +
                                  side * st.w_side + match * st.w_match + rng.f() * 0.5f;
              if (score < best_score) {
                best_score = score;
                best_pose = pose;
                best_x = x;
                best_y = land;
                best_pool = pi;
              }
            }
          }
        }
      }
    }
    if (best_pool < 0) {
      break;
    }
    stamp(best_pose, best_x, best_y);
    pool[best_pool] = make_stone();
  }

  /* Fill holes with small stones, the biggest hole first. */
  Array<int> dist(W * Hgrid);
  Array<uint8_t> cur(W * Hgrid), nxt(W * Hgrid);
  for (int f = 0; f < st.fillers; f++) {
    for (int i = 0; i < W * Hgrid; i++) {
      cur[i] = !occ[i];
      dist[i] = 0;
    }
    bool any = true;
    while (any) {
      any = false;
      for (int r = 0; r < Hgrid; r++) {
        for (int c = 0; c < W; c++) {
          const int i = r * W + c;
          if (!cur[i]) {
            nxt[i] = 0;
            continue;
          }
          dist[i]++;
          any = true;
          nxt[i] = cur[((r + Hgrid - 1) % Hgrid) * W + c] && cur[((r + 1) % Hgrid) * W + c] &&
                   cur[r * W + wrap(c - 1)] && cur[r * W + wrap(c + 1)];
        }
      }
      std::swap(cur, nxt);
    }
    int bi = -1, bd = 0;
    for (int r = 0; r < Hgrid; r++) {
      for (int c = 0; c < W; c++) {
        if (dist[r * W + c] > bd) {
          bd = dist[r * W + c];
          bi = r * W + c;
        }
      }
    }
    if (bi < 0 || bd < st.filler_min) {
      break;
    }
    const float rad = (float(bd) - 1.0f) * cell;
    Poly pts = stone_shape(rng, rad * 1.15f, rng.range(1.0f, 1.6f), st.filler, st);
    pts = rotate(pts, rng.f() * float(M_PI));
    Pose pose = make_pose(std::move(pts), cell);
    if (!pose.ok) {
      continue;
    }
    stamp(pose, bi % W - pose.w / 2, bi / W - pose.h / 2);
  }

  auto layout = std::make_unique<Layout>();
  layout->tile = float2(Wt, Ht);
  layout->unit = st.size * 3.0f;
  layout->mortar = 0.003f;
  layout->corner = 0.009f * lenf;
  layout->rough = 0.0035f * lenf;
  layout->bevel = 0.03f * lenf;
  for (Poly &poly : placed) {
    add_stone(*layout, std::move(poly), rng);
  }
  bin_stones(*layout, W / 8, Hgrid / 8);
  return layout;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Cache
 * \{ */

static bool is_packed(const CarvePattern pattern)
{
  switch (pattern) {
    case CarvePattern::PackedRound:
    case CarvePattern::PackedAngular:
    case CarvePattern::PackedBlocks:
    case CarvePattern::PackedSlabs:
    case CarvePattern::PackedWedges:
    case CarvePattern::PackedMixed:
    case CarvePattern::DryWall:
      return true;
    default:
      return false;
  }
}

using Key = std::tuple<int, int, int, int, int>;

static std::shared_ptr<const Layout> get_layout(const CarvePatternParams &params)
{
  const float lenf = std::max(params.stone_length, 1e-3f) / 0.8f;
  const float rowf = std::max(params.row_height, 1e-3f) / 0.25f;
  const float varf = std::clamp(params.variation, 0.0f, 1.0f) / 0.5f;
  const bool packed = is_packed(params.pattern);
  const Key key(int(params.pattern),
                params.seed,
                int(std::round(lenf * 1000.0f)),
                packed ? 0 : int(std::round(rowf * 1000.0f)),
                int(std::round(varf * 1000.0f)));

  thread_local Key last_key(-1, 0, 0, 0, 0);
  thread_local std::shared_ptr<const Layout> last;
  if (last && key == last_key) {
    return last;
  }

  static std::mutex mutex;
  static std::map<Key, std::shared_ptr<const Layout>> cache;
  std::lock_guard lock(mutex);
  auto it = cache.find(key);
  if (it == cache.end()) {
    if (cache.size() > 32) {
      cache.clear();
    }
    const uint32_t seed = noise::hash(uint32_t(params.pattern) + 7919u, uint32_t(params.seed));
    std::shared_ptr<const Layout> layout;
    if (packed) {
      layout = build_packed(packed_style(params.pattern), seed, lenf, varf);
    }
    else {
      layout = build_fitted(fitted_style(params.pattern), seed, lenf, rowf, varf);
    }
    it = cache.emplace(key, std::move(layout)).first;
  }
  last_key = key;
  last = it->second;
  return last;
}

/** \} */

}  // namespace carve_layout

/* -------------------------------------------------------------------- */
/** \name Evaluate
 * \{ */

float carve_layout_wall(const float2 p, const CarvePatternParams &params)
{
  using namespace carve_layout;
  const std::shared_ptr<const Layout> layout_ptr = get_layout(params);
  const Layout &layout = *layout_ptr;

  const float mortf = std::max(params.mortar, 0.0f) / 0.02f;
  const float roundf = std::clamp(params.roundness, 0.0f, 1.0f) / 0.5f;
  const float mortar = layout.mortar * mortf;
  const float corner = layout.corner * roundf;
  const float rough = layout.rough;
  const float bevel = std::max(layout.bevel * std::max(roundf, 0.2f), 1e-5f);
  const float unit = layout.unit;

  const float2 q(p.x - std::floor(p.x / layout.tile.x) * layout.tile.x,
                 p.y - std::floor(p.y / layout.tile.y) * layout.tile.y);
  const int bx = std::clamp(int(q.x / layout.tile.x * layout.nx), 0, layout.nx - 1);
  const int by = std::clamp(int(q.y / layout.tile.y * layout.ny), 0, layout.ny - 1);

  /* Noise is read at the point itself, so it does not repeat with the tile. */
  const float edge_noise = fbm(p.x / unit * 16.0f, p.y / unit * 16.0f, 81.0f, 4);
  const float joint_noise = fbm(p.x / unit * 2.5f, p.y / unit * 2.5f, 82.0f, 3);
  const float margin = rough * 3.0f;

  float best = 0.0f;
  for (const Bin &bin : layout.bins[by * layout.nx + bx]) {
    const Stone &s = layout.stones[bin.stone];
    const float2 x = q - bin.shift;
    if (x.x < s.lo.x - margin || x.x > s.hi.x + margin || x.y < s.lo.y - margin ||
        x.y > s.hi.y + margin)
    {
      continue;
    }
    const int n = s.pts.size();
    float dmin = 1e30f;
    bool inside = false;
    for (int i = 0; i < n; i++) {
      const float2 &a = s.pts[i];
      const float2 &b = s.pts[(i + 1) % n];
      const float2 ab = b - a;
      const float t = std::clamp(math::dot(x - a, ab) / std::max(math::dot(ab, ab), 1e-12f),
                                 0.0f,
                                 1.0f);
      dmin = std::min(dmin, math::length(x - (a + ab * t)));
      if ((a.y > x.y) != (b.y > x.y)) {
        if (x.x < ab.x * (x.y - a.y) / ab.y + a.x) {
          inside = !inside;
        }
      }
    }
    float inner = inside ? dmin : -dmin;
    if (inner < -margin) {
      continue;
    }
    /* Corners wear away more than straight sides, sharper ones most. */
    const float kk = corner * s.corner;
    if (kk > 0.0f) {
      const float rc = kk * 3.0f;
      float wear = 0.0f;
      for (int i = 0; i < n; i++) {
        if (s.sharp[i] == 0.0f) {
          continue;
        }
        const float r = math::distance(x, s.pts[i]) / rc;
        if (r < 1.0f) {
          wear = std::max(wear, kk * s.sharp[i] * math::square(1.0f - r));
        }
      }
      inner -= wear;
    }
    /* Chipped outlines, and joints that open and close along their length. */
    inner += rough * 3.2f * edge_noise;
    const float e = mortar * s.erode * (1.0f + 1.2f * joint_noise);
    if (inner <= e) {
      continue;
    }
    const float t = std::min((inner - e) / bevel, 1.0f);
    const float shoulder = std::sqrt(t * (2.0f - t));
    const float2 uv = (x - s.center) / s.half;
    float face = (1.0f - 0.12f * std::clamp(math::dot(uv, uv), 0.0f, 2.0f)) *
                 (1.0f - 0.3f * s.id);
    face += std::max(params.roughness, 0.0f) * 0.08f *
            fbm(p.x / unit * 6.0f, p.y / unit * 6.0f, 24.0f, 4);
    best = std::max(best, shoulder * std::clamp(face, 0.0f, 1.3f));
  }
  return std::clamp(best, 0.0f, 1.0f);
}

/** \} */

}  // namespace blender::geometry
