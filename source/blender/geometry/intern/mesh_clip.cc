/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <cmath>

#include "BLI_math_geom.h"

#include "bmesh.hh"

#include "GEO_mesh_clip.hh"

namespace blender::geometry {

static bool face_leaves_plane(const BMFace *face, const float plane[4], const float threshold)
{
  const BMLoop *loop = BM_FACE_FIRST_LOOP(face);
  do {
    if (std::abs(plane_point_side_v3(plane, loop->v->co)) > threshold) {
      return true;
    }
  } while ((loop = loop->next) != BM_FACE_FIRST_LOOP(face));
  return false;
}

static void fill_cut(BMesh *bm, BMOperator &bisect_op, const BMeshClipParams &params)
{
  float plane[4];
  plane_from_point_normal_v3(plane, params.plane_co, params.plane_no);

  /* Faces lying exactly on the plane have cut edges too. Filling those would make a face on top of
   * them, so only use edges with one face, and only if that face leaves the plane. */
  BM_mesh_elem_hflag_disable_all(bm, BM_EDGE, BM_ELEM_TAG, false);
  bool has_open_edge = false;
  BMOIter iter;
  BMEdge *edge;
  BMO_ITER (edge, &iter, bisect_op.slots_out, "geom_cut.out", BM_EDGE) {
    if (BM_edge_is_boundary(edge) && face_leaves_plane(edge->l->f, plane, params.threshold)) {
      BM_elem_flag_enable(edge, BM_ELEM_TAG);
      has_open_edge = true;
    }
  }
  if (!has_open_edge) {
    return;
  }

  /* The sign of the normal doesn't matter, because the attribute fill below flips the faces to
   * match the faces next to them. */
  BMOperator fill_op;
  BMO_op_initf(bm,
               &fill_op,
               0,
               "triangle_fill edges=%he normal=%v use_dissolve=%b",
               BM_ELEM_TAG,
               &params.plane_no.x,
               true);
  BMO_op_exec(bm, &fill_op);

  BMOperator attribute_op;
  BMO_op_initf(bm,
               &attribute_op,
               0,
               "face_attribute_fill faces=%S use_normals=%b use_data=%b",
               &fill_op,
               "geom.out",
               true,
               true);
  BMO_op_exec(bm, &attribute_op);
  BMO_op_finish(bm, &attribute_op);

  BMFace *face;
  BMO_ITER (face, &iter, fill_op.slots_out, "geom.out", BM_FACE) {
    /* A smooth cap would bend the shading of the surface next to it. */
    BM_elem_flag_disable(face, BM_ELEM_SMOOTH);
    if (params.interior_face_offset != -1) {
      BM_ELEM_CD_SET_BOOL(face, params.interior_face_offset, true);
    }
  }
  BMO_op_finish(bm, &fill_op);
}

void bmesh_clip(BMesh *bm, const BMeshClipParams &params)
{
  const bool clear_inner = params.keep == BMeshClipKeep::Above;
  const bool clear_outer = params.keep == BMeshClipKeep::Below;

  BMOperator bisect_op;
  BMO_op_initf(bm,
               &bisect_op,
               0,
               "bisect_plane geom=%avef dist=%f plane_co=%v plane_no=%v clear_inner=%b clear_outer=%b",
               params.threshold,
               &params.plane_co.x,
               &params.plane_no.x,
               clear_inner,
               clear_outer);
  BMO_op_exec(bm, &bisect_op);

  if (params.cut_edge_offset != -1) {
    BMOIter iter;
    BMEdge *edge;
    BMO_ITER (edge, &iter, bisect_op.slots_out, "geom_cut.out", BM_EDGE) {
      BM_ELEM_CD_SET_BOOL(edge, params.cut_edge_offset, true);
    }
  }
  if (params.fill && params.keep != BMeshClipKeep::Both) {
    fill_cut(bm, bisect_op, params);
  }
  BMO_op_finish(bm, &bisect_op);
}

}  // namespace blender::geometry
