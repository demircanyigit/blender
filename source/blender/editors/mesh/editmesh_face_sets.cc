/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup edmesh
 *
 * Face set operators for Edit Mode: make, assign, merge and clear face sets, make them from the
 * parts, materials, seams or sharp edges of the mesh, select and hide by set, and turn sets into
 * vertex groups or materials. Face sets are the `.sculpt_face_set` face attribute that Sculpt
 * Mode, the Piece ID modifier and the Face Set solid color read.
 */

#include <algorithm>

#include <fmt/format.h>

#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"

#include "BLI_array.hh"
#include "BLI_map.hh"
#include "BLI_math_color.h"
#include "BLI_math_rotation.h"
#include "BLI_math_vector.h"
#include "BLI_set.hh"
#include "BLI_vector.hh"

#include "BKE_context.hh"
#include "BKE_customdata.hh"
#include "BKE_deform.hh"
#include "BKE_editmesh.hh"
#include "BKE_layer.hh"
#include "BKE_lib_id.hh"
#include "BKE_material.hh"
#include "BKE_object_deform.h"
#include "BKE_paint.hh"
#include "BKE_report.hh"

#include "BLT_translation.hh"

#include "DEG_depsgraph.hh"
#include "DEG_depsgraph_build.hh"

#include "RNA_access.hh"
#include "RNA_define.hh"

#include "WM_api.hh"
#include "WM_types.hh"

#include "ED_mesh.hh"
#include "ED_screen.hh"

#include "UI_resources.hh"

#include "bmesh.hh"

#include "mesh_intern.hh" /* own include */

namespace blender {

static constexpr const char *FACE_SET_NAME = ".sculpt_face_set";

/** The face set layer of the edit mesh, made with every face in the default set when missing. */
static int face_set_offset_ensure(BMesh &bm, const Mesh &mesh)
{
  int offset = CustomData_get_offset_named(&bm.pdata, CD_PROP_INT32, FACE_SET_NAME);
  if (offset != -1) {
    return offset;
  }
  BM_data_layer_add_named(&bm, &bm.pdata, CD_PROP_INT32, FACE_SET_NAME);
  offset = CustomData_get_offset_named(&bm.pdata, CD_PROP_INT32, FACE_SET_NAME);
  BMIter iter;
  BMFace *f;
  BM_ITER_MESH (f, &iter, &bm, BM_FACES_OF_MESH) {
    BM_ELEM_CD_SET_INT(f, offset, mesh.face_sets_color_default);
  }
  return offset;
}

static bool face_selected(const BMFace *f)
{
  return BM_elem_flag_test(f, BM_ELEM_SELECT) && !BM_elem_flag_test(f, BM_ELEM_HIDDEN);
}

static void face_sets_changed(bContext *C, Object *obedit)
{
  EDBMUpdate_Params params{};
  params.calc_looptris = false;
  params.calc_normals = false;
  params.is_destructive = false;
  EDBM_update(id_cast<Mesh *>(obedit->data), &params);
  WM_event_add_notifier(C, NC_GEOM | ND_DATA, obedit->data);
}

/** Give the selected faces one face set: a new one, or `face_set` when it isn't zero. */
static wmOperatorStatus face_sets_assign(bContext *C, wmOperator *op, const int face_set)
{
  Scene *scene = CTX_data_scene(C);
  ViewLayer *view_layer = CTX_data_view_layer(C);
  Vector<Object *> objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      scene, view_layer, CTX_wm_view3d(C));

  int changed = 0;
  for (Object *obedit : objects) {
    BMEditMesh *em = BKE_editmesh_from_object(obedit);
    BMesh *bm = em->bm;
    if (bm->totfacesel == 0) {
      continue;
    }
    const Mesh &mesh = *id_cast<const Mesh *>(obedit->data);
    const int offset = face_set_offset_ensure(*bm, mesh);

    BMIter iter;
    BMFace *f;
    int id = face_set;
    if (id == 0) {
      /* One more than the highest set of the mesh. */
      id = mesh.face_sets_color_default;
      BM_ITER_MESH (f, &iter, bm, BM_FACES_OF_MESH) {
        id = std::max(id, BM_ELEM_CD_GET_INT(f, offset));
      }
      id++;
    }
    BM_ITER_MESH (f, &iter, bm, BM_FACES_OF_MESH) {
      if (face_selected(f)) {
        BM_ELEM_CD_SET_INT(f, offset, id);
      }
    }
    face_sets_changed(C, obedit);
    changed++;
  }

  if (changed == 0) {
    BKE_report(op->reports, RPT_WARNING, "No faces selected");
    return OPERATOR_CANCELLED;
  }
  return OPERATOR_FINISHED;
}

/* -------------------------------------------------------------------- */
/** \name New Face Set
 * \{ */

static wmOperatorStatus edbm_face_set_new_exec(bContext *C, wmOperator *op)
{
  return face_sets_assign(C, op, 0);
}

void MESH_OT_face_set_new(wmOperatorType *ot)
{
  ot->name = "New Face Set";
  ot->idname = "MESH_OT_face_set_new";
  ot->description = "Put the selected faces in a new face set";

  ot->exec = edbm_face_set_new_exec;
  ot->poll = ED_operator_editmesh;

  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Assign Face Set
 * \{ */

static wmOperatorStatus edbm_face_set_assign_exec(bContext *C, wmOperator *op)
{
  return face_sets_assign(C, op, RNA_int_get(op->ptr, "face_set"));
}

static wmOperatorStatus edbm_face_set_assign_invoke(bContext *C,
                                                    wmOperator *op,
                                                    const wmEvent *event)
{
  /* Start from the set of the active face, so the dialog shows something useful. */
  if (!RNA_struct_property_is_set(op->ptr, "face_set")) {
    Object *obedit = CTX_data_edit_object(C);
    BMEditMesh *em = obedit ? BKE_editmesh_from_object(obedit) : nullptr;
    if (em && em->bm->act_face) {
      const int offset = CustomData_get_offset_named(
          &em->bm->pdata, CD_PROP_INT32, FACE_SET_NAME);
      if (offset != -1) {
        RNA_int_set(op->ptr, "face_set", BM_ELEM_CD_GET_INT(em->bm->act_face, offset));
      }
    }
  }
  return WM_operator_props_popup_confirm_ex(
      C, op, event, IFACE_("Assign Face Set"), IFACE_("Assign"));
}

void MESH_OT_face_set_assign(wmOperatorType *ot)
{
  ot->name = "Assign Face Set";
  ot->idname = "MESH_OT_face_set_assign";
  ot->description = "Put the selected faces in the face set with the given number";

  ot->invoke = edbm_face_set_assign_invoke;
  ot->exec = edbm_face_set_assign_exec;
  ot->poll = ED_operator_editmesh;

  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  RNA_def_int(ot->srna, "face_set", 1, 1, INT_MAX, "Face Set", "Number of the face set", 1, 100);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Select Face Set
 * \{ */

static wmOperatorStatus edbm_face_set_select_exec(bContext *C, wmOperator *op)
{
  Scene *scene = CTX_data_scene(C);
  ViewLayer *view_layer = CTX_data_view_layer(C);
  Vector<Object *> objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      scene, view_layer, CTX_wm_view3d(C));

  bool any_selected = false;
  for (Object *obedit : objects) {
    BMEditMesh *em = BKE_editmesh_from_object(obedit);
    BMesh *bm = em->bm;
    if (bm->totfacesel == 0) {
      continue;
    }
    any_selected = true;
    const int offset = CustomData_get_offset_named(&bm->pdata, CD_PROP_INT32, FACE_SET_NAME);
    BMIter iter;
    BMFace *f;
    if (offset == -1) {
      /* All faces are in the default set. */
      BM_ITER_MESH (f, &iter, bm, BM_FACES_OF_MESH) {
        if (!BM_elem_flag_test(f, BM_ELEM_HIDDEN)) {
          BM_face_select_set(bm, f, true);
        }
      }
    }
    else {
      Set<int> sets;
      BM_ITER_MESH (f, &iter, bm, BM_FACES_OF_MESH) {
        if (face_selected(f)) {
          sets.add(BM_ELEM_CD_GET_INT(f, offset));
        }
      }
      BM_ITER_MESH (f, &iter, bm, BM_FACES_OF_MESH) {
        if (!BM_elem_flag_test(f, BM_ELEM_HIDDEN) && sets.contains(BM_ELEM_CD_GET_INT(f, offset)))
        {
          BM_face_select_set(bm, f, true);
        }
      }
    }
    EDBM_selectmode_flush(em);
    EDBM_uvselect_clear(em);
    DEG_id_tag_update(obedit->data, ID_RECALC_SELECT);
    WM_event_add_notifier(C, NC_GEOM | ND_SELECT, obedit->data);
  }

  if (!any_selected) {
    BKE_report(op->reports, RPT_WARNING, "No faces selected");
    return OPERATOR_CANCELLED;
  }
  return OPERATOR_FINISHED;
}

void MESH_OT_face_set_select(wmOperatorType *ot)
{
  ot->name = "Select Face Set";
  ot->idname = "MESH_OT_face_set_select";
  ot->description = "Select all faces in the same face sets as the selected faces";

  ot->exec = edbm_face_set_select_exec;
  ot->poll = ED_operator_editmesh;

  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Clear Face Set
 * \{ */

static wmOperatorStatus edbm_face_set_clear_exec(bContext *C, wmOperator *op)
{
  Scene *scene = CTX_data_scene(C);
  ViewLayer *view_layer = CTX_data_view_layer(C);
  Vector<Object *> objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      scene, view_layer, CTX_wm_view3d(C));

  bool any_selected = false;
  for (Object *obedit : objects) {
    BMEditMesh *em = BKE_editmesh_from_object(obedit);
    BMesh *bm = em->bm;
    if (bm->totfacesel == 0) {
      continue;
    }
    any_selected = true;
    const int offset = CustomData_get_offset_named(&bm->pdata, CD_PROP_INT32, FACE_SET_NAME);
    if (offset == -1) {
      continue;
    }
    const int default_set = id_cast<const Mesh *>(obedit->data)->face_sets_color_default;
    BMIter iter;
    BMFace *f;
    BM_ITER_MESH (f, &iter, bm, BM_FACES_OF_MESH) {
      if (face_selected(f)) {
        BM_ELEM_CD_SET_INT(f, offset, default_set);
      }
    }
    face_sets_changed(C, obedit);
  }

  if (!any_selected) {
    BKE_report(op->reports, RPT_WARNING, "No faces selected");
    return OPERATOR_CANCELLED;
  }
  return OPERATOR_FINISHED;
}

void MESH_OT_face_set_clear(wmOperatorType *ot)
{
  ot->name = "Clear Face Set";
  ot->idname = "MESH_OT_face_set_clear";
  ot->description = "Put the selected faces back in the default face set";

  ot->exec = edbm_face_set_clear_exec;
  ot->poll = ED_operator_editmesh;

  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Shared Helpers
 * \{ */

static Vector<Object *> face_set_objects(bContext *C)
{
  return BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      CTX_data_scene(C), CTX_data_view_layer(C), CTX_wm_view3d(C));
}

/** The face set of a face, the default one when the mesh has none. */
static int face_set_get(const BMFace *f, const int offset, const int default_set)
{
  return (offset == -1) ? default_set : BM_ELEM_CD_GET_INT(f, offset);
}

static int face_set_default(const Object *obedit)
{
  return id_cast<const Mesh *>(obedit->data)->face_sets_color_default;
}

static int face_set_offset_get(const BMesh &bm)
{
  return CustomData_get_offset_named(&bm.pdata, CD_PROP_INT32, FACE_SET_NAME);
}

/** The face sets of the selected faces. */
static Set<int> face_sets_of_selection(BMesh &bm, const int offset, const int default_set)
{
  Set<int> sets;
  BMIter iter;
  BMFace *f;
  BM_ITER_MESH (f, &iter, &bm, BM_FACES_OF_MESH) {
    if (face_selected(f)) {
      sets.add(face_set_get(f, offset, default_set));
    }
  }
  return sets;
}

/** Select exactly the visible faces for which `fn` is true. */
template<typename Fn> static void face_select_only(BMEditMesh &em, Fn fn)
{
  BMesh &bm = *em.bm;
  Vector<BMFace *> to_select;
  BMIter iter;
  BMFace *f;
  BM_ITER_MESH (f, &iter, &bm, BM_FACES_OF_MESH) {
    if (!BM_elem_flag_test(f, BM_ELEM_HIDDEN) && fn(f)) {
      to_select.append(f);
    }
  }
  EDBM_flag_disable_all(&em, BM_ELEM_SELECT);
  for (BMFace *face : to_select) {
    BM_face_select_set(&bm, face, true);
  }
}

static void face_selection_changed(bContext *C, Object *obedit)
{
  BMEditMesh *em = BKE_editmesh_from_object(obedit);
  EDBM_selectmode_flush(em);
  EDBM_uvselect_clear(em);
  DEG_id_tag_update(static_cast<ID *>(obedit->data), ID_RECALC_SELECT);
  WM_event_add_notifier(C, NC_GEOM | ND_SELECT, obedit->data);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Face Sets From
 * \{ */

enum {
  FACE_SETS_FROM_LOOSE_PARTS = 0,
  FACE_SETS_FROM_MATERIALS = 1,
  FACE_SETS_FROM_SEAMS = 2,
  FACE_SETS_FROM_SHARP = 3,
  FACE_SETS_FROM_ANGLE = 4,
};

static const EnumPropertyItem face_sets_from_items[] = {
    {FACE_SETS_FROM_LOOSE_PARTS,
     "LOOSE_PARTS",
     ICON_FACE_SETS_FROM_LOOSE_PARTS,
     "Loose Parts",
     "One face set per connected part"},
    {FACE_SETS_FROM_MATERIALS,
     "MATERIALS",
     ICON_FACE_SETS_FROM_MATERIALS,
     "Materials",
     "One face set per material slot"},
    {FACE_SETS_FROM_SEAMS,
     "SEAMS",
     ICON_FACE_SETS_FROM_SEAMS,
     "UV Seams",
     "A new face set wherever a seam cuts"},
    {FACE_SETS_FROM_SHARP,
     "SHARP",
     ICON_FACE_SETS_FROM_SHARP,
     "Sharp Edges",
     "A new face set wherever an edge is marked sharp"},
    {FACE_SETS_FROM_ANGLE,
     "ANGLE",
     ICON_FACE_SETS_FROM_ANGLE,
     "Angle",
     "A new face set wherever faces meet at a sharper angle than the given one"},
    {0, nullptr, 0, nullptr, nullptr},
};

/** True when the faces on both sides of the edge of `l` stay in one face set. */
static bool face_sets_from_joins(const BMLoop *l, const BMFace *other, const int mode,
                                 const float angle_cos)
{
  switch (mode) {
    case FACE_SETS_FROM_SEAMS:
      return !BM_elem_flag_test(l->e, BM_ELEM_SEAM);
    case FACE_SETS_FROM_SHARP:
      return BM_elem_flag_test(l->e, BM_ELEM_SMOOTH);
    case FACE_SETS_FROM_ANGLE:
      return dot_v3v3(l->f->no, other->no) >= angle_cos;
    default:
      return true;
  }
}

static wmOperatorStatus edbm_face_sets_from_exec(bContext *C, wmOperator *op)
{
  const int mode = RNA_enum_get(op->ptr, "mode");
  const bool selected_only = RNA_boolean_get(op->ptr, "selected_only");
  const float angle_cos = cosf(RNA_float_get(op->ptr, "angle"));

  int changed = 0;
  for (Object *obedit : face_set_objects(C)) {
    BMEditMesh *em = BKE_editmesh_from_object(obedit);
    BMesh &bm = *em->bm;
    if (bm.totface == 0 || (selected_only && bm.totfacesel == 0)) {
      continue;
    }
    const Mesh &mesh = *id_cast<const Mesh *>(obedit->data);
    const int offset = face_set_offset_ensure(bm, mesh);

    auto use_face = [&](const BMFace *f) {
      if (BM_elem_flag_test(f, BM_ELEM_HIDDEN)) {
        return false;
      }
      return !selected_only || BM_elem_flag_test(f, BM_ELEM_SELECT);
    };

    /* New sets start above the default set, or above every set kept. */
    int next_set = mesh.face_sets_color_default + 1;
    BMIter iter;
    BMFace *f;
    BM_ITER_MESH (f, &iter, &bm, BM_FACES_OF_MESH) {
      if (!use_face(f)) {
        next_set = std::max(next_set, BM_ELEM_CD_GET_INT(f, offset) + 1);
      }
    }

    if (mode == FACE_SETS_FROM_MATERIALS) {
      BM_ITER_MESH (f, &iter, &bm, BM_FACES_OF_MESH) {
        if (use_face(f)) {
          BM_ELEM_CD_SET_INT(f, offset, next_set + f->mat_nr);
        }
      }
    }
    else {
      BM_mesh_elem_index_ensure(&bm, BM_FACE);
      Array<bool> done(bm.totface, false);
      Vector<BMFace *> stack;
      BM_ITER_MESH (f, &iter, &bm, BM_FACES_OF_MESH) {
        if (done[BM_elem_index_get(f)] || !use_face(f)) {
          continue;
        }
        const int set = next_set++;
        done[BM_elem_index_get(f)] = true;
        stack.append(f);
        while (!stack.is_empty()) {
          BMFace *face = stack.pop_last();
          BM_ELEM_CD_SET_INT(face, offset, set);
          BMLoop *l_first = BM_FACE_FIRST_LOOP(face);
          BMLoop *l = l_first;
          do {
            for (BMLoop *l_other = l->radial_next; l_other != l; l_other = l_other->radial_next) {
              BMFace *other = l_other->f;
              if (done[BM_elem_index_get(other)] || !use_face(other)) {
                continue;
              }
              if (!face_sets_from_joins(l, other, mode, angle_cos)) {
                continue;
              }
              done[BM_elem_index_get(other)] = true;
              stack.append(other);
            }
          } while ((l = l->next) != l_first);
        }
      }
    }
    face_sets_changed(C, obedit);
    changed++;
  }

  if (changed == 0) {
    BKE_report(op->reports, RPT_WARNING, selected_only ? "No faces selected" : "No faces");
    return OPERATOR_CANCELLED;
  }
  return OPERATOR_FINISHED;
}

void MESH_OT_face_sets_from(wmOperatorType *ot)
{
  ot->name = "Face Sets From";
  ot->idname = "MESH_OT_face_sets_from";
  ot->description = "Make face sets from the parts, materials, seams or sharp edges of the mesh";

  ot->exec = edbm_face_sets_from_exec;
  ot->poll = ED_operator_editmesh;

  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  ot->prop = RNA_def_enum(
      ot->srna, "mode", face_sets_from_items, FACE_SETS_FROM_LOOSE_PARTS, "From", "");
  RNA_def_boolean(ot->srna,
                  "selected_only",
                  false,
                  "Selected Only",
                  "Only change the selected faces, the others keep their face sets");
  PropertyRNA *prop = RNA_def_float_rotation(ot->srna,
                                             "angle",
                                             0,
                                             nullptr,
                                             DEG2RADF(0.01f),
                                             DEG2RADF(180.0f),
                                             "Angle",
                                             "Faces meeting at a sharper angle get other sets",
                                             DEG2RADF(1.0f),
                                             DEG2RADF(180.0f));
  RNA_def_property_float_default(prop, DEG2RADF(30.0f));
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Select Face Set Boundaries
 * \{ */

static wmOperatorStatus edbm_face_set_select_boundary_exec(bContext *C, wmOperator *op)
{
  int found = 0;
  for (Object *obedit : face_set_objects(C)) {
    BMEditMesh *em = BKE_editmesh_from_object(obedit);
    BMesh &bm = *em->bm;
    const int offset = face_set_offset_get(bm);
    if (offset == -1) {
      continue;
    }
    Vector<BMEdge *> edges;
    BMIter iter;
    BMEdge *e;
    BM_ITER_MESH (e, &iter, &bm, BM_EDGES_OF_MESH) {
      if (BM_elem_flag_test(e, BM_ELEM_HIDDEN) || e->l == nullptr) {
        continue;
      }
      const int set = BM_ELEM_CD_GET_INT(e->l->f, offset);
      for (BMLoop *l = e->l->radial_next; l != e->l; l = l->radial_next) {
        if (BM_ELEM_CD_GET_INT(l->f, offset) != set) {
          edges.append(e);
          break;
        }
      }
    }
    EDBM_flag_disable_all(em, BM_ELEM_SELECT);
    for (BMEdge *edge : edges) {
      BM_edge_select_set(&bm, edge, true);
    }
    found += edges.size();
    face_selection_changed(C, obedit);
  }

  if (found == 0) {
    BKE_report(op->reports, RPT_WARNING, "No face set boundaries found");
    return OPERATOR_CANCELLED;
  }
  /* An edge-only selection needs edge select mode. */
  EDBM_selectmode_disable_multi(C, SCE_SELECT_FACE, SCE_SELECT_EDGE);
  return OPERATOR_FINISHED;
}

void MESH_OT_face_set_select_boundary(wmOperatorType *ot)
{
  ot->name = "Select Face Set Boundaries";
  ot->idname = "MESH_OT_face_set_select_boundary";
  ot->description = "Select the edges where one face set meets another";

  ot->exec = edbm_face_set_select_boundary_exec;
  ot->poll = ED_operator_editmesh;

  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Grow and Shrink by Face Set
 * \{ */

static wmOperatorStatus edbm_face_set_select_more_less(bContext *C, wmOperator *op, bool more)
{
  bool any_selected = false;
  for (Object *obedit : face_set_objects(C)) {
    BMEditMesh *em = BKE_editmesh_from_object(obedit);
    BMesh &bm = *em->bm;
    if (bm.totfacesel == 0) {
      continue;
    }
    any_selected = true;
    const int offset = face_set_offset_get(bm);
    const int default_set = face_set_default(obedit);

    /* Sets of the selection, and sets of the faces around its border. */
    Set<int> selected_sets = face_sets_of_selection(bm, offset, default_set);
    Set<int> border_sets;
    BMIter iter;
    BMFace *f;
    BM_ITER_MESH (f, &iter, &bm, BM_FACES_OF_MESH) {
      if (!face_selected(f)) {
        continue;
      }
      BMLoop *l_first = BM_FACE_FIRST_LOOP(f);
      BMLoop *l = l_first;
      do {
        for (BMLoop *l_other = l->radial_next; l_other != l; l_other = l_other->radial_next) {
          if (!BM_elem_flag_test(l_other->f, BM_ELEM_HIDDEN) &&
              !BM_elem_flag_test(l_other->f, BM_ELEM_SELECT))
          {
            /* Growing adds the set across the border, shrinking drops the set inside. */
            border_sets.add(face_set_get(more ? l_other->f : f, offset, default_set));
          }
        }
      } while ((l = l->next) != l_first);
    }

    face_select_only(*em, [&](const BMFace *face) {
      const int set = face_set_get(face, offset, default_set);
      if (more) {
        return selected_sets.contains(set) || border_sets.contains(set);
      }
      return BM_elem_flag_test(face, BM_ELEM_SELECT) && !border_sets.contains(set);
    });
    face_selection_changed(C, obedit);
  }

  if (!any_selected) {
    BKE_report(op->reports, RPT_WARNING, "No faces selected");
    return OPERATOR_CANCELLED;
  }
  return OPERATOR_FINISHED;
}

static wmOperatorStatus edbm_face_set_select_more_exec(bContext *C, wmOperator *op)
{
  return edbm_face_set_select_more_less(C, op, true);
}

static wmOperatorStatus edbm_face_set_select_less_exec(bContext *C, wmOperator *op)
{
  return edbm_face_set_select_more_less(C, op, false);
}

void MESH_OT_face_set_select_more(wmOperatorType *ot)
{
  ot->name = "Grow Face Set Selection";
  ot->idname = "MESH_OT_face_set_select_more";
  ot->description = "Add the whole face sets next to the selection";

  ot->exec = edbm_face_set_select_more_exec;
  ot->poll = ED_operator_editmesh;

  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

void MESH_OT_face_set_select_less(wmOperatorType *ot)
{
  ot->name = "Shrink Face Set Selection";
  ot->idname = "MESH_OT_face_set_select_less";
  ot->description = "Remove the whole face sets at the border of the selection";

  ot->exec = edbm_face_set_select_less_exec;
  ot->poll = ED_operator_editmesh;

  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Merge Face Sets
 * \{ */

static wmOperatorStatus edbm_face_set_merge_exec(bContext *C, wmOperator *op)
{
  bool any_selected = false;
  for (Object *obedit : face_set_objects(C)) {
    BMEditMesh *em = BKE_editmesh_from_object(obedit);
    BMesh &bm = *em->bm;
    if (bm.totfacesel == 0) {
      continue;
    }
    any_selected = true;
    const Mesh &mesh = *id_cast<const Mesh *>(obedit->data);
    const int offset = face_set_offset_ensure(bm, mesh);
    const Set<int> sets = face_sets_of_selection(bm, offset, mesh.face_sets_color_default);

    /* Into the set of the active face, or the lowest selected one. */
    int target = INT_MAX;
    for (const int set : sets) {
      target = std::min(target, set);
    }
    if (bm.act_face && face_selected(bm.act_face)) {
      target = BM_ELEM_CD_GET_INT(bm.act_face, offset);
    }

    BMIter iter;
    BMFace *f;
    BM_ITER_MESH (f, &iter, &bm, BM_FACES_OF_MESH) {
      if (sets.contains(BM_ELEM_CD_GET_INT(f, offset))) {
        BM_ELEM_CD_SET_INT(f, offset, target);
      }
    }
    face_sets_changed(C, obedit);
  }

  if (!any_selected) {
    BKE_report(op->reports, RPT_WARNING, "No faces selected");
    return OPERATOR_CANCELLED;
  }
  return OPERATOR_FINISHED;
}

void MESH_OT_face_set_merge(wmOperatorType *ot)
{
  ot->name = "Merge Face Sets";
  ot->idname = "MESH_OT_face_set_merge";
  ot->description =
      "Join the whole face sets of the selected faces into the face set of the active face";

  ot->exec = edbm_face_set_merge_exec;
  ot->poll = ED_operator_editmesh;

  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Hide Other Face Sets
 * \{ */

static wmOperatorStatus edbm_face_set_hide_others_exec(bContext *C, wmOperator *op)
{
  bool any_selected = false;
  for (Object *obedit : face_set_objects(C)) {
    BMEditMesh *em = BKE_editmesh_from_object(obedit);
    BMesh &bm = *em->bm;
    if (bm.totfacesel == 0) {
      continue;
    }
    any_selected = true;
    const int offset = face_set_offset_get(bm);
    const int default_set = face_set_default(obedit);
    const Set<int> sets = face_sets_of_selection(bm, offset, default_set);

    /* Select the whole sets, then hide everything else. */
    face_select_only(*em, [&](const BMFace *face) {
      return sets.contains(face_set_get(face, offset, default_set));
    });
    EDBM_selectmode_flush(em);
    EDBM_mesh_hide(em, true);

    EDBMUpdate_Params params{};
    params.calc_looptris = true;
    params.calc_normals = false;
    params.is_destructive = false;
    EDBM_update(id_cast<Mesh *>(obedit->data), &params);
    WM_event_add_notifier(C, NC_GEOM | ND_SELECT, obedit->data);
  }

  if (!any_selected) {
    BKE_report(op->reports, RPT_WARNING, "No faces selected");
    return OPERATOR_CANCELLED;
  }
  return OPERATOR_FINISHED;
}

void MESH_OT_face_set_hide_others(wmOperatorType *ot)
{
  ot->name = "Hide Other Face Sets";
  ot->idname = "MESH_OT_face_set_hide_others";
  ot->description = "Hide every face that isn't in the face sets of the selection";

  ot->exec = edbm_face_set_hide_others_exec;
  ot->poll = ED_operator_editmesh;

  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Randomize Face Set Colors
 * \{ */

static wmOperatorStatus edbm_face_set_randomize_colors_exec(bContext *C, wmOperator * /*op*/)
{
  for (Object *obedit : face_set_objects(C)) {
    Mesh *mesh = id_cast<Mesh *>(obedit->data);
    mesh->face_sets_color_seed += 1;
    DEG_id_tag_update(&mesh->id, ID_RECALC_GEOMETRY);
    WM_event_add_notifier(C, NC_GEOM | ND_DATA, mesh);
  }
  return OPERATOR_FINISHED;
}

void MESH_OT_face_set_randomize_colors(wmOperatorType *ot)
{
  ot->name = "Randomize Face Set Colors";
  ot->idname = "MESH_OT_face_set_randomize_colors";
  ot->description = "Give the face sets new colors, the sets themselves stay the same";

  ot->exec = edbm_face_set_randomize_colors_exec;
  ot->poll = ED_operator_editmesh;

  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Face Sets to Vertex Groups
 * \{ */

/** The sets of the mesh, lowest first, without the default set. */
static Vector<int> face_sets_sorted(BMesh &bm, const int offset, const int default_set)
{
  Set<int> sets;
  BMIter iter;
  BMFace *f;
  BM_ITER_MESH (f, &iter, &bm, BM_FACES_OF_MESH) {
    const int set = BM_ELEM_CD_GET_INT(f, offset);
    if (set != default_set) {
      sets.add(set);
    }
  }
  Vector<int> sorted(sets.begin(), sets.end());
  std::sort(sorted.begin(), sorted.end());
  return sorted;
}

static wmOperatorStatus edbm_face_sets_to_vertex_groups_exec(bContext *C, wmOperator *op)
{
  int made = 0;
  for (Object *obedit : face_set_objects(C)) {
    BMEditMesh *em = BKE_editmesh_from_object(obedit);
    BMesh &bm = *em->bm;
    const int offset = face_set_offset_get(bm);
    if (offset == -1) {
      continue;
    }
    const Vector<int> sets = face_sets_sorted(bm, offset, face_set_default(obedit));
    if (sets.is_empty()) {
      continue;
    }

    Map<int, int> group_of_set;
    for (const int set : sets) {
      const std::string name = fmt::format("Face Set {}", set);
      int index = BKE_object_defgroup_name_index(obedit, name);
      if (index == -1) {
        BKE_object_defgroup_add_name(obedit, name.c_str());
        index = BKE_object_defgroup_name_index(obedit, name);
      }
      group_of_set.add(set, index);
    }

    if (CustomData_get_offset(&bm.vdata, CD_MDEFORMVERT) == -1) {
      BM_data_layer_add(&bm, &bm.vdata, CD_MDEFORMVERT);
    }
    const int dvert_offset = CustomData_get_offset(&bm.vdata, CD_MDEFORMVERT);

    BMIter iter;
    BMFace *f;
    BM_ITER_MESH (f, &iter, &bm, BM_FACES_OF_MESH) {
      const int *group = group_of_set.lookup_ptr(BM_ELEM_CD_GET_INT(f, offset));
      if (group == nullptr) {
        continue;
      }
      BMLoop *l_first = BM_FACE_FIRST_LOOP(f);
      BMLoop *l = l_first;
      do {
        MDeformVert *dvert = static_cast<MDeformVert *>(BM_ELEM_CD_GET_VOID_P(l->v, dvert_offset));
        BKE_defvert_ensure_index(dvert, *group)->weight = 1.0f;
      } while ((l = l->next) != l_first);
    }

    made += sets.size();
    face_sets_changed(C, obedit);
    DEG_id_tag_update(&obedit->id, ID_RECALC_GEOMETRY);
    WM_event_add_notifier(C, NC_OBJECT | ND_DRAW, obedit);
  }

  if (made == 0) {
    BKE_report(op->reports, RPT_WARNING, "No face sets other than the default one");
    return OPERATOR_CANCELLED;
  }
  BKE_reportf(op->reports, RPT_INFO, "Made vertex groups for %d face sets", made);
  return OPERATOR_FINISHED;
}

void MESH_OT_face_sets_to_vertex_groups(wmOperatorType *ot)
{
  ot->name = "Face Sets to Vertex Groups";
  ot->idname = "MESH_OT_face_sets_to_vertex_groups";
  ot->description = "Make one vertex group per face set, named after it";

  ot->exec = edbm_face_sets_to_vertex_groups_exec;
  ot->poll = ED_operator_editmesh;

  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Face Sets to Materials
 * \{ */

static wmOperatorStatus edbm_face_sets_to_materials_exec(bContext *C, wmOperator *op)
{
  Main *bmain = CTX_data_main(C);
  int made = 0;
  for (Object *obedit : face_set_objects(C)) {
    BMEditMesh *em = BKE_editmesh_from_object(obedit);
    BMesh &bm = *em->bm;
    const int offset = face_set_offset_get(bm);
    if (offset == -1) {
      continue;
    }
    const Mesh &mesh = *id_cast<const Mesh *>(obedit->data);
    const Vector<int> sets = face_sets_sorted(bm, offset, mesh.face_sets_color_default);
    if (sets.is_empty()) {
      continue;
    }

    Map<int, int> slot_of_set;
    for (const int set : sets) {
      const std::string name = fmt::format("Face Set {}", set);
      Material *ma = BKE_material_add(bmain, name.c_str());
      /* The viewport color is the face set color. */
      uchar color[4];
      BKE_paint_face_set_overlay_color_get(set, mesh.face_sets_color_seed, color);
      rgb_uchar_to_float(&ma->r, color);
      srgb_to_linearrgb_v3_v3(&ma->r, &ma->r);
      BKE_object_material_slot_add(bmain, obedit);
      BKE_object_material_assign(bmain, obedit, ma, obedit->totcol, BKE_MAT_ASSIGN_USERPREF);
      /* Only the slot uses it. */
      id_us_min(&ma->id);
      slot_of_set.add(set, obedit->totcol - 1);
    }

    BMIter iter;
    BMFace *f;
    BM_ITER_MESH (f, &iter, &bm, BM_FACES_OF_MESH) {
      if (const int *slot = slot_of_set.lookup_ptr(BM_ELEM_CD_GET_INT(f, offset))) {
        f->mat_nr = short(*slot);
      }
    }

    made += sets.size();
    EDBMUpdate_Params params{};
    params.calc_looptris = true;
    params.calc_normals = false;
    params.is_destructive = false;
    EDBM_update(id_cast<Mesh *>(obedit->data), &params);
    DEG_id_tag_update(&obedit->id, ID_RECALC_GEOMETRY);
    WM_event_add_notifier(C, NC_OBJECT | ND_DRAW, obedit);
    WM_event_add_notifier(C, NC_MATERIAL | ND_SHADING_LINKS, nullptr);
  }

  if (made == 0) {
    BKE_report(op->reports, RPT_WARNING, "No face sets other than the default one");
    return OPERATOR_CANCELLED;
  }
  DEG_relations_tag_update(bmain);
  BKE_reportf(op->reports, RPT_INFO, "Made materials for %d face sets", made);
  return OPERATOR_FINISHED;
}

void MESH_OT_face_sets_to_materials(wmOperatorType *ot)
{
  ot->name = "Face Sets to Materials";
  ot->idname = "MESH_OT_face_sets_to_materials";
  ot->description =
      "Give each face set a new material in a new slot, colored like the face set";

  ot->exec = edbm_face_sets_to_materials_exec;
  ot->poll = ED_operator_editmesh;

  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/** \} */

}  // namespace blender
