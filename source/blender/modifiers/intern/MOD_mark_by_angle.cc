/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Mark by Angle. The work is done by shared code in the geometry module, which the matching nodes use too.
 */

#include <algorithm>

#include "BLI_array.hh"
#include "BLI_index_mask.hh"
#include "BLI_math_vector.hh"
#include "BLI_string.h"
#include "BLT_translation.hh"

#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_types.h"
#include "DNA_screen_types.h"

#include "BKE_attribute.hh"
#include "BKE_deform.hh"
#include "BKE_lib_query.hh"
#include "BKE_mesh.hh"
#include "BKE_modifier.hh"

#include "DEG_depsgraph_build.hh"
#include "DEG_depsgraph_query.hh"

#include "UI_interface_layout.hh"
#include "UI_resources.hh"

#include "RNA_access.hh"
#include "RNA_prototypes.hh"

#include "MOD_modifiertypes.hh"
#include "MOD_ui_common.hh"
#include "MOD_util.hh"
#include "GEO_mesh_topology_ops.hh"

namespace blender {

static void init_data_MarkByAngleModifierData(ModifierData *md)
{
  MarkByAngleModifierData *data = reinterpret_cast<MarkByAngleModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

static void required_data_mask(ModifierData *md, CustomData_MeshMasks *r_cddata_masks)
{
  const MarkByAngleModifierData &data = *reinterpret_cast<MarkByAngleModifierData *>(md);
  if (data.vertex_group[0] != '\0') {
    r_cddata_masks->vmask |= CD_MASK_MDEFORMVERT;
  }
}

static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  const MarkByAngleModifierData &mmd = *reinterpret_cast<MarkByAngleModifierData *>(md);
  geometry::MarkByAngleParams params;
  params.angle = mmd.angle;
  params.side = geometry::MarkAngleSide(mmd.side);
  params.use_boundary = mmd.flag & MOD_MARK_BY_ANGLE_BOUNDARY;
  params.clear_others = mmd.flag & MOD_MARK_BY_ANGLE_CLEAR;
  params.mark_sharp = mmd.flag & MOD_MARK_BY_ANGLE_SHARP;
  params.mark_seam = mmd.flag & MOD_MARK_BY_ANGLE_SEAM;
  params.mark_crease = mmd.flag & MOD_MARK_BY_ANGLE_CREASE;
  params.mark_bevel_weight = mmd.flag & MOD_MARK_BY_ANGLE_BEVEL_WEIGHT;
  params.value = mmd.value;
  params.flat_faces = mmd.flag & MOD_MARK_BY_ANGLE_FLAT_FACES;

  /* Edges to change: all, or those whose points are both in the vertex group. */
  IndexMaskMemory memory;
  IndexMask edge_mask(mesh->edges_num);
  const MDeformVert *dvert = nullptr;
  int defgrp_index = -1;
  MOD_get_vgroup(ctx->object, mesh, mmd.vertex_group, &dvert, &defgrp_index);
  if (dvert != nullptr && defgrp_index != -1) {
    const bool invert = mmd.flag & MOD_MARK_BY_ANGLE_INVERT_VGROUP;
    const Span<int2> edges = mesh->edges();
    edge_mask = IndexMask::from_predicate(
        edges.index_range(), GrainSize(4096), memory, [&](const int edge) {
          for (const int vert : {edges[edge][0], edges[edge][1]}) {
            const bool in_group = BKE_defvert_find_weight(&dvert[vert], defgrp_index) > 0.0f;
            if (in_group == invert) {
              return false;
            }
          }
          return true;
        });
  }
  Mesh *result = geometry::mesh_mark_by_angle(*mesh, edge_mask, params);
  return result ? result : mesh;
}

static void panel_draw_MarkByAngle(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);
  UNUSED_VARS(ob_ptr);

  layout.prop(ptr, "side", ui::ITEM_R_EXPAND, std::nullopt, ICON_NONE);
  layout.use_property_split_set(true);
  layout.prop(ptr, "angle", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  {
    ui::Layout &col = layout.column(true, IFACE_("Mark"));
    col.prop(ptr, "use_sharp", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    col.prop(ptr, "use_seam", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    col.prop(ptr, "use_crease", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    col.prop(ptr, "use_bevel_weight", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  if (RNA_boolean_get(ptr, "use_crease") || RNA_boolean_get(ptr, "use_bevel_weight")) {
    layout.prop(ptr, "value", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  layout.prop(ptr, "use_boundary", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "use_clear", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "use_flat_faces", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  modifier_vgroup_ui(layout, ptr, &ob_ptr, "vertex_group", "invert_vertex_group", std::nullopt);
  modifier_error_message_draw(layout, ptr);
}

static void panel_register_MarkByAngle(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_MarkByAngle, panel_draw_MarkByAngle);
}

ModifierTypeInfo modifierType_MarkByAngle = {
    /*idname*/ "MarkByAngle",
    /*name*/ N_("Mark by Angle"),
    /*struct_name*/ "MarkByAngleModifierData",
    /*struct_size*/ sizeof(MarkByAngleModifierData),
    /*srna*/ &RNA_MarkByAngleModifier,
    /*type*/ ModifierTypeType::Nonconstructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode | eModifierTypeFlag_EnableInEditmode,
    /*icon*/ ICON_MOD_MARK_BY_ANGLE,

    /*copy_data*/ BKE_modifier_copydata_generic,

    /*deform_verts*/ nullptr,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ modify_mesh,
    /*modify_geometry_set*/ nullptr,

    /*init_data*/ init_data_MarkByAngleModifierData,
    /*required_data_mask*/ required_data_mask,
    /*free_data*/ nullptr,
    /*is_disabled*/ nullptr,
    /*update_depsgraph*/ nullptr,
    /*depends_on_time*/ nullptr,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ nullptr,
    /*foreach_tex_link*/ nullptr,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ panel_register_MarkByAngle,
    /*blend_write*/ nullptr,
    /*blend_read*/ nullptr,
    /*foreach_cache*/ nullptr,
    /*foreach_working_space_color*/ nullptr,
};

}  // namespace blender
