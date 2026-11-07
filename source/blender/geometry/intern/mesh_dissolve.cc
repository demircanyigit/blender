/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup geo
 */

#include <algorithm>

#include "DNA_mesh_types.h"

#include "BLI_assert.h"
#include "BLI_utildefines.h"

#include "BKE_attribute.hh"
#include "BKE_customdata.hh"
#include "BKE_mesh.hh"

#include "bmesh.hh"

#include "GEO_mesh_dissolve.hh"

namespace blender::geometry {

bke::AttrDomain dissolve_selection_domain(const DissolveMode mode)
{
  switch (mode) {
    case DissolveMode::Vertices:
      return bke::AttrDomain::Point;
    case DissolveMode::Edges:
    case DissolveMode::Limited:
      return bke::AttrDomain::Edge;
    case DissolveMode::Faces:
      return bke::AttrDomain::Face;
  }
  BLI_assert_unreachable();
  return bke::AttrDomain::Point;
}

/** Tag the points whose edges are all selected, because Limited mode also takes points. */
static void tag_verts_of_tagged_edges(BMesh *bm)
{
  BMIter iter;
  BMVert *vert;
  BM_ITER_MESH (vert, &iter, bm, BM_VERTS_OF_MESH) {
    bool all_tagged = vert->e != nullptr;
    BMIter edge_iter;
    BMEdge *edge;
    BM_ITER_ELEM (edge, &edge_iter, vert, BM_EDGES_OF_VERT) {
      if (!BM_elem_flag_test(edge, BM_ELEM_TAG)) {
        all_tagged = false;
        break;
      }
    }
    BM_elem_flag_set(vert, BM_ELEM_TAG, all_tagged);
  }
}

Mesh *mesh_dissolve(const Mesh &mesh, const Span<bool> selection, const MeshDissolveParams &params)
{
  const bke::AttrDomain domain = dissolve_selection_domain(params.mode);
  if (!selection.contains(true)) {
    return nullptr;
  }

  BMeshCreateParams create_params{};
  create_params.use_toolflags = true;
  BMeshFromMeshParams convert_params{};
  convert_params.calc_face_normal = true;
  convert_params.calc_vert_normal = true;
  BMesh *bm = BKE_mesh_to_bmesh_ex(&mesh, &create_params, &convert_params);

  BM_mesh_elem_hflag_disable_all(bm, BM_VERT | BM_EDGE | BM_FACE, BM_ELEM_TAG, false);
  BM_mesh_elem_table_ensure(bm, BM_VERT | BM_EDGE | BM_FACE);
  for (const int i : selection.index_range()) {
    if (!selection[i]) {
      continue;
    }
    switch (domain) {
      case bke::AttrDomain::Point:
        BM_elem_flag_enable(BM_vert_at_index(bm, i), BM_ELEM_TAG);
        break;
      case bke::AttrDomain::Edge:
        BM_elem_flag_enable(BM_edge_at_index(bm, i), BM_ELEM_TAG);
        break;
      default:
        BM_elem_flag_enable(BM_face_at_index(bm, i), BM_ELEM_TAG);
        break;
    }
  }

  int merged_offset = -1;
  if (params.merged_faces_id) {
    const char *name = params.merged_faces_id->c_str();
    BM_data_layer_add_named(bm, &bm->pdata, CD_PROP_BOOL, name);
    merged_offset = CustomData_get_offset_named(&bm->pdata, CD_PROP_BOOL, name);
  }
  if (merged_offset != -1 && params.mode == DissolveMode::Edges) {
    /* The operator doesn't output the faces it merges. A merged face gets the attributes of one of
     * the faces it was made from, so the value set here carries over. */
    BMIter iter;
    BMEdge *edge;
    BM_ITER_MESH (edge, &iter, bm, BM_EDGES_OF_MESH) {
      BMLoop *l_a, *l_b;
      if (BM_elem_flag_test(edge, BM_ELEM_TAG) && BM_edge_loop_pair(edge, &l_a, &l_b)) {
        BM_ELEM_CD_SET_BOOL(l_a->f, merged_offset, true);
        BM_ELEM_CD_SET_BOOL(l_b->f, merged_offset, true);
      }
    }
  }

  int delimit = BMO_DELIM_NORMAL;
  if (params.keep_materials) {
    delimit |= BMO_DELIM_MATERIAL;
  }
  if (params.keep_seams) {
    delimit |= BMO_DELIM_SEAM;
  }
  if (params.keep_sharp) {
    delimit |= BMO_DELIM_SHARP;
  }
  if (params.keep_uvs) {
    delimit |= BMO_DELIM_UV;
  }

  BMOperator op;
  switch (params.mode) {
    case DissolveMode::Vertices:
      BMO_op_initf(bm,
                   &op,
                   0,
                   "dissolve_verts verts=%hv use_face_split=%b use_boundary_tear=%b",
                   BM_ELEM_TAG,
                   params.face_split,
                   params.tear_boundary);
      break;
    case DissolveMode::Edges:
      BMO_op_initf(bm,
                   &op,
                   0,
                   "dissolve_edges edges=%he use_verts=%b use_face_split=%b",
                   BM_ELEM_TAG,
                   params.remove_inline_points,
                   params.face_split);
      break;
    case DissolveMode::Faces:
      BMO_op_initf(bm,
                   &op,
                   0,
                   "dissolve_faces faces=%hf use_verts=%b",
                   BM_ELEM_TAG,
                   params.remove_inline_points);
      break;
    case DissolveMode::Limited:
      tag_verts_of_tagged_edges(bm);
      BMO_op_initf(
          bm,
          &op,
          0,
          "dissolve_limit edges=%he verts=%hv angle_limit=%f use_dissolve_boundaries=%b delimit=%i",
          BM_ELEM_TAG,
          BM_ELEM_TAG,
          std::max(params.angle, 0.0f),
          params.all_boundaries,
          delimit);
      break;
  }
  BMO_op_exec(bm, &op);
  /* Errors from the operators can stay on the mesh, and aren't useful here. */
  BMO_error_clear(bm);

  if (merged_offset != -1 && ELEM(params.mode, DissolveMode::Faces, DissolveMode::Limited)) {
    BMOIter iter;
    BMFace *face;
    BMO_ITER (face, &iter, op.slots_out, "region.out", BM_FACE) {
      BM_ELEM_CD_SET_BOOL(face, merged_offset, true);
    }
  }
  BMO_op_finish(bm, &op);

  Mesh *result = BKE_mesh_from_bmesh_for_eval_nomain(bm, nullptr, &mesh);
  BM_mesh_free(bm);
  return result;
}

}  // namespace blender::geometry
