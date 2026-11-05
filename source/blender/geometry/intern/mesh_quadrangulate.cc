/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "DNA_mesh_types.h"

#include "BLI_index_mask.hh"

#include "BKE_mesh.h"
#include "BKE_mesh.hh"

#include "bmesh.hh"

#include "GEO_mesh_quadrangulate.hh"

namespace blender::geometry {

Mesh *mesh_quadrangulate(const Mesh &mesh,
                         const IndexMask &selection,
                         const QuadrangulateParams &params)
{
  if (selection.is_empty()) {
    return nullptr;
  }
  /* Only triangles can be joined. */
  const OffsetIndices faces = mesh.faces();
  bool has_triangles = false;
  selection.foreach_index([&](const int face) {
    if (faces[face].size() == 3) {
      has_triangles = true;
    }
  });
  if (!has_triangles) {
    return nullptr;
  }

  BMeshCreateParams create_params{};
  create_params.use_toolflags = true;
  BMeshFromMeshParams convert_params{};
  convert_params.calc_face_normal = true;
  convert_params.calc_vert_normal = true;
  BMesh *bm = BKE_mesh_to_bmesh_ex(&mesh, &create_params, &convert_params);

  BM_mesh_elem_table_ensure(bm, BM_FACE);
  selection.foreach_index(
      [&](const int face) { BM_elem_flag_enable(BM_face_at_index(bm, face), BM_ELEM_TAG); });

  BMO_op_callf(bm,
               BMO_FLAG_DEFAULTS,
               "join_triangles faces=%hf angle_face_threshold=%f angle_shape_threshold=%f "
               "cmp_seam=%b cmp_sharp=%b cmp_uvs=%b cmp_vcols=%b cmp_materials=%b "
               "topology_influence=%f deselect_joined=%b",
               BM_ELEM_TAG,
               params.face_threshold,
               params.shape_threshold,
               params.compare_seam,
               params.compare_sharp,
               params.compare_uvs,
               params.compare_vcols,
               params.compare_materials,
               params.topology_influence,
               false);

  Mesh *result = BKE_mesh_from_bmesh_for_eval_nomain(bm, nullptr, &mesh);
  BM_mesh_free(bm);
  return result;
}

}  // namespace blender::geometry
