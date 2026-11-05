/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "DNA_mesh_types.h"

#include "BKE_customdata.hh"
#include "BKE_mesh.h"
#include "BKE_mesh.hh"

#include "bmesh.hh"

#include "GEO_mesh_cap_holes.hh"

namespace blender::geometry {

static bool mesh_has_boundary(const Mesh &mesh)
{
  Array<int> edge_face_count(mesh.edges_num, 0);
  for (const int edge : mesh.corner_edges()) {
    edge_face_count[edge]++;
  }
  for (const int count : edge_face_count) {
    if (count == 1) {
      return true;
    }
  }
  return false;
}

Mesh *mesh_cap_holes(const Mesh &mesh, const CapHolesParams &params)
{
  if (mesh.faces_num == 0 || !mesh_has_boundary(mesh)) {
    return nullptr;
  }

  BMeshCreateParams create_params{};
  create_params.use_toolflags = true;
  BMeshFromMeshParams convert_params{};
  convert_params.calc_face_normal = true;
  convert_params.calc_vert_normal = true;
  BMesh *bm = BKE_mesh_to_bmesh_ex(&mesh, &create_params, &convert_params);

  int new_face_offset = -1;
  if (params.new_faces_id) {
    BM_data_layer_add_named(bm, &bm->pdata, CD_PROP_BOOL, params.new_faces_id->c_str());
    new_face_offset = CustomData_get_offset_named(
        &bm->pdata, CD_PROP_BOOL, params.new_faces_id->c_str());
  }

  BMOperator op;
  BMO_op_initf(bm, &op, BMO_FLAG_DEFAULTS, "holes_fill edges=%ae sides=%i", params.max_sides);
  BMO_op_exec(bm, &op);

  if (params.triangulate) {
    BMO_op_callf(bm,
                 BMO_FLAG_DEFAULTS,
                 "triangulate faces=%S quad_method=%i ngon_method=%i",
                 &op,
                 "faces.out",
                 0,
                 0);
  }

  if (new_face_offset != -1) {
    /* Triangulating replaces the faces, so tag them by what they are next to: any face that is
     * not in the input any more is new. */
    BMIter iter;
    BMFace *face;
    const int old_faces_num = mesh.faces_num;
    int index = 0;
    BM_ITER_MESH (face, &iter, bm, BM_FACES_OF_MESH) {
      BM_ELEM_CD_SET_BOOL(face, new_face_offset, index >= old_faces_num);
      index++;
    }
  }
  BMO_op_finish(bm, &op);

  Mesh *result = BKE_mesh_from_bmesh_for_eval_nomain(bm, nullptr, &mesh);
  BM_mesh_free(bm);
  return result;
}

}  // namespace blender::geometry
