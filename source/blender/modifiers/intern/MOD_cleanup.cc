/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Clean up a mesh: delete loose and duplicate parts, dissolve, fill holes, split and flatten
 * faces, and fix normals, in one go.
 */

#include "BLI_array.hh"
#include "BLI_string.h"
#include "BLI_utildefines.h"

#include "BLT_translation.hh"

#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_types.h"
#include "DNA_screen_types.h"

#include "BKE_deform.hh"
#include "BKE_mesh.hh"
#include "BKE_modifier.hh"

#include "UI_interface_layout.hh"
#include "UI_resources.hh"

#include "RNA_access.hh"
#include "RNA_prototypes.hh"

#include "DEG_depsgraph_query.hh"

#include "GEO_mesh_cleanup.hh"
#include "GEO_randomize.hh"

#include "MOD_modifiertypes.hh"
#include "MOD_ui_common.hh"
#include "MOD_util.hh"

namespace blender {

static void init_data(ModifierData *md)
{
  CleanupModifierData *data = reinterpret_cast<CleanupModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

/** Keep the counts before and after on the original modifier, for the panel. */
static void store_stats(ModifierData *md,
                        const ModifierEvalContext *ctx,
                        const Mesh &before,
                        const Mesh &after)
{
  if (!DEG_is_active(ctx->depsgraph)) {
    return;
  }
  CleanupModifierData *orig = reinterpret_cast<CleanupModifierData *>(
      BKE_modifier_get_original(ctx->object, md));
  if (orig == nullptr) {
    return;
  }
  orig->stats[0] = before.verts_num;
  orig->stats[1] = before.edges_num;
  orig->stats[2] = before.faces_num;
  orig->stats[3] = after.verts_num;
  orig->stats[4] = after.edges_num;
  orig->stats[5] = after.faces_num;
}

static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  const CleanupModifierData &cmd = *reinterpret_cast<CleanupModifierData *>(md);
  if (mesh->verts_num == 0) {
    store_stats(md, ctx, *mesh, *mesh);
    return mesh;
  }

  geometry::MeshCleanupParams params;
  params.loose_verts = cmd.flag & MOD_CLEANUP_LOOSE_VERTS;
  params.loose_edges = cmd.flag & MOD_CLEANUP_LOOSE_EDGES;
  params.loose_faces = cmd.flag & MOD_CLEANUP_LOOSE_FACES;
  params.duplicate_faces = cmd.flag & MOD_CLEANUP_DUPLICATE_FACES;
  params.interior_faces = cmd.flag & MOD_CLEANUP_INTERIOR_FACES;
  params.degenerate = cmd.flag & MOD_CLEANUP_DEGENERATE;
  params.degenerate_distance = cmd.degenerate_distance;
  params.extra_verts = cmd.flag & MOD_CLEANUP_EXTRA_VERTS;
  params.extra_verts_angle = cmd.extra_verts_angle;
  params.limited = cmd.flag & MOD_CLEANUP_LIMITED;
  params.limited_angle = cmd.limited_angle;
  params.limited_all_boundaries = cmd.flag & MOD_CLEANUP_LIMITED_ALL_BOUNDARIES;
  params.limited_delimit = cmd.limited_delimit;
  params.small_pieces = cmd.flag & MOD_CLEANUP_SMALL_PIECES;
  params.small_pieces_faces = cmd.small_pieces_faces;
  params.small_pieces_size = cmd.small_pieces_size;
  params.fill_holes = cmd.flag & MOD_CLEANUP_FILL_HOLES;
  params.fill_holes_sides = cmd.fill_holes_sides;
  params.make_planar = cmd.flag & MOD_CLEANUP_MAKE_PLANAR;
  params.make_planar_factor = cmd.make_planar_factor;
  params.make_planar_iterations = cmd.make_planar_iterations;
  params.split_nonplanar = cmd.flag & MOD_CLEANUP_SPLIT_NONPLANAR;
  params.split_nonplanar_angle = cmd.split_nonplanar_angle;
  params.split_concave = cmd.flag & MOD_CLEANUP_SPLIT_CONCAVE;
  params.recalc_normals = cmd.flag & MOD_CLEANUP_RECALC_NORMALS;

  Array<bool> selection;
  const MDeformVert *dvert = nullptr;
  int defgrp_index = -1;
  MOD_get_vgroup(ctx->object, mesh, cmd.vertex_group, &dvert, &defgrp_index);
  if (dvert != nullptr) {
    const bool invert = cmd.flag & MOD_CLEANUP_INVERT_VGROUP;
    selection.reinitialize(mesh->verts_num);
    for (const int i : selection.index_range()) {
      selection[i] = (BKE_defvert_find_weight(&dvert[i], defgrp_index) > 0.0f) != invert;
    }
  }

  Mesh *result = geometry::mesh_cleanup(*mesh, selection, params);
  if (result == nullptr) {
    store_stats(md, ctx, *mesh, *mesh);
    return mesh;
  }
  store_stats(md, ctx, *mesh, *result);
  geometry::debug_randomize_mesh_order(result);
  return result;
}

/** A step's checkbox, with its first setting on the same row. */
static void step_row(ui::Layout &layout,
                     PointerRNA *ptr,
                     const char *use,
                     const char *value,
                     const StringRef heading)
{
  ui::Layout &row = layout.row(true, heading);
  row.prop(ptr, use, UI_ITEM_NONE, "", ICON_NONE);
  ui::Layout &sub = row.row(true);
  sub.active_set(RNA_boolean_get(ptr, use));
  sub.prop(ptr, value, UI_ITEM_NONE, "", ICON_NONE);
}

static void stats_draw(ui::Layout &layout, PointerRNA *ptr)
{
  const CleanupModifierData &cmd = *static_cast<const CleanupModifierData *>(ptr->data);
  const int *s = cmd.stats;
  if (s[0] == 0 && s[1] == 0 && s[2] == 0) {
    return;
  }
  ui::Layout &col = layout.column(true);
  auto line = [&](const char *name, const int before, const int after) {
    char text[128];
    const int change = after - before;
    if (change == 0) {
      SNPRINTF(text, "%s: %d", name, after);
    }
    else {
      SNPRINTF(text, "%s: %d (%+d)", name, after, change);
    }
    col.label(text, ICON_NONE);
  };
  line(IFACE_("Vertices"), s[0], s[3]);
  line(IFACE_("Edges"), s[1], s[4]);
  line(IFACE_("Faces"), s[2], s[5]);
}

static void panel_draw(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);

  layout.use_property_split_set(true);

  ui::Layout &loose = layout.column(true, IFACE_("Delete Loose"));
  loose.prop(ptr, "use_loose_verts", UI_ITEM_NONE, IFACE_("Vertices"), ICON_NONE);
  loose.prop(ptr, "use_loose_edges", UI_ITEM_NONE, IFACE_("Edges"), ICON_NONE);
  loose.prop(ptr, "use_loose_faces", UI_ITEM_NONE, IFACE_("Faces"), ICON_NONE);

  ui::Layout &remove = layout.column(true, IFACE_("Delete"));
  remove.prop(ptr, "use_duplicate_faces", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  remove.prop(ptr, "use_interior_faces", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  step_row(layout, ptr, "use_degenerate", "degenerate_distance", IFACE_("Degenerate"));
  step_row(layout, ptr, "use_extra_verts", "extra_verts_angle", IFACE_("Extra Vertices"));

  step_row(layout, ptr, "use_limited", "limited_angle", IFACE_("Limited Dissolve"));
  if (RNA_boolean_get(ptr, "use_limited")) {
    ui::Layout &col = layout.column(true);
    col.prop(ptr, "use_limited_all_boundaries", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    col.prop(ptr, "limited_delimit", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }

  step_row(layout, ptr, "use_small_pieces", "small_pieces_faces", IFACE_("Small Pieces"));
  if (RNA_boolean_get(ptr, "use_small_pieces")) {
    layout.prop(ptr, "small_pieces_size", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }

  step_row(layout, ptr, "use_fill_holes", "fill_holes_sides", IFACE_("Fill Holes"));

  step_row(layout, ptr, "use_make_planar", "make_planar_factor", IFACE_("Make Planar"));
  if (RNA_boolean_get(ptr, "use_make_planar")) {
    layout.prop(ptr, "make_planar_iterations", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }

  step_row(
      layout, ptr, "use_split_nonplanar", "split_nonplanar_angle", IFACE_("Split Non-Planar"));
  ui::Layout &more = layout.column(true);
  more.prop(ptr, "use_split_concave", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  more.prop(ptr, "use_recalc_normals", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  modifier_vgroup_ui(layout, ptr, &ob_ptr, "vertex_group", "invert_vertex_group", std::nullopt);

  stats_draw(layout, ptr);

  modifier_error_message_draw(layout, ptr);
}

static void panel_register(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_Cleanup, panel_draw);
}

ModifierTypeInfo modifierType_Cleanup = {
    /*idname*/ "Cleanup",
    /*name*/ N_("Cleanup"),
    /*struct_name*/ "CleanupModifierData",
    /*struct_size*/ sizeof(CleanupModifierData),
    /*srna*/ &RNA_CleanupModifier,
    /*type*/ ModifierTypeType::Nonconstructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode |
        eModifierTypeFlag_AcceptsCVs,
    /*icon*/ ICON_MOD_CLEANUP,

    /*copy_data*/ BKE_modifier_copydata_generic,

    /*deform_verts*/ nullptr,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ modify_mesh,
    /*modify_geometry_set*/ nullptr,

    /*init_data*/ init_data,
    /*required_data_mask*/ nullptr,
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
