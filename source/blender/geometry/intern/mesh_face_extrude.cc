/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "BLI_index_mask.hh"
#include "BLI_math_vector.h"
#include "BLI_map.hh"
#include "BLI_set.hh"
#include "BLI_math_vector.hh"
#include "BLI_vector.hh"

#include "DNA_mesh_types.h"

#include "BKE_customdata.hh"
#include "BKE_mesh.h"
#include "BKE_mesh.hh"

#include "bmesh.hh"

#include "GEO_mesh_face_extrude.hh"

namespace blender::geometry {

/** Disjoint set over the indices of the moved vertices, to find the connected regions. */
static int find_root(Vector<int> &parents, int i)
{
  while (parents[i] != i) {
    parents[i] = parents[parents[i]];
    i = parents[i];
  }
  return i;
}

Mesh *mesh_face_extrude(const Mesh &mesh,
                        const IndexMask &face_selection,
                        const FaceExtrudeParams &params)
{
  if (face_selection.is_empty()) {
    return nullptr;
  }

  BMeshCreateParams create_params{};
  create_params.use_toolflags = true;
  BMeshFromMeshParams convert_params{};
  convert_params.calc_face_normal = true;
  convert_params.calc_vert_normal = true;
  BMesh *bm = BKE_mesh_to_bmesh_ex(&mesh, &create_params, &convert_params);

  int top_offset = -1;
  int side_offset = -1;
  if (params.top_id) {
    BM_data_layer_add_named(bm, &bm->pdata, CD_PROP_BOOL, params.top_id->c_str());
    top_offset = CustomData_get_offset_named(&bm->pdata, CD_PROP_BOOL, params.top_id->c_str());
  }
  if (params.side_id) {
    BM_data_layer_add_named(bm, &bm->pdata, CD_PROP_BOOL, params.side_id->c_str());
    side_offset = CustomData_get_offset_named(&bm->pdata, CD_PROP_BOOL, params.side_id->c_str());
  }

  BM_mesh_elem_table_ensure(bm, BM_FACE);
  BM_mesh_elem_hflag_disable_all(bm, BM_FACE | BM_EDGE | BM_VERT, BM_ELEM_TAG, false);
  face_selection.foreach_index([&](const int i) {
    BM_elem_flag_enable(BM_face_at_index(bm, i), BM_ELEM_TAG);
  });

  /* The faces that end up on top, and their vertices, which are the ones to move. */
  Vector<BMFace *> top_faces;
  BMOperator op;
  if (params.individual) {
    BMO_op_initf(bm,
                 &op,
                 BMO_FLAG_DEFAULTS,
                 "extrude_discrete_faces faces=%hf use_normal_flip=%b use_select_history=%b",
                 BM_ELEM_TAG,
                 false,
                 false);
    BMO_op_exec(bm, &op);
    BMOIter oiter;
    BMFace *face;
    BMO_ITER (face, &oiter, op.slots_out, "faces.out", BM_FACE) {
      top_faces.append(face);
    }
  }
  else {
    BMO_op_initf(bm,
                 &op,
                 BMO_FLAG_DEFAULTS,
                 "extrude_face_region geom=%hf use_keep_orig=%b use_normal_flip=%b "
                 "use_normal_from_adjacent=%b use_dissolve_ortho_edges=%b use_select_history=%b",
                 BM_ELEM_TAG,
                 false,
                 false,
                 false,
                 false,
                 false);
    BMO_op_exec(bm, &op);
    /* The top faces are the new faces with all their vertices new. */
    BM_mesh_elem_hflag_disable_all(bm, BM_VERT, BM_ELEM_TAG, false);
    BMOIter oiter;
    BMVert *vert;
    BMO_ITER (vert, &oiter, op.slots_out, "geom.out", BM_VERT) {
      BM_elem_flag_enable(vert, BM_ELEM_TAG);
    }
    BMFace *face;
    BMO_ITER (face, &oiter, op.slots_out, "geom.out", BM_FACE) {
      bool all_new = true;
      BMLoop *l_first = BM_FACE_FIRST_LOOP(face);
      BMLoop *l_iter = l_first;
      do {
        if (!BM_elem_flag_test(l_iter->v, BM_ELEM_TAG)) {
          all_new = false;
          break;
        }
      } while ((l_iter = l_iter->next) != l_first);
      if (all_new) {
        top_faces.append(face);
      }
    }
  }
  BMO_op_finish(bm, &op);

  /* Gather the vertices of the top faces and their offset directions. */
  Map<BMVert *, int> vert_indices;
  Vector<BMVert *> verts;
  Vector<float3> directions;
  for (BMFace *face : top_faces) {
    BM_face_normal_update(face);
    BMLoop *l_first = BM_FACE_FIRST_LOOP(face);
    BMLoop *l_iter = l_first;
    do {
      const int index = vert_indices.lookup_or_add_cb(l_iter->v, [&]() {
        verts.append(l_iter->v);
        directions.append(float3(0.0f));
        return int(verts.size() - 1);
      });
      directions[index] += float3(face->no);
    } while ((l_iter = l_iter->next) != l_first);
  }

  /* Connected regions of top faces, to scale each around its own center. */
  Vector<int> parents(verts.size());
  for (const int i : parents.index_range()) {
    parents[i] = i;
  }
  for (BMFace *face : top_faces) {
    BMLoop *l_first = BM_FACE_FIRST_LOOP(face);
    const int root = find_root(parents, vert_indices.lookup(l_first->v));
    BMLoop *l_iter = l_first->next;
    while (l_iter != l_first) {
      const int other = find_root(parents, vert_indices.lookup(l_iter->v));
      parents[other] = root;
      l_iter = l_iter->next;
    }
  }

  /* Move along the normals first, then scale around the moved centers. */
  for (const int i : verts.index_range()) {
    const float3 direction = math::normalize(directions[i]);
    float3 co(verts[i]->co);
    co += direction * params.amount;
    copy_v3_v3(verts[i]->co, co);
  }
  if (params.scale != 1.0f) {
    Map<int, float3> centers;
    Map<int, int> counts;
    for (const int i : verts.index_range()) {
      const int root = find_root(parents, i);
      centers.lookup_or_add(root, float3(0.0f)) += float3(verts[i]->co);
      counts.lookup_or_add(root, 0)++;
    }
    for (const int i : verts.index_range()) {
      const int root = find_root(parents, i);
      const float3 center = centers.lookup(root) / float(counts.lookup(root));
      const float3 co = center + (float3(verts[i]->co) - center) * params.scale;
      copy_v3_v3(verts[i]->co, co);
    }
  }

  if (top_offset != -1 || side_offset != -1) {
    Set<BMFace *> top_set(top_faces.as_span());
    BMIter iter;
    BMFace *face;
    BM_ITER_MESH (face, &iter, bm, BM_FACES_OF_MESH) {
      const bool is_top = top_set.contains(face);
      bool is_side = false;
      if (!is_top) {
        BMLoop *l_first = BM_FACE_FIRST_LOOP(face);
        BMLoop *l_iter = l_first;
        do {
          if (vert_indices.contains(l_iter->v)) {
            is_side = true;
            break;
          }
        } while ((l_iter = l_iter->next) != l_first);
      }
      if (top_offset != -1) {
        BM_ELEM_CD_SET_BOOL(face, top_offset, is_top);
      }
      if (side_offset != -1) {
        BM_ELEM_CD_SET_BOOL(face, side_offset, is_side);
      }
    }
  }

  Mesh *result = BKE_mesh_from_bmesh_for_eval_nomain(bm, nullptr, &mesh);
  BM_mesh_free(bm);
  return result;
}

}  // namespace blender::geometry
