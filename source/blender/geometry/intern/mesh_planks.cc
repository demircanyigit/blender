/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup geo
 *
 * Planks: floors and walls of boards on the faces of a mesh, and crates and barrels.
 *
 * Floors and walls: connected faces in one plane are filled together, so boards run across the
 * edges between them and are cut square where the faces end. Holes in the faces become clean
 * openings.
 */

#include <cmath>
#include <numeric>

#include "DNA_mesh_types.h"

#include "BLI_array.hh"
#include "BLI_math_base.hh"
#include "BLI_math_vector.hh"
#include "BLI_noise.hh"
#include "BLI_utildefines.h"

#include "BKE_attribute.hh"
#include "BKE_lib_id.hh"
#include "BKE_mesh.hh"

#include "GEO_mesh_copy_selection.hh"
#include "GEO_mesh_planks.hh"

#include "mesh_boards.hh"

namespace blender::geometry {

namespace bd = boards;

namespace {

constexpr float PI_F = 3.14159265358979f;

bd::Variation variation_of(const MeshPlanksParams &params)
{
  bd::Variation variation;
  variation.height = std::max(params.random_height, 0.0f);
  variation.tilt = std::max(params.tilt, 0.0f);
  variation.cup = std::max(params.cup, 0.0f);
  variation.bow = std::max(params.bow, 0.0f);
  variation.missing = std::clamp(params.missing, 0.0f, 1.0f);
  return variation;
}

bd::Joints joints_of(const PlanksJoints joints)
{
  switch (joints) {
    case PlanksJoints::Random:
      return bd::Joints::Random;
    case PlanksJoints::Half:
      return bd::Joints::Half;
    case PlanksJoints::Third:
      return bd::Joints::Third;
  }
  return bd::Joints::Random;
}

bd::Layout layout_of(const PlanksPattern pattern)
{
  switch (pattern) {
    case PlanksPattern::Straight:
      return bd::Layout::Rows;
    case PlanksPattern::Herringbone:
      return bd::Layout::Herringbone;
    case PlanksPattern::Chevron:
      return bd::Layout::Chevron;
    case PlanksPattern::Basket:
      return bd::Layout::Basket;
  }
  return bd::Layout::Rows;
}

/** A fill with the board size, materials and seed of the parameters. */
bd::Fill base_fill(const MeshPlanksParams &params)
{
  bd::Fill fill;
  fill.width = std::max(params.width, 1e-4f);
  fill.length = std::max(params.length, 1e-4f);
  fill.gap = std::max(params.gap, 0.0f);
  fill.thickness = std::max(params.thickness, 1e-5f);
  fill.random_length = std::clamp(params.random_length, 0.0f, 1.0f);
  fill.min_length = std::max(params.min_length, 0.0f);
  fill.joints = joints_of(params.joints);
  fill.material = std::max(params.material, 0);
  fill.material_count = std::max(params.material_count, 1);
  fill.part = int(PlanksPart::Plank);
  fill.seed = uint32_t(params.seed);
  return fill;
}

/** An area that is one rectangle in the given frame. */
bd::Area rect_area(const float3 &origin,
                   const float3 &u,
                   const float3 &v,
                   const float3 &n,
                   const float2 &min,
                   const float2 &max)
{
  bd::Area area;
  area.origin = origin;
  area.u = u;
  area.v = v;
  area.n = n;
  bd::Polygon outline = bd::rect(min.x, min.y, max.x, max.y);
  area.loops.append(outline);
  area.add_face(std::move(outline));
  return area;
}

/* -------------------------------------------------------------------- */
/** \name Floors and Walls
 * \{ */

void build_floor_or_wall(bd::Builder &builder,
                         const Mesh &mesh,
                         const MeshPlanksParams &params,
                         std::string &r_message)
{
  const bd::Surface surface = bd::surface_from_mesh(mesh, params.selection);
  Vector<int> all(surface.faces.size());
  std::iota(all.begin(), all.end(), 0);
  const bool wall = params.type == PlanksType::Wall;
  const bool vertical = wall && ELEM(params.wall_style,
                                     PlanksWallStyle::Vertical,
                                     PlanksWallStyle::BoardAndBatten);
  const bd::Variation variation = variation_of(params);
  bool border_failed = false;

  for (const Span<int> group : bd::coplanar_groups(surface, all)) {
    if (builder.too_many) {
      break;
    }
    const float3 hint = bd::auto_row_direction(surface.face_normal(group[0]));
    const bd::Area area = bd::area_from_faces(
        surface, group, hint, params.angle + (vertical ? PI_F * 0.5f : 0.0f));

    bd::Fill fill = base_fill(params);
    if (params.material < 0) {
      fill.material = surface.face_materials[group[0]];
      fill.material_count = 1;
    }
    if (wall) {
      switch (params.wall_style) {
        case PlanksWallStyle::Horizontal:
        case PlanksWallStyle::Vertical:
          break;
        case PlanksWallStyle::Lap:
          fill.overlap = std::clamp(params.lap, 0.0f, fill.width * 0.9f);
          fill.lean = fill.thickness;
          fill.rows_from_min = true;
          break;
        case PlanksWallStyle::BoardAndBatten:
          fill.batten_width = std::max(params.batten_width, 0.0f);
          fill.batten_thickness = fill.thickness;
          break;
      }
    }
    else {
      fill.layout = layout_of(params.pattern);
      fill.border = params.border;
    }
    if (!bd::fill_area(builder, area, fill, variation)) {
      border_failed = true;
    }
    if (params.supports) {
      bd::add_supports(builder,
                       area,
                       params.support_spacing,
                       params.support_width,
                       params.support_depth,
                       fill.material,
                       int(PlanksPart::Frame),
                       uint32_t(params.seed));
    }
  }
  if (border_failed) {
    r_message = "The border needs faces with one convex outline";
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Crate
 * \{ */

void build_crate(bd::Builder &builder, const MeshPlanksParams &params)
{
  const float t = std::max(params.thickness, 1e-4f);
  const float w = std::max(params.width, 1e-4f);
  const float3 size = math::max(params.crate_size, float3(t * 3.0f));
  const float hx = size.x * 0.5f;
  const float hy = size.y * 0.5f;
  const bool has_lid = params.lid != PlanksLid::None;
  const float side_height = has_lid ? size.z - t : size.z;
  const bd::Variation variation = variation_of(params);

  bd::Fill fill = base_fill(params);
  fill.joints = bd::Joints::None;
  fill.rows_from_min = true;
  fill.gap = params.slatted ? std::max(params.slat_gap, 0.0f) : std::max(params.gap, 0.0f);
  fill.z0 = -t;
  fill.thickness = t;

  struct Side {
    float3 origin, u, v, n;
    /** Half the width of the boards and of the frame. */
    float half, frame_half;
  };
  const Side sides[4] = {
      {float3(0, -hy, 0), float3(1, 0, 0), float3(0, 0, 1), float3(0, -1, 0), hx, hx},
      {float3(0, hy, 0), float3(-1, 0, 0), float3(0, 0, 1), float3(0, 1, 0), hx, hx},
      {float3(hx, 0, 0), float3(0, 1, 0), float3(0, 0, 1), float3(1, 0, 0), hy - t, hy + t},
      {float3(-hx, 0, 0), float3(0, -1, 0), float3(0, 0, 1), float3(-1, 0, 0), hy - t, hy + t},
  };

  auto frame_board = [&](const bd::Area &area, bd::Polygon outline, const float2 &axis, int k) {
    bd::Board board;
    board.outline = std::move(outline);
    board.axis = axis;
    board.id = noise::hash(uint32_t(k), uint32_t(params.seed), 6007u);
    board.z0 = 0.0f;
    board.thickness = t;
    board.material = fill.material;
    board.part = int(PlanksPart::Frame);
    bd::Variation frame_variation = variation;
    frame_variation.missing = 0.0f;
    bd::build_board(builder, area, board, frame_variation);
  };

  for (const int i : IndexRange(4)) {
    const Side &side = sides[i];
    fill.seed = uint32_t(params.seed) * 16u + uint32_t(i);
    bd::fill_area(builder,
                  rect_area(side.origin,
                            side.u,
                            side.v,
                            side.n,
                            float2(-side.half, 0.0f),
                            float2(side.half, side_height)),
                  fill,
                  variation);

    const float x0 = -side.frame_half;
    const float x1 = side.frame_half;
    const float y0 = 0.0f;
    const float y1 = side_height;
    const bd::Area frame_area = rect_area(
        side.origin, side.u, side.v, side.n, float2(x0, y0), float2(x1, y1));
    const bool rails = params.frame || params.brace == PlanksBrace::Z;
    if (rails) {
      frame_board(frame_area, bd::rect(x0, y0, x1, y0 + w), float2(1, 0), i * 8 + 0);
      frame_board(frame_area, bd::rect(x0, y1 - w, x1, y1), float2(1, 0), i * 8 + 1);
    }
    if (params.frame && !params.corner_posts) {
      frame_board(frame_area, bd::rect(x0, y0 + w, x0 + w, y1 - w), float2(0, 1), i * 8 + 2);
      frame_board(frame_area, bd::rect(x1 - w, y0 + w, x1, y1 - w), float2(0, 1), i * 8 + 3);
    }
    if (params.brace != PlanksBrace::None) {
      const float inset_x = params.frame ? w : 0.0f;
      const float inset_y = rails ? w : 0.0f;
      const bd::Polygon inside = bd::rect(x0 + inset_x, y0 + inset_y, x1 - inset_x, y1 - inset_y);
      const float2 from(x0 + inset_x, y0 + inset_y);
      const float2 to(x1 - inset_x, y1 - inset_y);
      float2 d = to - from;
      if (d.x > w && d.y > w) {
        float2 center = (from + to) * 0.5f;
        if (i & 1) {
          d.x = -d.x;
        }
        const float2 axis = math::normalize(d);
        const bd::Polygon diagonal = bd::clip(
            bd::oriented_rect(center, axis, math::length(d) + w * 4.0f, w), inside);
        frame_board(frame_area, diagonal, axis, i * 8 + 4);
      }
    }
  }

  /* Bottom, between the sides. */
  fill.seed = uint32_t(params.seed) * 16u + 4u;
  bd::fill_area(builder,
                rect_area(float3(0.0f),
                          float3(1, 0, 0),
                          float3(0, -1, 0),
                          float3(0, 0, -1),
                          float2(-(hx - t), -(hy - t)),
                          float2(hx - t, hy - t)),
                fill,
                variation);

  if (params.corner_posts) {
    const float pw = std::max(params.post_width, t);
    const bd::Area ground = rect_area(float3(0.0f),
                                      float3(1, 0, 0),
                                      float3(0, 1, 0),
                                      float3(0, 0, 1),
                                      float2(-hx - pw - t, -hy - pw - t),
                                      float2(hx + pw + t, hy + pw + t));
    for (const int k : IndexRange(4)) {
      const float sx = (k & 1) ? 1.0f : -1.0f;
      const float sy = (k & 2) ? 1.0f : -1.0f;
      const float ax = hx - (pw - t);
      const float bx = hx + t;
      const float ay = hy - (pw - t);
      const float by = hy + t;
      const float px0 = sx > 0.0f ? ax : -bx;
      const float px1 = sx > 0.0f ? bx : -ax;
      const float py0 = sy > 0.0f ? ay : -by;
      const float py1 = sy > 0.0f ? by : -ay;
      bd::Board post;
      post.outline = bd::rect(px0, py0, px1, py1);
      post.axis = float2(1, 0);
      post.id = noise::hash(uint32_t(k), uint32_t(params.seed), 7011u);
      post.thickness = side_height;
      post.material = fill.material;
      post.part = int(PlanksPart::Frame);
      bd::build_board(builder, ground, post, {});
    }
  }

  if (has_lid) {
    const int first_vert = builder.positions.size();
    fill.seed = uint32_t(params.seed) * 16u + 5u;
    bd::fill_area(builder,
                  rect_area(float3(0, 0, size.z),
                            float3(1, 0, 0),
                            float3(0, 1, 0),
                            float3(0, 0, 1),
                            float2(-hx, -hy),
                            float2(hx, hy)),
                  fill,
                  variation);
    if (params.lid == PlanksLid::Open && params.lid_angle != 0.0f) {
      builder.rotate_verts(
          first_vert, float3(0.0f, hy, size.z), float3(1, 0, 0), -params.lid_angle);
    }
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Barrel
 * \{ */

void build_barrel(bd::Builder &builder, const MeshPlanksParams &params)
{
  const float height = std::max(params.barrel_height, 1e-3f);
  const float radius = std::max(params.barrel_radius, 1e-3f);
  const float bulge = params.bulge;
  const float t = std::clamp(params.thickness, 1e-4f, radius * 0.5f);
  const int staves = std::max(params.staves, 3);
  const float step = 2.0f * PI_F / float(staves);
  const float gap_angle = std::min(std::max(params.gap, 0.0f) * 0.5f / radius, step * 0.25f);
  const float jitter_max = std::max(params.random_height, 0.0f);
  const float missing = std::clamp(params.missing, 0.0f, 1.0f);
  const int base_material = std::max(params.material, 0);
  const int material_count = std::max(params.material_count, 1);
  const int metal = params.metal_material >= 0 ? params.metal_material : base_material;
  auto radius_at = [&](const float z) {
    const float s = std::clamp(z / height, 0.0f, 1.0f) * 2.0f - 1.0f;
    return radius + bulge * (1.0f - s * s);
  };

  /* Staves. */
  constexpr int rows = 16;
  constexpr int columns = 3;
  constexpr int ring = columns + 1;
  for (const int i : IndexRange(staves)) {
    if (builder.too_many) {
      return;
    }
    const uint32_t id = noise::hash(uint32_t(i), uint32_t(params.seed), 3001u);
    if (missing > 0.0f && noise::hash_to_float(id, 1) < missing) {
      continue;
    }
    if (!builder.begin_board(noise::hash_to_float(id, 2))) {
      return;
    }
    const float dr = (noise::hash_to_float(id, 3) * 2.0f - 1.0f) * jitter_max;
    const float a0 = float(i) * step + gap_angle;
    const float a1 = float(i + 1) * step - gap_angle;
    const int material = base_material +
                         std::min(int(noise::hash_to_float(id, 20) * float(material_count)),
                                  material_count - 1);
    const float uv_offset = noise::hash_to_float(id, 8) * 2.0f;
    const int first = builder.positions.size();
    for (const bool outer : {true, false}) {
      for (const int k : IndexRange(rows + 1)) {
        const float z = height * float(k) / float(rows);
        const float r = radius_at(z) + dr - (outer ? 0.0f : t);
        for (const int c : IndexRange(ring)) {
          const float a = a0 + (a1 - a0) * float(c) / float(columns);
          builder.add_vert(float3(r * std::cos(a), r * std::sin(a), z));
        }
      }
    }
    auto o = [&](const int k, const int c) { return first + k * ring + c; };
    auto in = [&](const int k, const int c) { return first + (rows + 1) * ring + k * ring + c; };
    auto arc = [&](const int c) {
      return (a1 - a0) * float(c) / float(columns) * radius + uv_offset;
    };
    auto z_of = [&](const int k) { return height * float(k) / float(rows); };
    for (const int k : IndexRange(rows)) {
      for (const int c : IndexRange(columns)) {
        const int outer_face[4] = {o(k, c), o(k, c + 1), o(k + 1, c + 1), o(k + 1, c)};
        const float2 outer_uvs[4] = {float2(arc(c), z_of(k)),
                                     float2(arc(c + 1), z_of(k)),
                                     float2(arc(c + 1), z_of(k + 1)),
                                     float2(arc(c), z_of(k + 1))};
        builder.add_face(
            Span<int>(outer_face, 4), Span<float2>(outer_uvs, 4), material, 0, true);
        const int inner_face[4] = {in(k, c), in(k + 1, c), in(k + 1, c + 1), in(k, c + 1)};
        const float2 inner_uvs[4] = {float2(arc(c), z_of(k)),
                                     float2(arc(c), z_of(k + 1)),
                                     float2(arc(c + 1), z_of(k + 1)),
                                     float2(arc(c + 1), z_of(k))};
        builder.add_face(
            Span<int>(inner_face, 4), Span<float2>(inner_uvs, 4), material, 0, true);
      }
      const float2 side_uvs[4] = {
          float2(z_of(k), 0.0f), float2(z_of(k + 1), 0.0f), float2(z_of(k + 1), t), float2(z_of(k), t)};
      const int start_side[4] = {o(k, 0), o(k + 1, 0), in(k + 1, 0), in(k, 0)};
      builder.add_face(Span<int>(start_side, 4), Span<float2>(side_uvs, 4), material, 0);
      const int end_side[4] = {o(k, columns), in(k, columns), in(k + 1, columns), o(k + 1, columns)};
      builder.add_face(Span<int>(end_side, 4), Span<float2>(side_uvs, 4), material, 0);
    }
    for (const int c : IndexRange(columns)) {
      const float2 cap_uvs[4] = {
          float2(arc(c), 0.0f), float2(arc(c), t), float2(arc(c + 1), t), float2(arc(c + 1), 0.0f)};
      const int bottom[4] = {o(0, c), in(0, c), in(0, c + 1), o(0, c + 1)};
      builder.add_face(Span<int>(bottom, 4), Span<float2>(cap_uvs, 4), material, 0);
      const int top[4] = {o(rows, c), o(rows, c + 1), in(rows, c + 1), in(rows, c)};
      builder.add_face(Span<int>(top, 4), Span<float2>(cap_uvs, 4), material, 0);
    }
  }

  /* The boards of the top and bottom, set into the staves. */
  if (params.barrel_ends != PlanksBarrelEnds::None) {
    bd::Fill fill = base_fill(params);
    fill.joints = bd::Joints::None;
    fill.thickness = t;
    const float chime = std::clamp(params.chime, 0.0f, height * 0.25f);
    Vector<float, 2> heights = {chime};
    if (params.barrel_ends == PlanksBarrelEnds::Both) {
      heights.append(height - chime - t);
    }
    for (const int h : heights.index_range()) {
      const float z = heights[h];
      const float head_radius = radius_at(z + t * 0.5f) - t * 0.5f;
      bd::Polygon circle;
      constexpr int segments = 48;
      for (const int s : IndexRange(segments)) {
        const float a = 2.0f * PI_F * float(s) / float(segments);
        circle.append(float2(std::cos(a), std::sin(a)) * head_radius);
      }
      bd::Area area;
      area.origin = float3(0.0f, 0.0f, z);
      area.u = float3(1, 0, 0);
      area.v = float3(0, 1, 0);
      area.n = float3(0, 0, 1);
      area.loops.append(circle);
      area.add_face(std::move(circle));
      fill.seed = uint32_t(params.seed) * 8u + uint32_t(h);
      bd::fill_area(builder, area, fill, {});
    }
  }

  /* Hoops. */
  const int hoops = std::max(params.hoops, 0);
  const float hoop_width = std::max(params.hoop_width, 1e-4f);
  const float hoop_thickness = std::max(params.hoop_thickness, 1e-5f);
  const float offset = std::clamp(params.hoop_offset, 0.0f, 0.5f);
  constexpr int segments = 64;
  for (const int i : IndexRange(hoops)) {
    if (!builder.begin_board(0.0f)) {
      return;
    }
    const float center = hoops == 1 ? height * 0.5f :
                                      height * (offset + (1.0f - offset * 2.0f) * float(i) /
                                                             float(hoops - 1));
    const float z0 = std::max(center - hoop_width * 0.5f, 0.0f);
    const float z1 = std::min(center + hoop_width * 0.5f, height);
    const int first = builder.positions.size();
    /* Inner bottom, outer bottom, outer top, inner top. */
    const float zs[4] = {z0, z0, z1, z1};
    const float extra[4] = {0.0f, hoop_thickness, hoop_thickness, 0.0f};
    for (const int r : IndexRange(4)) {
      const float rr = radius_at(zs[r]) + jitter_max + extra[r];
      for (const int s : IndexRange(segments)) {
        const float a = 2.0f * PI_F * float(s) / float(segments);
        builder.add_vert(float3(rr * std::cos(a), rr * std::sin(a), zs[r]));
      }
    }
    auto at = [&](const int r, const int s) { return first + r * segments + (s % segments); };
    const float circumference = 2.0f * PI_F * radius;
    for (const int s : IndexRange(segments)) {
      const float u0 = circumference * float(s) / float(segments);
      const float u1 = circumference * float(s + 1) / float(segments);
      const float2 uvs[4] = {float2(u0, 0.0f),
                             float2(u1, 0.0f),
                             float2(u1, hoop_width),
                             float2(u0, hoop_width)};
      const int outer[4] = {at(1, s), at(1, s + 1), at(2, s + 1), at(2, s)};
      builder.add_face(Span<int>(outer, 4), Span<float2>(uvs, 4), metal, int(PlanksPart::Metal), true);
      const int inner[4] = {at(0, s), at(3, s), at(3, s + 1), at(0, s + 1)};
      builder.add_face(Span<int>(inner, 4), Span<float2>(uvs, 4), metal, int(PlanksPart::Metal), true);
      const int top[4] = {at(2, s), at(2, s + 1), at(3, s + 1), at(3, s)};
      builder.add_face(Span<int>(top, 4), Span<float2>(uvs, 4), metal, int(PlanksPart::Metal));
      const int bottom[4] = {at(1, s), at(0, s), at(0, s + 1), at(1, s + 1)};
      builder.add_face(Span<int>(bottom, 4), Span<float2>(uvs, 4), metal, int(PlanksPart::Metal));
    }
  }
}

/** \} */

}  // namespace

Mesh *mesh_planks(const Mesh *mesh, const MeshPlanksParams &params, std::string &r_message)
{
  bd::Builder builder;
  bd::OutputNames names;
  names.board_index = params.board_index_id;
  names.random = params.random_id;
  names.part = params.part_id;

  const bool on_faces = ELEM(params.type, PlanksType::Floor, PlanksType::Wall);
  if (on_faces) {
    if (mesh == nullptr || mesh->faces_num == 0) {
      return nullptr;
    }
    const StringRef uv_name = mesh->active_or_default_uv_map_name();
    if (!uv_name.is_empty()) {
      names.uv_map = std::string(uv_name);
    }
    build_floor_or_wall(builder, *mesh, params, r_message);
  }
  else if (params.type == PlanksType::Crate) {
    build_crate(builder, params);
  }
  else {
    build_barrel(builder, params);
  }
  if (builder.too_many) {
    r_message = "Too many boards, only some were made. Make the boards bigger";
  }

  Mesh *boards_mesh = bd::builder_to_mesh(builder, names);
  if (!on_faces) {
    return boards_mesh;
  }
  if (boards_mesh == nullptr) {
    return nullptr;
  }
  if (params.keep_surface) {
    return bd::join_meshes(mesh, boards_mesh, names);
  }
  /* Without the surface: only the faces that got no boards stay. */
  if (params.selection.is_empty()) {
    return boards_mesh;
  }
  Array<bool> keep(mesh->faces_num);
  bool any = false;
  for (const int i : keep.index_range()) {
    keep[i] = !params.selection[i];
    any |= keep[i];
  }
  if (!any) {
    return boards_mesh;
  }
  const std::optional<Mesh *> kept = mesh_copy_selection(
      *mesh, VArray<bool>::from_span(keep), bke::AttrDomain::Face);
  if (!kept || *kept == nullptr) {
    return boards_mesh;
  }
  Mesh *result = bd::join_meshes(*kept, boards_mesh, names);
  BKE_id_free(nullptr, *kept);
  return result;
}

}  // namespace blender::geometry
