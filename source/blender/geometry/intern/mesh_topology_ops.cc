/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <cmath>

#include "BLI_array.hh"
#include "BLI_index_mask.hh"
#include "BLI_math_vector.hh"
#include "BLI_task.hh"

#include "DNA_mesh_types.h"

#include "BKE_attribute.hh"
#include "BKE_mesh.h"
#include "BKE_mesh.hh"

#include "bmesh.hh"

#include "GEO_mesh_topology_ops.hh"

namespace blender::geometry {

/* -------------------------------------------------------------------- */
/** \name BMesh Helpers
 * \{ */

static BMesh *to_bmesh(const Mesh &mesh)
{
  BMeshCreateParams create_params{};
  create_params.use_toolflags = true;
  BMeshFromMeshParams convert_params{};
  convert_params.calc_face_normal = true;
  convert_params.calc_vert_normal = true;
  return BKE_mesh_to_bmesh_ex(&mesh, &create_params, &convert_params);
}

static Mesh *from_bmesh(BMesh *bm, const Mesh &mesh)
{
  Mesh *result = BKE_mesh_from_bmesh_for_eval_nomain(bm, nullptr, &mesh);
  BM_mesh_free(bm);
  return result;
}

static void tag_faces(BMesh *bm, const IndexMask &selection)
{
  BM_mesh_elem_hflag_disable_all(bm, BM_FACE | BM_EDGE | BM_VERT, BM_ELEM_TAG, false);
  BM_mesh_elem_table_ensure(bm, BM_FACE);
  selection.foreach_index(
      [&](const int face) { BM_elem_flag_enable(BM_face_at_index(bm, face), BM_ELEM_TAG); });
}

/** Tag the edges whose faces are all tagged (and that have at least two). */
static int tag_inner_edges(BMesh *bm)
{
  int count = 0;
  BMIter iter;
  BMEdge *edge;
  BM_ITER_MESH (edge, &iter, bm, BM_EDGES_OF_MESH) {
    bool inner = edge->l && edge->l->radial_next != edge->l;
    if (inner) {
      BMLoop *l = edge->l;
      do {
        if (!BM_elem_flag_test(l->f, BM_ELEM_TAG)) {
          inner = false;
          break;
        }
      } while ((l = l->radial_next) != edge->l);
    }
    BM_elem_flag_set(edge, BM_ELEM_TAG, inner);
    count += inner;
  }
  return count;
}

static int add_bool_layer(BMesh *bm, const std::string &name)
{
  if (name.empty()) {
    return -1;
  }
  BM_data_layer_add_named(bm, &bm->pdata, CD_PROP_BOOL, name);
  return CustomData_get_offset_named(&bm->pdata, CD_PROP_BOOL, name);
}

static void set_bool(BMFace *face, const int offset, const bool value)
{
  if (offset != -1) {
    BM_ELEM_CD_SET_BOOL(face, offset, value);
  }
}

static bool has_selection(const IndexMask &selection)
{
  return !selection.is_empty();
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Inset
 * \{ */

/** Inset the tagged faces. New faces get the side flag and the material offset. */
static void inset_tagged(BMesh *bm,
                         const InsetParams &params,
                         const int top_offset,
                         const int side_offset,
                         const bool retag_top)
{
  BMOperator op;
  if (params.individual) {
    BMO_op_initf(bm,
                 &op,
                 BMO_FLAG_DEFAULTS,
                 "inset_individual faces=%hf thickness=%f depth=%f use_even_offset=%b "
                 "use_interpolate=%b use_relative_offset=%b",
                 BM_ELEM_TAG,
                 params.thickness,
                 params.depth,
                 params.use_even_offset,
                 true,
                 params.use_relative_offset);
  }
  else {
    BMO_op_initf(bm,
                 &op,
                 BMO_FLAG_DEFAULTS,
                 "inset_region faces=%hf use_boundary=%b use_even_offset=%b use_interpolate=%b "
                 "use_relative_offset=%b use_edge_rail=%b thickness=%f depth=%f use_outset=%b",
                 BM_ELEM_TAG,
                 params.use_boundary,
                 params.use_even_offset,
                 true,
                 params.use_relative_offset,
                 params.use_edge_rail,
                 params.thickness,
                 params.depth,
                 params.use_outset);
  }
  BMO_op_exec(bm, &op);

  BMIter iter;
  BMFace *face;
  BM_ITER_MESH (face, &iter, bm, BM_FACES_OF_MESH) {
    if (BM_elem_flag_test(face, BM_ELEM_TAG)) {
      set_bool(face, top_offset, true);
    }
  }
  BMOIter oiter;
  BMO_ITER (face, &oiter, op.slots_out, "faces.out", BM_FACE) {
    set_bool(face, side_offset, true);
    set_bool(face, top_offset, false);
    face->mat_nr = short(std::max(0, face->mat_nr + params.side_material_offset));
    if (retag_top) {
      BM_elem_flag_disable(face, BM_ELEM_TAG);
    }
  }
  BMO_op_finish(bm, &op);
}

Mesh *mesh_inset(const Mesh &mesh,
                 const IndexMask &selection,
                 const InsetParams &params,
                 const TopologyOutputs &outputs)
{
  if (!has_selection(selection)) {
    return nullptr;
  }
  BMesh *bm = to_bmesh(mesh);
  const int top_offset = add_bool_layer(bm, outputs.top);
  const int side_offset = add_bool_layer(bm, outputs.side);
  tag_faces(bm, selection);
  inset_tagged(bm, params, top_offset, side_offset, false);
  return from_bmesh(bm, mesh);
}

Mesh *mesh_panelize(const Mesh &mesh,
                    const IndexMask &selection,
                    const PanelizeParams &params,
                    const TopologyOutputs &outputs)
{
  if (!has_selection(selection)) {
    return nullptr;
  }
  BMesh *bm = to_bmesh(mesh);
  const int top_offset = add_bool_layer(bm, outputs.top);
  const int side_offset = add_bool_layer(bm, outputs.side);
  tag_faces(bm, selection);

  /* The gap: each face inset on its own, flat. */
  InsetParams gap;
  gap.individual = true;
  gap.thickness = params.gap * 0.5f;
  gap.depth = 0.0f;
  gap.use_even_offset = params.use_even_offset;
  gap.use_relative_offset = params.use_relative_offset;
  gap.side_material_offset = params.gap_material_offset;
  inset_tagged(bm, gap, top_offset, side_offset, true);

  /* The chamfer: the panels inset again and lifted. */
  if (params.bevel > 0.0f || params.height != 0.0f) {
    InsetParams chamfer;
    chamfer.individual = true;
    chamfer.thickness = params.bevel;
    chamfer.depth = params.height;
    chamfer.use_even_offset = params.use_even_offset;
    chamfer.use_relative_offset = params.use_relative_offset;
    inset_tagged(bm, chamfer, top_offset, -1, true);
  }
  return from_bmesh(bm, mesh);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Poke
 * \{ */

Mesh *mesh_poke(const Mesh &mesh,
                const IndexMask &selection,
                const PokeParams &params,
                const TopologyOutputs &outputs)
{
  if (!has_selection(selection)) {
    return nullptr;
  }
  BMesh *bm = to_bmesh(mesh);
  const int top_offset = add_bool_layer(bm, outputs.top);
  const int side_offset = add_bool_layer(bm, outputs.side);
  tag_faces(bm, selection);

  BMOperator op;
  BMO_op_initf(bm,
               &op,
               BMO_FLAG_DEFAULTS,
               "poke faces=%hf offset=%f use_relative_offset=%b center_mode=%i",
               BM_ELEM_TAG,
               params.offset,
               params.use_relative_offset,
               int(params.center));
  BMO_op_exec(bm, &op);
  BMOIter oiter;
  BMFace *face;
  BMO_ITER (face, &oiter, op.slots_out, "faces.out", BM_FACE) {
    set_bool(face, side_offset, true);
    set_bool(face, top_offset, true);
  }
  BMO_op_finish(bm, &op);
  return from_bmesh(bm, mesh);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Beautify and Rotate Edges
 * \{ */

Mesh *mesh_beautify(const Mesh &mesh,
                    const IndexMask &selection,
                    const BeautifyMethod method,
                    const bool triangulate)
{
  if (!has_selection(selection)) {
    return nullptr;
  }
  BMesh *bm = to_bmesh(mesh);
  tag_faces(bm, selection);
  if (triangulate) {
    BMOperator op;
    BMO_op_initf(bm,
                 &op,
                 BMO_FLAG_DEFAULTS,
                 "triangulate faces=%hf quad_method=%i ngon_method=%i",
                 BM_ELEM_TAG,
                 0,
                 0);
    BMO_op_exec(bm, &op);
    BMO_slot_buffer_hflag_enable(bm, op.slots_out, "faces.out", BM_FACE, BM_ELEM_TAG, false);
    BMO_op_finish(bm, &op);
  }
  /* Only triangles can be beautified. */
  BMIter iter;
  BMFace *face;
  BM_ITER_MESH (face, &iter, bm, BM_FACES_OF_MESH) {
    if (face->len != 3) {
      BM_elem_flag_disable(face, BM_ELEM_TAG);
    }
  }
  if (tag_inner_edges(bm) > 0) {
    BMO_op_callf(bm,
                 BMO_FLAG_DEFAULTS,
                 "beautify_fill faces=%hf edges=%he use_restrict_tag=%b method=%i",
                 BM_ELEM_TAG,
                 BM_ELEM_TAG,
                 false,
                 int(method));
  }
  return from_bmesh(bm, mesh);
}

Mesh *mesh_rotate_edges(const Mesh &mesh,
                        const IndexMask &selection,
                        const bool counter_clockwise,
                        const int steps)
{
  if (!has_selection(selection) || steps < 1) {
    return nullptr;
  }
  BMesh *bm = to_bmesh(mesh);
  tag_faces(bm, selection);
  for (int step = 0; step < steps; step++) {
    if (tag_inner_edges(bm) == 0) {
      break;
    }
    BMOperator op;
    BMO_op_initf(
        bm, &op, BMO_FLAG_DEFAULTS, "rotate_edges edges=%he use_ccw=%b", BM_ELEM_TAG, counter_clockwise);
    BMO_op_exec(bm, &op);
    BMO_op_finish(bm, &op);
    /* Rotating keeps the faces tagged: their flags carry over to the faces made in their place. */
  }
  return from_bmesh(bm, mesh);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Symmetrize
 * \{ */

Mesh *mesh_symmetrize(const Mesh &mesh,
                      const SymmetrizeDirection direction,
                      const float merge_distance)
{
  if (mesh.verts_num == 0) {
    return nullptr;
  }
  BMesh *bm = to_bmesh(mesh);
  BMO_op_callf(bm,
               BMO_FLAG_DEFAULTS,
               "symmetrize input=%avef direction=%i dist=%f use_shapekey=%b",
               int(direction),
               merge_distance,
               false);
  return from_bmesh(bm, mesh);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Mark by Angle
 * \{ */

void mesh_edges_by_angle(const Mesh &mesh,
                         const MarkByAngleParams &params,
                         MutableSpan<bool> r_selection)
{
  const Span<float3> positions = mesh.vert_positions();
  const OffsetIndices faces = mesh.faces();
  const Span<int> corner_verts = mesh.corner_verts();
  const Span<int> corner_edges = mesh.corner_edges();
  const Span<float3> face_normals = mesh.face_normals();

  /* The first two faces of each edge, and how many it has. */
  Array<int2> edge_faces(mesh.edges_num, int2(-1));
  Array<int> edge_users(mesh.edges_num, 0);
  for (const int face : faces.index_range()) {
    for (const int edge : corner_edges.slice(faces[face])) {
      if (edge_users[edge] < 2) {
        edge_faces[edge][edge_users[edge]] = face;
      }
      edge_users[edge]++;
    }
  }
  Array<float3> face_centers(faces.size());
  threading::parallel_for(faces.index_range(), 4096, [&](const IndexRange range) {
    for (const int face : range) {
      face_centers[face] = bke::mesh::face_center_calc(positions,
                                                       corner_verts.slice(faces[face]));
    }
  });

  const float cos_limit = std::cos(params.angle);
  threading::parallel_for(IndexRange(mesh.edges_num), 4096, [&](const IndexRange range) {
    for (const int edge : range) {
      const int users = edge_users[edge];
      if (users == 0) {
        r_selection[edge] = false;
        continue;
      }
      if (users == 1) {
        r_selection[edge] = params.use_boundary;
        continue;
      }
      if (users > 2) {
        /* Non-manifold edges always count as sharp. */
        r_selection[edge] = true;
        continue;
      }
      const int a = edge_faces[edge][0];
      const int b = edge_faces[edge][1];
      const float cos_angle = math::dot(face_normals[a], face_normals[b]);
      if (cos_angle > cos_limit) {
        r_selection[edge] = false;
        continue;
      }
      if (params.side == MarkAngleSide::Both) {
        r_selection[edge] = true;
        continue;
      }
      const bool convex = math::dot(face_normals[a], face_centers[b] - face_centers[a]) < 0.0f;
      r_selection[edge] = (params.side == MarkAngleSide::Convex) == convex;
    }
  });
}

Mesh *mesh_mark_by_angle(const Mesh &mesh,
                         const IndexMask &edge_mask,
                         const MarkByAngleParams &params)
{
  if (mesh.edges_num == 0 || !(params.mark_sharp || params.mark_seam || params.mark_crease ||
                               params.mark_bevel_weight || params.flat_faces))
  {
    return nullptr;
  }
  Array<bool> marked(mesh.edges_num);
  mesh_edges_by_angle(mesh, params, marked);
  /* Edges outside the mask keep what they had. */
  Array<bool> in_mask(mesh.edges_num, false);
  edge_mask.foreach_index([&](const int edge) { in_mask[edge] = true; });

  Mesh *result = BKE_mesh_copy_for_eval(mesh);
  bke::MutableAttributeAccessor attributes = result->attributes_for_write();

  const auto write_bool = [&](const StringRef name) {
    bke::SpanAttributeWriter values = attributes.lookup_or_add_for_write_span<bool>(
        name, bke::AttrDomain::Edge);
    if (!values) {
      return;
    }
    for (const int edge : marked.index_range()) {
      if (!in_mask[edge]) {
        continue;
      }
      if (marked[edge]) {
        values.span[edge] = true;
      }
      else if (params.clear_others) {
        values.span[edge] = false;
      }
    }
    values.finish();
  };
  const auto write_float = [&](const StringRef name) {
    bke::SpanAttributeWriter values = attributes.lookup_or_add_for_write_span<float>(
        name, bke::AttrDomain::Edge);
    if (!values) {
      return;
    }
    for (const int edge : marked.index_range()) {
      if (!in_mask[edge]) {
        continue;
      }
      if (marked[edge]) {
        values.span[edge] = params.value;
      }
      else if (params.clear_others) {
        values.span[edge] = 0.0f;
      }
    }
    values.finish();
  };
  if (params.mark_sharp) {
    write_bool("sharp_edge");
  }
  if (params.mark_seam) {
    write_bool("uv_seam");
  }
  if (params.mark_crease) {
    write_float("crease_edge");
  }
  if (params.mark_bevel_weight) {
    write_float("bevel_weight_edge");
  }
  if (params.flat_faces) {
    const OffsetIndices faces = result->faces();
    const Span<int> corner_edges = result->corner_edges();
    bke::SpanAttributeWriter sharp_faces = attributes.lookup_or_add_for_write_span<bool>(
        "sharp_face", bke::AttrDomain::Face);
    if (sharp_faces) {
      for (const int face : faces.index_range()) {
        for (const int edge : corner_edges.slice(faces[face])) {
          if (marked[edge] && in_mask[edge]) {
            sharp_faces.span[face] = true;
            break;
          }
        }
      }
      sharp_faces.finish();
    }
  }
  return result;
}

/** \} */

}  // namespace blender::geometry
