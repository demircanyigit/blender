/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Let the mesh fall as cloth and keep the shape it rests in.
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

#include "GEO_mesh_drape.hh"

#include "MOD_modifiertypes.hh"
#include "MOD_ui_common.hh"
#include "MOD_util.hh"

namespace blender {

static void init_data(ModifierData *md)
{
  DrapeModifierData *data = reinterpret_cast<DrapeModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

static void required_data_mask(ModifierData *md, CustomData_MeshMasks *r_cddata_masks)
{
  const DrapeModifierData *dmd = reinterpret_cast<DrapeModifierData *>(md);
  if (dmd->vertex_group[0] != '\0') {
    r_cddata_masks->vmask |= CD_MASK_MDEFORMVERT;
  }
}

static void foreach_ID_link(ModifierData *md, Object *ob, IDWalkFunc walk, void *user_data)
{
  DrapeModifierData *dmd = reinterpret_cast<DrapeModifierData *>(md);
  walk(user_data, ob, reinterpret_cast<ID **>(&dmd->collider), IDWALK_CB_NOP);
}

static void update_depsgraph(ModifierData *md, const ModifierUpdateDepsgraphContext *ctx)
{
  const DrapeModifierData *dmd = reinterpret_cast<DrapeModifierData *>(md);
  if (dmd->collider != nullptr) {
    DEG_add_object_relation(ctx->node, dmd->collider, DEG_OB_COMP_TRANSFORM, "Drape Modifier");
    DEG_add_object_relation(ctx->node, dmd->collider, DEG_OB_COMP_GEOMETRY, "Drape Modifier");
  }
  /* Down is down in the world, so the object's turn and place matter. */
  DEG_add_depends_on_transform_relation(ctx->node, "Drape Modifier");
}

static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  const DrapeModifierData &dmd = *reinterpret_cast<DrapeModifierData *>(md);
  if (mesh->verts_num == 0) {
    return mesh;
  }

  geometry::MeshDrapeParams params;
  params.time = dmd.time;
  params.gravity = dmd.gravity;
  params.stiffness = dmd.stiffness;
  params.bending = dmd.bending;
  params.shrink = dmd.shrink;
  params.thickness = dmd.thickness;
  params.friction = dmd.friction;
  params.pressure = dmd.pressure;
  params.substeps = dmd.substeps;
  params.floor = dmd.flag & MOD_DRAPE_FLOOR;
  params.self_collision = dmd.flag & MOD_DRAPE_SELF_COLLISION;
  params.mesh_to_space = ctx->object->object_to_world();

  if (dmd.collider != nullptr && dmd.collider != ctx->object && dmd.collider->type == OB_MESH) {
    Object *collider_eval = DEG_get_evaluated(ctx->depsgraph, dmd.collider);
    Mesh *collider = BKE_modifier_get_evaluated_mesh_from_evaluated_object(collider_eval);
    if (collider != nullptr) {
      BKE_mesh_wrapper_ensure_mdata(collider);
      params.collider = collider;
      params.collider_to_space = collider_eval->object_to_world();
    }
  }

  Array<bool> pin;
  const MDeformVert *dvert = nullptr;
  int defgrp_index = -1;
  MOD_get_vgroup(ctx->object, mesh, dmd.vertex_group, &dvert, &defgrp_index);
  if (dvert != nullptr) {
    const bool invert = dmd.flag & MOD_DRAPE_INVERT_VGROUP;
    pin.reinitialize(mesh->verts_num);
    for (const int i : pin.index_range()) {
      pin[i] = (BKE_defvert_find_weight(&dvert[i], defgrp_index) > 0.0f) != invert;
    }
    params.pin = pin;
  }

  const char *error = nullptr;
  Mesh *result = geometry::mesh_drape(*mesh, params, &error);
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
  layout.prop(ptr, "collider", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "use_floor", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  modifier_vgroup_ui(layout, ptr, &ob_ptr, "vertex_group", "invert_vertex_group", IFACE_("Pin"));

  ui::Layout &col = layout.column(true);
  col.prop(ptr, "time", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  col.prop(ptr, "gravity", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  ui::Layout &col2 = layout.column(true);
  col2.prop(ptr, "stiffness", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  col2.prop(ptr, "bending", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  col2.prop(ptr, "shrink", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  col2.prop(ptr, "pressure", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  ui::Layout &col3 = layout.column(true);
  col3.prop(ptr, "thickness", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  col3.prop(ptr, "friction", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "use_self_collision", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  layout.prop(ptr, "quality", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  modifier_error_message_draw(layout, ptr);
}

static void panel_register(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_Drape, panel_draw);
}

ModifierTypeInfo modifierType_Drape = {
    /*idname*/ "Drape",
    /*name*/ N_("Drape"),
    /*struct_name*/ "DrapeModifierData",
    /*struct_size*/ sizeof(DrapeModifierData),
    /*srna*/ &RNA_DrapeModifier,
    /*type*/ ModifierTypeType::Constructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode,
    /*icon*/ ICON_MOD_DRAPE,

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
