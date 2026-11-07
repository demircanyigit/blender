/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Let every mesh island fall as a rigid piece and keep the pose where the pieces came to rest.
 */

#include "BLI_array.hh"
#include "BLI_math_matrix.hh"

#include "BLT_translation.hh"

#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_types.h"
#include "DNA_screen_types.h"

#include "BKE_deform.hh"
#include "BKE_lib_query.hh"
#include "BKE_mesh.hh"
#include "BKE_mesh_wrapper.hh"
#include "BKE_modifier.hh"

#include "DEG_depsgraph_build.hh"
#include "DEG_depsgraph_query.hh"

#include "UI_interface_layout.hh"
#include "UI_resources.hh"

#include "RNA_access.hh"
#include "RNA_prototypes.hh"

#include "GEO_mesh_collapse.hh"

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
}

static void update_depsgraph(ModifierData *md, const ModifierUpdateDepsgraphContext *ctx)
{
  const CollapseModifierData *cmd = reinterpret_cast<CollapseModifierData *>(md);
  if (cmd->target != nullptr) {
    DEG_add_object_relation(ctx->node, cmd->target, DEG_OB_COMP_TRANSFORM, "Collapse Modifier");
    DEG_add_object_relation(ctx->node, cmd->target, DEG_OB_COMP_GEOMETRY, "Collapse Modifier");
  }
  /* Down is down in the world, so the object's turn and place matter. */
  DEG_add_depends_on_transform_relation(ctx->node, "Collapse Modifier");
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

  if (cmd.target != nullptr && cmd.target != ctx->object && cmd.target->type == OB_MESH) {
    Object *target_eval = DEG_get_evaluated(ctx->depsgraph, cmd.target);
    Mesh *ground = BKE_modifier_get_evaluated_mesh_from_evaluated_object(target_eval);
    if (ground != nullptr) {
      BKE_mesh_wrapper_ensure_mdata(ground);
      params.ground = ground;
      params.ground_to_space = target_eval->object_to_world();
    }
  }

  Array<bool> keep;
  const MDeformVert *dvert = nullptr;
  int defgrp_index = -1;
  MOD_get_vgroup(ctx->object, mesh, cmd.vertex_group, &dvert, &defgrp_index);
  if (dvert != nullptr) {
    const bool invert = cmd.flag & MOD_COLLAPSE_INVERT_VGROUP;
    keep.reinitialize(mesh->verts_num);
    for (const int i : keep.index_range()) {
      keep[i] = (BKE_defvert_find_weight(&dvert[i], defgrp_index) > 0.0f) != invert;
    }
    params.keep = keep;
  }

  const char *error = nullptr;
  Mesh *result = geometry::mesh_collapse(*mesh, params, &error);
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
  layout.prop(ptr, "target", UI_ITEM_NONE, std::nullopt, ICON_NONE);
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
    /*update_depsgraph*/ update_depsgraph,
    /*depends_on_time*/ nullptr,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ foreach_ID_link,
    /*foreach_tex_link*/ nullptr,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ panel_register,
    /*blend_write*/ nullptr,
    /*blend_read*/ nullptr,
    /*foreach_cache*/ nullptr,
    /*foreach_working_space_color*/ nullptr,
};

}  // namespace blender
