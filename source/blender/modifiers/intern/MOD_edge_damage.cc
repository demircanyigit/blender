/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Wear and chip the edges and corners of closed meshes.
 */

#include <string>

#include "BLI_array.hh"
#include "BLI_utildefines.h"

#include "BLT_translation.hh"

#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_types.h"
#include "DNA_screen_types.h"

#include "BKE_deform.hh"
#include "BKE_modifier.hh"

#include "UI_interface_layout.hh"
#include "UI_resources.hh"

#include "RNA_access.hh"
#include "RNA_prototypes.hh"

#include "GEO_edge_damage.hh"

#include "MOD_modifiertypes.hh"
#include "MOD_ui_common.hh"
#include "MOD_util.hh"

namespace blender {

using geometry::EdgeDamageRemesh;

static void init_data(ModifierData *md)
{
  auto *data = reinterpret_cast<EdgeDamageModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

static void required_data_mask(ModifierData *md, CustomData_MeshMasks *r_cddata_masks)
{
  const auto &data = *reinterpret_cast<EdgeDamageModifierData *>(md);
  if (data.vertex_group[0] != '\0') {
    r_cddata_masks->vmask |= CD_MASK_MDEFORMVERT;
  }
}

static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  const auto &data = *reinterpret_cast<EdgeDamageModifierData *>(md);
  if (mesh->faces_num == 0) {
    return mesh;
  }

  geometry::EdgeDamageParams params;
  params.remesh = EdgeDamageRemesh(data.remesh);
  params.resolution = data.resolution;
  params.remesh_iterations = data.remesh_iterations;
  params.smooth_iterations = data.smooth_iterations;
  params.smooth_factor = data.smooth_factor;
  params.push = data.push;
  params.noise_type = geometry::EdgeDamageNoise(data.noise_type);
  params.noise_amount = data.noise_amount;
  params.noise_scale = data.noise_scale;
  params.noise_detail = data.noise_detail;
  params.noise_roughness = data.noise_roughness;
  params.solver = geometry::EdgeDamageSolver(data.solver);
  params.per_island = data.flag & MOD_EDGE_DAMAGE_PER_ISLAND;
  params.island_variation = data.island_variation;
  params.seed = data.seed;
  params.material_index = data.material_index >= 0 && data.material_index < ctx->object->totcol ?
                              data.material_index :
                              -1;
  params.smooth_shading = data.flag & MOD_EDGE_DAMAGE_SMOOTH_SHADING;
  params.visualize = data.flag & MOD_EDGE_DAMAGE_VISUALIZE;
  params.damage_attribute = data.damage_attribute;

  Array<float> weights;
  const MDeformVert *dvert = nullptr;
  int defgrp_index = -1;
  MOD_get_vgroup(ctx->object, mesh, data.vertex_group, &dvert, &defgrp_index);
  if (dvert != nullptr) {
    const bool invert = data.flag & MOD_EDGE_DAMAGE_INVERT_VGROUP;
    weights.reinitialize(mesh->verts_num);
    bool any = false;
    for (const int i : weights.index_range()) {
      float weight = BKE_defvert_find_weight(&dvert[i], defgrp_index);
      if (invert) {
        weight = 1.0f - weight;
      }
      weights[i] = weight;
      any |= weight > 0.0f;
    }
    if (!any) {
      return mesh;
    }
    params.weights = weights;
  }

  std::string message;
  Mesh *result = geometry::edge_damage(*mesh, params, message);
  if (!message.empty()) {
    BKE_modifier_set_warning(ctx->object, md, "%s", message.c_str());
  }
  return result ? result : mesh;
}

static void panel_draw(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);
  layout.use_property_split_set(true);

  ui::Layout &damage = layout.column(true);
  damage.prop(ptr, "smooth_iterations", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  damage.prop(ptr, "smooth_factor", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "push", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  layout.prop(ptr, "solver", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  ui::Layout &islands = layout.column(true, IFACE_("Per Island"));
  islands.prop(ptr, "use_per_island", UI_ITEM_NONE, "", ICON_NONE);
  ui::Layout &variation = islands.column(true);
  variation.active_set(RNA_boolean_get(ptr, "use_per_island"));
  variation.prop(ptr, "island_variation", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "seed", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  modifier_vgroup_ui(layout, ptr, &ob_ptr, "vertex_group", "invert_vertex_group", std::nullopt);

  modifier_error_message_draw(layout, ptr);
}

static void remesh_panel_draw(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, nullptr);
  layout.use_property_split_set(true);

  layout.prop(ptr, "remesh", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  const EdgeDamageRemesh remesh = EdgeDamageRemesh(RNA_enum_get(ptr, "remesh"));
  if (remesh != EdgeDamageRemesh::None) {
    layout.prop(ptr, "resolution", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  if (remesh == EdgeDamageRemesh::Isotropic) {
    layout.prop(ptr, "remesh_iterations", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
}

static void noise_panel_draw(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, nullptr);
  layout.use_property_split_set(true);

  layout.prop(ptr, "noise_type", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  ui::Layout &col = layout.column(true);
  col.prop(ptr, "noise_amount", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  col.prop(ptr, "noise_scale", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  ui::Layout &detail = layout.column(true);
  detail.prop(ptr, "noise_detail", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  detail.prop(ptr, "noise_roughness", UI_ITEM_NONE, std::nullopt, ICON_NONE);
}

static void output_panel_draw(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, nullptr);
  layout.use_property_split_set(true);

  layout.prop(ptr, "material_index", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "use_smooth_shade", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "damage_attribute", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "visualize_damage", UI_ITEM_NONE, std::nullopt, ICON_NONE);
}

static void panel_register(ARegionType *region_type)
{
  PanelType *parent = modifier_panel_register(region_type, eModifierType_EdgeDamage, panel_draw);
  modifier_subpanel_register(region_type, "remesh", "Remesh", nullptr, remesh_panel_draw, parent);
  modifier_subpanel_register(region_type, "noise", "Noise", nullptr, noise_panel_draw, parent);
  modifier_subpanel_register(region_type, "output", "Output", nullptr, output_panel_draw, parent);
}

ModifierTypeInfo modifierType_EdgeDamage = {
    /*idname*/ "EdgeDamage",
    /*name*/ N_("Edge Damage"),
    /*struct_name*/ "EdgeDamageModifierData",
    /*struct_size*/ sizeof(EdgeDamageModifierData),
    /*srna*/ &RNA_EdgeDamageModifier,
    /*type*/ ModifierTypeType::Constructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode,
    /*icon*/ ICON_MOD_EDGE_DAMAGE,

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
