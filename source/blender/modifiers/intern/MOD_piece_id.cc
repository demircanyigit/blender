/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Piece ID: write a whole number per face or point that says which piece it belongs to, from
 * islands, split edges, materials, face sets, a vertex group, another attribute or the selection.
 * Modifiers further down the stack, like Collapse, use it to know what stays together.
 */

#include <climits>
#include <string>

#include "BLI_array.hh"
#include "BLI_disjoint_set.hh"
#include "BLI_map.hh"
#include "BLI_noise.hh"
#include "BLI_string.h"

#include "BLT_translation.hh"

#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_types.h"
#include "DNA_screen_types.h"

#include "BKE_attribute.hh"
#include "BKE_deform.hh"
#include "BKE_mesh.hh"
#include "BKE_modifier.hh"

#include "DEG_depsgraph_query.hh"

#include "UI_interface_layout.hh"
#include "UI_resources.hh"

#include "RNA_access.hh"
#include "RNA_prototypes.hh"

#include "MOD_modifiertypes.hh"
#include "MOD_ui_common.hh"
#include "MOD_util.hh"

namespace blender {

enum PieceIdSource {
  PIECE_ID_ISLANDS = 0,
  PIECE_ID_EDGES = 1,
  PIECE_ID_MATERIAL = 2,
  PIECE_ID_FACE_SETS = 3,
  PIECE_ID_VERTEX_GROUP = 4,
  PIECE_ID_ATTRIBUTE = 5,
  PIECE_ID_SELECTION = 6,
};

static void init_data(ModifierData *md)
{
  PieceIdModifierData *data = reinterpret_cast<PieceIdModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

static void required_data_mask(ModifierData *md, CustomData_MeshMasks *r_cddata_masks)
{
  const PieceIdModifierData *pmd = reinterpret_cast<PieceIdModifierData *>(md);
  if (pmd->source == PIECE_ID_VERTEX_GROUP && pmd->vertex_group[0] != '\0') {
    r_cddata_masks->vmask |= CD_MASK_MDEFORMVERT;
  }
}

/** IDs of the pieces, either one per point or one per face. */
struct PieceIds {
  Array<int> values;
  bool on_faces = true;
};

static PieceIds islands(const Mesh &mesh)
{
  DisjointSet<int> sets(mesh.verts_num);
  for (const int2 edge : mesh.edges()) {
    sets.join(edge[0], edge[1]);
  }
  PieceIds ids;
  ids.on_faces = false;
  ids.values.reinitialize(mesh.verts_num);
  for (const int v : IndexRange(mesh.verts_num)) {
    ids.values[v] = sets.find_root(v);
  }
  return ids;
}

/** Faces that share an edge belong together, unless the edge is marked to split them. */
static PieceIds split_edges(const Mesh &mesh, const bool seams, const bool sharp)
{
  const bke::AttributeAccessor attributes = mesh.attributes();
  const VArraySpan<bool> seam = *attributes.lookup_or_default<bool>(
      "uv_seam", bke::AttrDomain::Edge, false);
  const VArraySpan<bool> sharp_edge = *attributes.lookup_or_default<bool>(
      "sharp_edge", bke::AttrDomain::Edge, false);
  const OffsetIndices<int> faces = mesh.faces();
  const Span<int> corner_edges = mesh.corner_edges();
  DisjointSet<int> sets(mesh.faces_num);
  Array<int> first_face(mesh.edges_num, -1);
  for (const int face : faces.index_range()) {
    for (const int edge : corner_edges.slice(faces[face])) {
      if ((seams && seam[edge]) || (sharp && sharp_edge[edge])) {
        continue;
      }
      if (first_face[edge] == -1) {
        first_face[edge] = face;
      }
      else {
        sets.join(first_face[edge], face);
      }
    }
  }
  PieceIds ids;
  ids.values.reinitialize(mesh.faces_num);
  for (const int face : faces.index_range()) {
    ids.values[face] = sets.find_root(face);
  }
  return ids;
}

static PieceIds face_attribute(const Mesh &mesh, const StringRef name)
{
  PieceIds ids;
  ids.values.reinitialize(mesh.faces_num);
  const VArray<int> values = *mesh.attributes().lookup_or_default<int>(
      name, bke::AttrDomain::Face, 0);
  values.materialize(ids.values);
  return ids;
}

static PieceIds face_selection(const Mesh &mesh)
{
  PieceIds ids;
  ids.values.reinitialize(mesh.faces_num);
  const VArray<bool> selected = *mesh.attributes().lookup_or_default<bool>(
      ".select_poly", bke::AttrDomain::Face, false);
  for (const int face : ids.values.index_range()) {
    ids.values[face] = selected[face] ? 1 : 0;
  }
  return ids;
}

static PieceIds vertex_group(const PieceIdModifierData &pmd,
                             const ModifierEvalContext *ctx,
                             const Mesh &mesh)
{
  PieceIds ids;
  const MDeformVert *dvert = nullptr;
  int defgrp_index = -1;
  MOD_get_vgroup(ctx->object, &mesh, pmd.vertex_group, &dvert, &defgrp_index);
  const bool invert = pmd.flag & MOD_PIECE_ID_INVERT_VGROUP;
  auto weight = [&](const int v) {
    return dvert ? BKE_defvert_find_weight(&dvert[v], defgrp_index) : 0.0f;
  };
  if (pmd.domain == MOD_PIECE_ID_DOMAIN_POINT) {
    ids.on_faces = false;
    ids.values.reinitialize(mesh.verts_num);
    for (const int v : IndexRange(mesh.verts_num)) {
      ids.values[v] = (weight(v) >= pmd.threshold) != invert;
    }
    return ids;
  }
  const OffsetIndices<int> faces = mesh.faces();
  const Span<int> corner_verts = mesh.corner_verts();
  ids.values.reinitialize(mesh.faces_num);
  for (const int face : faces.index_range()) {
    float sum = 0.0f;
    for (const int v : corner_verts.slice(faces[face])) {
      sum += weight(v);
    }
    ids.values[face] = (sum / float(faces[face].size()) >= pmd.threshold) != invert;
  }
  return ids;
}

static PieceIds other_attribute(const Mesh &mesh, const StringRef name)
{
  const bke::AttributeAccessor attributes = mesh.attributes();
  const std::optional<bke::AttributeMetaData> meta = attributes.lookup_meta_data(name);
  PieceIds ids;
  if (meta && meta->domain == bke::AttrDomain::Point) {
    ids.on_faces = false;
    ids.values.reinitialize(mesh.verts_num);
    attributes.lookup_or_default<int>(name, bke::AttrDomain::Point, 0).varray.materialize(
        ids.values);
    return ids;
  }
  return face_attribute(mesh, name);
}

/** The IDs moved to the domain that is written. A point takes the ID of its first face, a face
 * the ID of its first point. */
static Array<int> to_domain(const Mesh &mesh, const PieceIds &ids, const bool faces_wanted)
{
  if (ids.on_faces == faces_wanted) {
    return ids.values;
  }
  const OffsetIndices<int> faces = mesh.faces();
  const Span<int> corner_verts = mesh.corner_verts();
  if (faces_wanted) {
    Array<int> result(mesh.faces_num);
    for (const int face : faces.index_range()) {
      result[face] = ids.values[corner_verts[faces[face].first()]];
    }
    return result;
  }
  /* Points without faces get one ID past every face ID, so they stay apart. */
  Array<int> result(mesh.verts_num, INT_MIN);
  for (const int face : faces.index_range()) {
    for (const int v : corner_verts.slice(faces[face])) {
      if (result[v] == INT_MIN) {
        result[v] = ids.values[face];
      }
    }
  }
  return result;
}

static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  const PieceIdModifierData &pmd = *reinterpret_cast<PieceIdModifierData *>(md);
  if (pmd.name[0] == '\0' || mesh->verts_num == 0) {
    return mesh;
  }
  const bool on_faces = pmd.domain == MOD_PIECE_ID_DOMAIN_FACE;
  const bke::AttrDomain domain = on_faces ? bke::AttrDomain::Face : bke::AttrDomain::Point;

  PieceIds ids;
  switch (pmd.source) {
    case PIECE_ID_ISLANDS:
      ids = islands(*mesh);
      break;
    case PIECE_ID_EDGES:
      ids = split_edges(
          *mesh, pmd.flag & MOD_PIECE_ID_SEAMS, pmd.flag & MOD_PIECE_ID_SHARP);
      break;
    case PIECE_ID_MATERIAL:
      ids = face_attribute(*mesh, "material_index");
      break;
    case PIECE_ID_FACE_SETS:
      ids = face_attribute(*mesh, ".sculpt_face_set");
      break;
    case PIECE_ID_VERTEX_GROUP:
      ids = vertex_group(pmd, ctx, *mesh);
      break;
    case PIECE_ID_ATTRIBUTE:
      ids = other_attribute(*mesh, pmd.source_name);
      break;
    case PIECE_ID_SELECTION:
      ids = face_selection(*mesh);
      break;
    default:
      ids = islands(*mesh);
      break;
  }
  Array<int> values = to_domain(*mesh, ids, on_faces);

  bke::MutableAttributeAccessor attributes = mesh->attributes_for_write();

  /* Combine: split the pieces already there further, so every pair of old and new gets its own
   * number. Then number them from 0 in the order they first appear. */
  Map<int64_t, int> numbers;
  if (pmd.flag & MOD_PIECE_ID_COMBINE) {
    const VArray<int> old = *attributes.lookup_or_default<int>(pmd.name, domain, 0);
    for (const int i : values.index_range()) {
      const int64_t key = (int64_t(old[i]) << 32) | uint32_t(values[i]);
      values[i] = numbers.lookup_or_add(key, int(numbers.size()));
    }
  }
  else {
    for (int &value : values) {
      value = numbers.lookup_or_add(int64_t(value), int(numbers.size()));
    }
  }

  if (pmd.flag & MOD_PIECE_ID_RANDOM) {
    Array<float> random(values.size());
    for (const int i : values.index_range()) {
      random[i] = noise::hash_to_float(uint32_t(values[i]), uint32_t(pmd.seed));
    }
    const std::string random_name = std::string(pmd.name) + "_random";
    attributes.remove(random_name);
    attributes.add<float>(random_name,
                          domain,
                          bke::AttributeInitVArray(
                              VArray<float>::from_container(std::move(random))));
  }

  const int count = int(numbers.size());
  attributes.remove(pmd.name);
  attributes.add<int>(
      pmd.name, domain, bke::AttributeInitVArray(VArray<int>::from_container(std::move(values))));

  if (DEG_is_active(ctx->depsgraph)) {
    PieceIdModifierData *orig = reinterpret_cast<PieceIdModifierData *>(
        BKE_modifier_get_original(ctx->object, md));
    if (orig != nullptr) {
      orig->count_num = count;
    }
  }
  return mesh;
}

static void panel_draw(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);

  layout.use_property_split_set(true);
  layout.prop(ptr, "name", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "domain", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "source", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  switch (RNA_enum_get(ptr, "source")) {
    case PIECE_ID_EDGES: {
      ui::Layout &col = layout.column(true, IFACE_("Split"));
      col.prop(ptr, "use_seams", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      col.prop(ptr, "use_sharp", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      break;
    }
    case PIECE_ID_VERTEX_GROUP:
      modifier_vgroup_ui(
          layout, ptr, &ob_ptr, "vertex_group", "invert_vertex_group", std::nullopt);
      layout.prop(ptr, "threshold", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      break;
    case PIECE_ID_ATTRIBUTE:
      layout.prop(ptr, "source_name", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      break;
    case PIECE_ID_FACE_SETS: {
      /* Set the face sets of the selected faces in Edit Mode. */
      ui::Layout &row = layout.row(true);
      row.op("MESH_OT_face_set_new", IFACE_("New"), ICON_FACE_SET_NEW);
      row.op("MESH_OT_face_set_select", IFACE_("Select"), ICON_FACE_SET_SELECT);
      row.op("MESH_OT_face_set_clear", IFACE_("Clear"), ICON_FACE_SET_CLEAR);
      ui::Layout &row_more = layout.row(true);
      row_more.op("MESH_OT_face_set_merge", IFACE_("Merge"), ICON_FACE_SET_MERGE);
      row_more.menu("VIEW3D_MT_edit_mesh_face_sets_from", IFACE_("From"), ICON_NONE);
      break;
    }
    default:
      break;
  }

  layout.prop(ptr, "use_combine", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  ui::Layout &row = layout.row(true, IFACE_("Random"));
  row.prop(ptr, "use_random", UI_ITEM_NONE, "", ICON_NONE);
  ui::Layout &seed = row.row(true);
  seed.active_set(RNA_boolean_get(ptr, "use_random"));
  seed.prop(ptr, "seed", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  const int count = RNA_int_get(ptr, "count");
  if (count > 0) {
    char text[64];
    SNPRINTF(text, IFACE_("Pieces: %d"), count);
    layout.label(text, ICON_NONE);
  }
  modifier_error_message_draw(layout, ptr);
}

static void panel_register(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_PieceId, panel_draw);
}

ModifierTypeInfo modifierType_PieceId = {
    /*idname*/ "PieceId",
    /*name*/ N_("Piece ID"),
    /*struct_name*/ "PieceIdModifierData",
    /*struct_size*/ sizeof(PieceIdModifierData),
    /*srna*/ &RNA_PieceIdModifier,
    /*type*/ ModifierTypeType::Nonconstructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode |
        eModifierTypeFlag_EnableInEditmode,
    /*icon*/ ICON_MOD_PIECE_ID,

    /*copy_data*/ BKE_modifier_copydata_generic,

    /*deform_verts*/ nullptr,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ modify_mesh,
    /*modify_geometry_set*/ nullptr,

    /*init_data*/ init_data,
    /*required_data_mask*/ required_data_mask,
    /*free_data*/ nullptr,
    /*is_disabled*/ nullptr,
    /*update_depsgraph*/ nullptr,
    /*depends_on_time*/ nullptr,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ nullptr,
    /*foreach_tex_link*/ nullptr,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ panel_register,
    /*blend_write*/ nullptr,
    /*blend_read*/ nullptr,
    /*foreach_cache*/ nullptr,
    /*foreach_working_space_color*/ nullptr,
};

}  // namespace blender
