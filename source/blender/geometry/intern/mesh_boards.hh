/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

/** \file
 * \ingroup geo
 *
 * Shared by Planks and Roof: boards laid in rows or patterns on flat areas made of faces, and
 * the mesh they are written into.
 *
 * A board is a convex outline in the 2D frame of an area, given a thickness along the area's
 * normal. The outline is cut to the faces of the area, so a board that crosses an edge between
 * two faces of the same plane stays one solid piece, and a board that runs past the outline of
 * the area is cut square at it.
 */

#include <optional>
#include <string>

#include "BLI_map.hh"
#include "BLI_math_vector_types.hh"
#include "BLI_span.hh"
#include "BLI_string_ref.hh"
#include "BLI_vector.hh"

namespace blender {
struct Mesh;
}

namespace blender::geometry::boards {

using Polygon = Vector<float2, 8>;

/* -------------------------------------------------------------------- */
/** \name 2D Polygons
 * \{ */

float cross2(const float2 &a, const float2 &b);
float signed_area(Span<float2> poly);
float2 centroid(Span<float2> poly);
bool is_convex(Span<float2> poly);
bool point_inside(Span<float2> poly, const float2 &p);
/** The part of \a subject inside \a clipper, which must be convex and counter-clockwise. */
Polygon clip(Span<float2> subject, Span<float2> clipper);
/** Move every edge of a convex counter-clockwise polygon inward by \a distance, or outward when
 * it is negative. Empty when the polygon would turn inside out. */
Polygon offset_convex(Span<float2> poly, float distance);
Polygon rect(float x0, float y0, float x1, float y1);
/** A rectangle along \a axis around \a center, counter-clockwise. */
Polygon oriented_rect(const float2 &center, const float2 &axis, float length, float width);
float2 rotate(const float2 &p, float angle);

/** \} */

/* -------------------------------------------------------------------- */
/** \name Surfaces
 * \{ */

/** Faces sharing vertices, as polygons of vertex indices. */
struct Surface {
  Vector<float3> positions;
  Vector<Vector<int, 8>> faces;
  /** Per face: material index and index of the face in the original mesh, or -1. */
  Vector<int> face_materials;
  Vector<int> face_origins;

  /** Add a vertex, or find one already at this position. */
  int vert(const float3 &position);
  int add_face(Span<int> verts, int material = 0, int origin = -1);
  int add_face_positions(Span<float3> positions, int material = 0, int origin = -1);

  float3 face_normal(int face) const;
  float3 face_center(int face) const;

 private:
  struct VertKey {
    int64_t x, y, z;
    uint64_t hash() const
    {
      return uint64_t(x) * 73856093u ^ uint64_t(y) * 19349663u ^ uint64_t(z) * 83492791u;
    }
    friend bool operator==(const VertKey &a, const VertKey &b)
    {
      return a.x == b.x && a.y == b.y && a.z == b.z;
    }
  };
  Map<VertKey, int> lookup_;
};

/** The faces of \a mesh in \a selection (all when empty). */
Surface surface_from_mesh(const Mesh &mesh, Span<bool> selection);

/** Groups of connected faces of a surface that are in the same plane. */
Vector<Vector<int>> coplanar_groups(const Surface &surface, Span<int> faces);

/** Loops of the edges of a group of faces that are used by only one face of the group. They
 * follow the winding of the faces, so outer loops turn counter-clockwise around the normal. */
Vector<Vector<int>> boundary_loops(const Surface &surface, Span<int> faces);

/** \} */

/* -------------------------------------------------------------------- */
/** \name Areas
 * \{ */

/** A flat area: faces in one plane, in a 2D frame. A point (x, y) at height z above the area is
 * at origin + u * x + v * y + n * z. */
struct Area {
  float3 origin;
  float3 u, v, n;
  /** Counter-clockwise outlines of the faces. */
  Vector<Polygon> faces;
  Vector<float2> face_min, face_max;
  /** The outline of the whole area, outer loops counter-clockwise and holes clockwise. */
  Vector<Polygon> loops;
  float2 box_min, box_max;

  float3 to_3d(const float2 &q, float z) const;
  float2 to_2d(const float3 &p) const;
  void add_face(Polygon poly);
  bool contains(const float2 &p) const;
  /** The parts of the line at height \a y (or at \a y along X when \a vertical) inside the area,
   * as pairs of start and end. */
  Vector<float2> intervals(float y, bool vertical) const;
};

/** The parts of a line inside \a loops: the line at height \a c, or at \a c along X when
 * \a vertical. Pairs of start and end, sorted. */
Vector<float2> line_intervals(Span<Polygon> loops, float c, bool vertical);

/** An area for a group of coplanar faces, with \a u turned into the plane and \a angle added. */
Area area_from_faces(const Surface &surface, Span<int> faces, const float3 &u, float angle);
/** Level rows on walls, rows along X on floors and ceilings. */
float3 auto_row_direction(const float3 &normal);
/** Along the face, level: the rows of a roof. Falls back to #auto_row_direction on flat faces. */
float3 contour_direction(const float3 &normal);

/** \} */

/* -------------------------------------------------------------------- */
/** \name Mesh Output
 * \{ */

struct Builder {
  Vector<float3> positions;
  /** Per point: the board it belongs to and a random value of the board. */
  Vector<int> vert_board;
  Vector<float> vert_random;
  Vector<int> face_sizes;
  Vector<int> face_materials;
  Vector<int> face_parts;
  Vector<bool> face_smooth;
  Vector<int> corner_verts;
  Vector<float2> corner_uvs;
  int boards_num = 0;
  /** Board and random value given to new points. */
  int current_board = 0;
  float current_random = 0.0f;
  /** Above this many boards, nothing more is added. */
  int max_boards = 500000;
  bool too_many = false;

  /** Start a new piece; its points get a new board index. False when there are too many. */
  bool begin_board(float random);
  int add_vert(const float3 &position);
  void add_face(Span<int> verts, Span<float2> uvs, int material, int part, bool smooth = false);
  /** Rotate the points from \a first_vert on around \a axis through \a pivot. */
  void rotate_verts(int first_vert, const float3 &pivot, const float3 &axis, float angle);
};

struct OutputNames {
  std::optional<std::string> board_index;
  std::optional<std::string> random;
  std::optional<std::string> part;
  /** UV map of the new faces. */
  std::string uv_map = "UVMap";
};

/** The mesh of everything added to \a builder, or null when it is empty. */
Mesh *builder_to_mesh(const Builder &builder, const OutputNames &names);
/** Join \a base (may be null) and \a added (may be null). Points and faces of \a base get -1, 0
 * and -1 in the board index, random and part attributes. */
Mesh *join_meshes(const Mesh *base, Mesh *added, const OutputNames &names);

/** \} */

/* -------------------------------------------------------------------- */
/** \name Boards
 * \{ */

struct Board {
  /** Convex and counter-clockwise, in the frame of the area. */
  Polygon outline;
  /** Unit direction of the length of the board. */
  float2 axis = float2(1.0f, 0.0f);
  uint32_t id = 0;
  /** Height of the bottom over the area, and thickness. */
  float z0 = 0.0f;
  float thickness = 0.02f;
  /** Extra height of the edge on the right of the axis, rising to none at the other edge, so
   * overlapping rows lie on each other. */
  float lean = 0.0f;
  int material = 0;
  int part = 0;
};

/** Random changes per board. All 0 for none. */
struct Variation {
  /** Largest random move along the normal. */
  float height = 0.0f;
  /** Largest random tilt, in radians. */
  float tilt = 0.0f;
  /** How much the long edges rise over the middle. */
  float cup = 0.0f;
  /** How much the middle rises over the ends. */
  float bow = 0.0f;
  /** 0 to 1: share of boards left out. */
  float missing = 0.0f;
};

/** Add the solid of a board, cut to the faces of the area. */
void build_board(Builder &builder, const Area &area, const Board &board, const Variation &variation);

enum class Layout {
  Rows = 0,
  Herringbone = 1,
  Chevron = 2,
  Basket = 3,
};

enum class Joints {
  Random = 0,
  Half = 1,
  Third = 2,
  /** One board per row. */
  None = 3,
};

/** How an area is covered with boards. Rows run along X of the area. */
struct Fill {
  Layout layout = Layout::Rows;
  /** Size of a board along and across the rows. */
  float length = 1.0f;
  float width = 0.1f;
  float gap = 0.0f;
  float thickness = 0.02f;
  float z0 = 0.0f;
  /** 0 to 1. */
  float random_length = 0.0f;
  /** Shortest piece at the ends of a row. */
  float min_length = 0.0f;
  Joints joints = Joints::Random;
  /** Each row covers this much of the one before it, and leans on it. */
  float overlap = 0.0f;
  float lean = 0.0f;
  /** Start the rows at the low side of the area instead of at 0, so the first row is whole. */
  bool rows_from_min = false;
  /** A ring of boards along the outline. Only when the area has one convex outline. */
  bool border = false;
  /** Strips over the joints between the rows, on top of the boards. 0 for none. */
  float batten_width = 0.0f;
  float batten_thickness = 0.0f;
  /** Materials are picked at random from this many slots, starting at #material. */
  int material = 0;
  int material_count = 1;
  int part = 0;
  uint32_t seed = 0;
};

/** \return False when a border was asked for but the area has no single convex outline. */
bool fill_area(Builder &builder, const Area &area, const Fill &fill, const Variation &variation);

/** Beams across the rows of a fill, under the area: joists or studs. */
void add_supports(Builder &builder,
                  const Area &area,
                  float spacing,
                  float width,
                  float depth,
                  int material,
                  int part,
                  uint32_t seed);

/** A straight solid along \a start to \a end with the cross-section \a profile (offsets from the
 * line, in either winding). */
void add_prism(Builder &builder,
               Span<float3> profile,
               const float3 &start,
               const float3 &end,
               int material,
               int part,
               bool smooth = false);

/** \} */

}  // namespace blender::geometry::boards
