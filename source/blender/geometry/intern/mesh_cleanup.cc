/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup geo
 */

#include <algorithm>
#include <cmath>
#include <cstdarg>

#include "DNA_mesh_types.h"

#include "BLI_array.hh"
#include "BLI_bounds_types.hh"
#include "BLI_disjoint_set.hh"
#include "BLI_map.hh"
#include "BLI_math_vector.hh"
#include "BLI_vector.hh"

#include "BKE_customdata.hh"
#include "BKE_mesh.hh"

#include "bmesh.hh"

#include "GEO_mesh_cleanup.hh"

namespace blender::geometry {

/** Name of the temporary point layer that marks the points that may be cleaned up. */
static constexpr const char *scope_name = "__cleanup_scope";

struct Scope {
  /** Offset of the bool point layer, or -1 when everything may be cleaned up. */
  int offset = -1;

  bool vert(const BMVert *v) const
  {
    return offset == -1 || BM_ELEM_CD_GET_BOOL(v, offset);
  }
  bool edge(const BMEdge *e) const
  {
    return this->vert(e->v1) && this->vert(e->v2);
  }
  bool face(const BMFace *f) const
  {
    if (offset == -1) {
      return true;
    }
    const BMLoop *l_first = BM_FACE_FIRST_LOOP(f);
    const BMLoop *l_iter = l_first;
    do {
      if (!this->vert(l_iter->v)) {
        return false;
      }
    } while ((l_iter = l_iter->next) != l_first);
    return true;
  }
};

static void clear_tags(BMesh *bm)
{
  BM_mesh_elem_hflag_disable_all(bm, BM_VERT | BM_EDGE | BM_FACE, BM_ELEM_TAG, false);
}

/** Tag the points, edges and faces that may be cleaned up. */
static void tag_scope(BMesh *bm, const Scope &scope)
{
  BMIter iter;
  BMVert *v;
  BM_ITER_MESH (v, &iter, bm, BM_VERTS_OF_MESH) {
    BM_elem_flag_set(v, BM_ELEM_TAG, scope.vert(v));
  }
  BMEdge *e;
  BM_ITER_MESH (e, &iter, bm, BM_EDGES_OF_MESH) {
    BM_elem_flag_set(e, BM_ELEM_TAG, scope.edge(e));
  }
  BMFace *f;
  BM_ITER_MESH (f, &iter, bm, BM_FACES_OF_MESH) {
    BM_elem_flag_set(f, BM_ELEM_TAG, scope.face(f));
  }
}

static bool face_is_loose(const BMFace *f)
{
  const BMLoop *l_first = BM_FACE_FIRST_LOOP(f);
  const BMLoop *l_iter = l_first;
  do {
    if (!BM_edge_is_boundary(l_iter->e)) {
      return false;
    }
  } while ((l_iter = l_iter->next) != l_first);
  return true;
}

static void delete_loose(BMesh *bm, const Scope &scope, const MeshCleanupParams &params)
{
  BMIter iter;
  if (params.loose_faces) {
    clear_tags(bm);
    BMFace *f;
    BM_ITER_MESH (f, &iter, bm, BM_FACES_OF_MESH) {
      BM_elem_flag_set(f, BM_ELEM_TAG, scope.face(f) && face_is_loose(f));
    }
    BM_mesh_delete_hflag_context(bm, BM_ELEM_TAG, DEL_FACES);
  }
  if (params.loose_edges) {
    clear_tags(bm);
    BMEdge *e;
    BM_ITER_MESH (e, &iter, bm, BM_EDGES_OF_MESH) {
      BM_elem_flag_set(e, BM_ELEM_TAG, scope.edge(e) && BM_edge_is_wire(e));
    }
    BM_mesh_delete_hflag_context(bm, BM_ELEM_TAG, DEL_EDGES);
  }
  if (params.loose_verts) {
    clear_tags(bm);
    BMVert *v;
    BM_ITER_MESH (v, &iter, bm, BM_VERTS_OF_MESH) {
      BM_elem_flag_set(v, BM_ELEM_TAG, scope.vert(v) && v->e == nullptr);
    }
    BM_mesh_delete_hflag_context(bm, BM_ELEM_TAG, DEL_VERTS);
  }
}

static void delete_duplicate_faces(BMesh *bm, const Scope &scope)
{
  clear_tags(bm);
  BM_mesh_elem_index_ensure(bm, BM_VERT);
  Map<Vector<int>, BMFace *> first_faces;
  BMIter iter;
  BMFace *f;
  BM_ITER_MESH (f, &iter, bm, BM_FACES_OF_MESH) {
    if (!scope.face(f)) {
      continue;
    }
    Vector<int> key;
    const BMLoop *l_first = BM_FACE_FIRST_LOOP(f);
    const BMLoop *l_iter = l_first;
    do {
      key.append(BM_elem_index_get(l_iter->v));
    } while ((l_iter = l_iter->next) != l_first);
    std::sort(key.begin(), key.end());
    if (!first_faces.add(std::move(key), f)) {
      BM_elem_flag_enable(f, BM_ELEM_TAG);
    }
  }
  BM_mesh_delete_hflag_context(bm, BM_ELEM_TAG, DEL_FACES);
}

static void delete_interior_faces(BMesh *bm, const Scope &scope)
{
  clear_tags(bm);
  BMIter iter;
  BMFace *f;
  BM_ITER_MESH (f, &iter, bm, BM_FACES_OF_MESH) {
    if (!scope.face(f)) {
      continue;
    }
    bool interior = true;
    const BMLoop *l_first = BM_FACE_FIRST_LOOP(f);
    const BMLoop *l_iter = l_first;
    do {
      if (!BM_edge_face_count_is_over(l_iter->e, 2)) {
        interior = false;
        break;
      }
    } while ((l_iter = l_iter->next) != l_first);
    BM_elem_flag_set(f, BM_ELEM_TAG, interior);
  }
  BM_mesh_delete_hflag_context(bm, BM_ELEM_TAG, DEL_FACES);
}

static void run_op(BMesh *bm, const char *format, ...)
{
  va_list list;
  va_start(list, format);
  BMOperator op;
  if (BMO_op_vinitf(bm, &op, 0, format, list)) {
    BMO_op_exec(bm, &op);
    BMO_op_finish(bm, &op);
  }
  va_end(list);
  /* Errors from the operators can stay on the mesh, and aren't useful here. */
  BMO_error_clear(bm);
}

static void dissolve_extra_verts(BMesh *bm, const Scope &scope, const float angle)
{
  clear_tags(bm);
  BMIter iter;
  BMVert *v;
  bool any = false;
  BM_ITER_MESH (v, &iter, bm, BM_VERTS_OF_MESH) {
    if (!scope.vert(v) || !BM_vert_is_edge_pair(v)) {
      continue;
    }
    const BMVert *a = BM_edge_other_vert(v->e, v);
    const BMVert *b = BM_edge_other_vert(BM_DISK_EDGE_NEXT(v->e, v), v);
    const float3 da = float3(v->co) - float3(a->co);
    const float3 db = float3(b->co) - float3(v->co);
    if (math::length_squared(da) == 0.0f || math::length_squared(db) == 0.0f) {
      continue;
    }
    const float cos_angle = std::clamp(
        math::dot(math::normalize(da), math::normalize(db)), -1.0f, 1.0f);
    if (std::acos(cos_angle) <= angle) {
      BM_elem_flag_enable(v, BM_ELEM_TAG);
      any = true;
    }
  }
  if (any) {
    run_op(bm,
           "dissolve_verts verts=%hv use_face_split=%b use_boundary_tear=%b",
           BM_ELEM_TAG,
           false,
           false);
  }
}

static void delete_small_pieces(BMesh *bm, const Scope &scope, const MeshCleanupParams &params)
{
  if (params.small_pieces_faces <= 0 && params.small_pieces_size <= 0.0f) {
    return;
  }
  BM_mesh_elem_index_ensure(bm, BM_VERT);
  DisjointSet<int> pieces(bm->totvert);
  BMIter iter;
  BMEdge *e;
  BM_ITER_MESH (e, &iter, bm, BM_EDGES_OF_MESH) {
    pieces.join(BM_elem_index_get(e->v1), BM_elem_index_get(e->v2));
  }

  struct Piece {
    int faces = 0;
    float3 min = float3(FLT_MAX);
    float3 max = float3(-FLT_MAX);
    bool in_scope = true;
  };
  Map<int, Piece> by_root;
  BMVert *v;
  BM_ITER_MESH (v, &iter, bm, BM_VERTS_OF_MESH) {
    Piece &piece = by_root.lookup_or_add_default(pieces.find_root(BM_elem_index_get(v)));
    piece.min = math::min(piece.min, float3(v->co));
    piece.max = math::max(piece.max, float3(v->co));
    piece.in_scope &= scope.vert(v);
  }
  BMFace *f;
  BM_ITER_MESH (f, &iter, bm, BM_FACES_OF_MESH) {
    const BMVert *first = BM_FACE_FIRST_LOOP(f)->v;
    by_root.lookup(pieces.find_root(BM_elem_index_get(first))).faces++;
  }

  clear_tags(bm);
  bool any = false;
  BM_ITER_MESH (v, &iter, bm, BM_VERTS_OF_MESH) {
    const Piece &piece = by_root.lookup(pieces.find_root(BM_elem_index_get(v)));
    if (!piece.in_scope) {
      continue;
    }
    const bool few_faces = piece.faces < params.small_pieces_faces;
    const bool small = params.small_pieces_size > 0.0f &&
                       math::reduce_max(piece.max - piece.min) < params.small_pieces_size;
    if (few_faces || small) {
      BM_elem_flag_enable(v, BM_ELEM_TAG);
      any = true;
    }
  }
  if (any) {
    BM_mesh_delete_hflag_context(bm, BM_ELEM_TAG, DEL_VERTS);
  }
}

static bool any_step(const MeshCleanupParams &params)
{
  return params.loose_verts || params.loose_edges || params.loose_faces ||
         params.duplicate_faces || params.interior_faces || params.degenerate ||
         params.extra_verts || params.limited || params.small_pieces || params.fill_holes ||
         params.make_planar || params.split_nonplanar || params.split_concave ||
         params.recalc_normals;
}

Mesh *mesh_cleanup(const Mesh &mesh, const Span<bool> selection, const MeshCleanupParams &params)
{
  if (!any_step(params) || (!selection.is_empty() && !selection.contains(true))) {
    return nullptr;
  }

  BMeshCreateParams create_params{};
  create_params.use_toolflags = true;
  BMeshFromMeshParams convert_params{};
  convert_params.calc_face_normal = true;
  convert_params.calc_vert_normal = true;
  BMesh *bm = BKE_mesh_to_bmesh_ex(&mesh, &create_params, &convert_params);

  Scope scope;
  if (!selection.is_empty() && selection.contains(false)) {
    BM_data_layer_add_named(bm, &bm->vdata, CD_PROP_BOOL, scope_name);
    scope.offset = CustomData_get_offset_named(&bm->vdata, CD_PROP_BOOL, scope_name);
    BM_mesh_elem_table_ensure(bm, BM_VERT);
    for (const int i : selection.index_range()) {
      BM_ELEM_CD_SET_BOOL(BM_vert_at_index(bm, i), scope.offset, selection[i]);
    }
  }

  if (params.loose_verts || params.loose_edges || params.loose_faces) {
    delete_loose(bm, scope, params);
  }
  if (params.duplicate_faces) {
    delete_duplicate_faces(bm, scope);
  }
  if (params.interior_faces) {
    delete_interior_faces(bm, scope);
  }
  if (params.degenerate) {
    tag_scope(bm, scope);
    run_op(bm,
           "dissolve_degenerate edges=%he dist=%f",
           BM_ELEM_TAG,
           std::max(params.degenerate_distance, 0.0f));
  }
  if (params.extra_verts) {
    dissolve_extra_verts(bm, scope, std::max(params.extra_verts_angle, 0.0f));
  }
  if (params.limited) {
    BM_mesh_normals_update(bm);
    tag_scope(bm, scope);
    run_op(bm,
           "dissolve_limit edges=%he verts=%hv angle_limit=%f use_dissolve_boundaries=%b "
           "delimit=%i",
           BM_ELEM_TAG,
           BM_ELEM_TAG,
           std::max(params.limited_angle, 0.0f),
           params.limited_all_boundaries,
           params.limited_delimit);
  }
  if (params.small_pieces) {
    delete_small_pieces(bm, scope, params);
  }
  if (params.fill_holes) {
    tag_scope(bm, scope);
    run_op(bm, "holes_fill edges=%he sides=%i", BM_ELEM_TAG, std::max(params.fill_holes_sides, 0));
  }
  if (params.make_planar) {
    BM_mesh_normals_update(bm);
    tag_scope(bm, scope);
    run_op(bm,
           "planar_faces faces=%hf iterations=%i factor=%f",
           BM_ELEM_TAG,
           std::max(params.make_planar_iterations, 1),
           params.make_planar_factor);
  }
  if (params.split_nonplanar) {
    BM_mesh_normals_update(bm);
    tag_scope(bm, scope);
    run_op(bm,
           "connect_verts_nonplanar faces=%hf angle_limit=%f",
           BM_ELEM_TAG,
           std::max(params.split_nonplanar_angle, 0.0f));
  }
  if (params.split_concave) {
    BM_mesh_normals_update(bm);
    tag_scope(bm, scope);
    run_op(bm, "connect_verts_concave faces=%hf", BM_ELEM_TAG);
  }
  if (params.recalc_normals) {
    BM_mesh_normals_update(bm);
    tag_scope(bm, scope);
    run_op(bm, "recalc_face_normals faces=%hf", BM_ELEM_TAG);
  }

  clear_tags(bm);
  if (scope.offset != -1) {
    BM_data_layer_free_named(bm, &bm->vdata, scope_name);
  }
  Mesh *result = BKE_mesh_from_bmesh_for_eval_nomain(bm, nullptr, &mesh);
  BM_mesh_free(bm);
  return result;
}

}  // namespace blender::geometry
