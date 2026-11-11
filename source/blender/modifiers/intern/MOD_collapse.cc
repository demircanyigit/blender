/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Let every mesh island fall as a rigid piece and keep the pose where the pieces came to rest.
 */

#include <climits>

#include "BLI_array.hh"
#include "BLI_index_mask.hh"
#include "BLI_math_matrix.hh"

#include "BLT_translation.hh"

#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_types.h"
#include "DNA_screen_types.h"

#include "BKE_attribute.hh"
#include "BKE_deform.hh"
#include "BKE_lib_id.hh"
#include "BKE_lib_query.hh"
#include "BKE_mesh.hh"
#include "BKE_mesh_wrapper.hh"
#include "BKE_modifier.hh"

#include "BLO_read_write.hh"

#include "DEG_depsgraph_build.hh"
#include "DEG_depsgraph_query.hh"

#include "UI_interface_layout.hh"
#include "UI_resources.hh"

#include "RNA_access.hh"
#include "RNA_prototypes.hh"

#include "GEO_mesh_collapse.hh"
#include "GEO_mesh_split_edges.hh"

#include "MOD_colliders.hh"
#include "MOD_modifiertypes.hh"
#include "MOD_ui_common.hh"
#include "MOD_util.hh"

namespace blender {

static void init_data(ModifierData *md)
{
  CollapseModifierData *data = reinterpret_cast<CollapseModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

static void required_data_mask(ModifierData *md, CustomData_MeshMasks *r_cddata_masks)
{
  const CollapseModifierData *cmd = reinterpret_cast<CollapseModifierData *>(md);
  if (cmd->vertex_group[0] != '\0') {
    r_cddata_masks->vmask |= CD_MASK_MDEFORMVERT;
  }
}

static void foreach_ID_link(ModifierData *md, Object *ob, IDWalkFunc walk, void *user_data)
{
  CollapseModifierData *cmd = reinterpret_cast<CollapseModifierData *>(md);
  walk(user_data, ob, reinterpret_cast<ID **>(&cmd->target), IDWALK_CB_NOP);
  MOD_collider_sources_foreach_ID(cmd->colliders, ob, walk, user_data);
}

static void copy_data(const ModifierData *md, ModifierData *target, const int flag)
{
  BKE_modifier_copydata_generic(md, target, flag);
  const CollapseModifierData &src = *reinterpret_cast<const CollapseModifierData *>(md);
  CollapseModifierData &dst = *reinterpret_cast<CollapseModifierData *>(target);
  MOD_collider_sources_copy(dst.colliders, src.colliders);
}

static void free_data(ModifierData *md)
{
  MOD_collider_sources_free(reinterpret_cast<CollapseModifierData *>(md)->colliders);
}

static void blend_write(BlendWriter *writer, const ID * /*owner*/, const ModifierData *md)
{
  const CollapseModifierData *cmd = reinterpret_cast<const CollapseModifierData *>(md);
  writer->write_struct(cmd);
  MOD_collider_sources_blend_write(writer, cmd->colliders);
}

static void blend_read(BlendDataReader *reader, ModifierData *md)
{
  MOD_collider_sources_blend_read(reader,
                                  reinterpret_cast<CollapseModifierData *>(md)->colliders);
}

static void update_depsgraph(ModifierData *md, const ModifierUpdateDepsgraphContext *ctx)
{
  const CollapseModifierData *cmd = reinterpret_cast<CollapseModifierData *>(md);
  MOD_colliders_update_depsgraph(cmd->target, cmd->colliders, ctx, "Collapse Modifier");
  /* Down is down in the world, so the object's turn and place matter. */
  DEG_add_depends_on_transform_relation(ctx->node, "Collapse Modifier");
}

/**
 * The piece of each point, from an attribute. A face attribute first splits the mesh where faces
 * of different pieces meet, so every point belongs to one piece; the split mesh is returned in
 * `r_split` and must be used and freed by the caller. Returns false when there is no attribute.
 */
static bool pieces_from_attribute(const Mesh &mesh,
                                  const StringRef name,
                                  Mesh **r_split,
                                  Array<int> &r_pieces)
{
  *r_split = nullptr;
  const std::optional<bke::AttributeMetaData> meta = mesh.attributes().lookup_meta_data(name);
  if (!meta) {
    return false;
  }
  if (meta->domain == bke::AttrDomain::Point) {
    r_pieces.reinitialize(mesh.verts_num);
    mesh.attributes().lookup_or_default<int>(name, bke::AttrDomain::Point, 0).varray.materialize(
        r_pieces);
    return true;
  }

  /* Edges between faces of different pieces. */
  const OffsetIndices<int> faces = mesh.faces();
  const Span<int> corner_edges = mesh.corner_edges();
  const VArraySpan<int> face_pieces = *mesh.attributes().lookup_or_default<int>(
      name, bke::AttrDomain::Face, 0);
  Array<int> first_face(mesh.edges_num, -1);
  Array<bool> split(mesh.edges_num, false);
  for (const int face : faces.index_range()) {
    for (const int edge : corner_edges.slice(faces[face])) {
      if (first_face[edge] == -1) {
        first_face[edge] = face;
      }
      else if (face_pieces[first_face[edge]] != face_pieces[face]) {
        split[edge] = true;
      }
    }
  }
  const Mesh *source = &mesh;
  IndexMaskMemory memory;
  const IndexMask split_mask = IndexMask::from_bools(split, memory);
  if (!split_mask.is_empty()) {
    *r_split = BKE_mesh_copy_for_eval(mesh);
    geometry::split_edges(**r_split, split_mask);
    source = *r_split;
  }

  /* A point takes the piece of its faces. Points without faces stay together as one piece. */
  const OffsetIndices<int> split_faces = source->faces();
  const Span<int> corner_verts = source->corner_verts();
  const VArraySpan<int> split_pieces = *source->attributes().lookup_or_default<int>(
      name, bke::AttrDomain::Face, 0);
  r_pieces.reinitialize(source->verts_num);
  r_pieces.fill(INT_MIN);
  for (const int face : split_faces.index_range()) {
    for (const int v : corner_verts.slice(split_faces[face])) {
      r_pieces[v] = split_pieces[face];
    }
  }
  return true;
}

static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  const CollapseModifierData &cmd = *reinterpret_cast<CollapseModifierData *>(md);
  if (mesh->verts_num == 0) {
    return mesh;
  }

  geometry::MeshCollapseParams params;
  params.time = cmd.time;
  params.gravity = cmd.gravity;
  params.push = cmd.push;
  params.seed = cmd.seed;
  params.friction = cmd.friction;
  params.bounce = cmd.bounce;
  params.substeps = cmd.substeps;
  params.mesh_to_space = ctx->object->object_to_world();

  /* Every collider joined, already in world space. */
  Mesh *ground = MOD_colliders_mesh(cmd.target, cmd.colliders, ctx);
  params.ground = ground;
  params.ground_to_space = float4x4::identity();
  params.ground_cage = cmd.cage;

  /* Pieces from an attribute, which may split the mesh first. */
  Mesh *split = nullptr;
  Array<int> pieces;
  if (cmd.pieces == MOD_COLLAPSE_PIECES_ATTRIBUTE) {
    if (pieces_from_attribute(*mesh, cmd.piece_name, &split, pieces)) {
      params.pieces = pieces;
    }
    else {
      BKE_modifier_set_warning(
          ctx->object, md, "No attribute \"%s\", loose parts are used", cmd.piece_name);
    }
  }
  const Mesh &input = split ? *split : *mesh;

  Array<bool> keep;
  const MDeformVert *dvert = nullptr;
  int defgrp_index = -1;
  MOD_get_vgroup(ctx->object, &input, cmd.vertex_group, &dvert, &defgrp_index);
  if (dvert != nullptr) {
    const bool invert = cmd.flag & MOD_COLLAPSE_INVERT_VGROUP;
    keep.reinitialize(input.verts_num);
    for (const int i : keep.index_range()) {
      keep[i] = (BKE_defvert_find_weight(&dvert[i], defgrp_index) > 0.0f) != invert;
    }
    params.keep = keep;
  }

  const char *error = nullptr;
  Mesh *result = geometry::mesh_collapse(input, params, &error);
  if (ground != nullptr) {
    BKE_id_free(nullptr, ground);
  }
  if (split != nullptr) {
    BKE_id_free(nullptr, split);
  }
  if (error != nullptr) {
    BKE_modifier_set_error(ctx->object, md, "%s", error);
  }
  return result ? result : mesh;
}

static void panel_draw(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);

  layout.use_property_split_set(true);
  layout.prop(ptr, "pieces", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  if (RNA_enum_get(ptr, "pieces") == MOD_COLLAPSE_PIECES_ATTRIBUTE) {
    layout.prop(ptr, "piece_name", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  layout.prop(ptr, "target", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  MOD_collider_sources_draw(layout, ptr);
  layout.prop(ptr, "cage", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  modifier_vgroup_ui(layout, ptr, &ob_ptr, "vertex_group", "invert_vertex_group", IFACE_("Keep"));

  ui::Layout &col = layout.column(true);
  col.prop(ptr, "time", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  col.prop(ptr, "gravity", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  ui::Layout &col2 = layout.column(true);
  col2.prop(ptr, "push", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  col2.prop(ptr, "seed", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  ui::Layout &col3 = layout.column(true);
  col3.prop(ptr, "friction", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  col3.prop(ptr, "bounce", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  layout.prop(ptr, "quality", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  modifier_error_message_draw(layout, ptr);
}

static void panel_register(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_Collapse, panel_draw);
}

ModifierTypeInfo modifierType_Collapse = {
    /*idname*/ "Collapse",
    /*name*/ N_("Collapse"),
    /*struct_name*/ "CollapseModifierData",
    /*struct_size*/ sizeof(CollapseModifierData),
    /*srna*/ &RNA_CollapseModifier,
    /*type*/ ModifierTypeType::Constructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode,
    /*icon*/ ICON_MOD_COLLAPSE,

    /*copy_data*/ copy_data,

    /*deform_verts*/ nullptr,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ modify_mesh,
    /*modify_geometry_set*/ nullptr,

    /*init_data*/ init_data,
    /*required_data_mask*/ required_data_mask,
    /*free_data*/ free_data,
    /*is_disabled*/ nullptr,
    /*update_depsgraph*/ update_depsgraph,
    /*depends_on_time*/ nullptr,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ foreach_ID_link,
    /*foreach_tex_link*/ nullptr,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ panel_register,
    /*blend_write*/ blend_write,
    /*blend_read*/ blend_read,
    /*foreach_cache*/ nullptr,
    /*foreach_working_space_color*/ nullptr,
};

}  // namespace blender
