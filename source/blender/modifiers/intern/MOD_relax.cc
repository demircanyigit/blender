/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Relax. Evens out the distances between the points by sliding them along the surface. The work
 * is done by shared code in the geometry module, which the Relax node uses too.
 */

#include <algorithm>

#include "BLI_array.hh"
#include "BLI_index_mask.hh"
#include "BLI_virtual_array.hh"

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

#include "UI_interface_layout.hh"
#include "UI_resources.hh"

#include "RNA_access.hh"
#include "RNA_prototypes.hh"

#include "MOD_modifiertypes.hh"
#include "MOD_ui_common.hh"
#include "MOD_util.hh"

#include "GEO_mesh_smooth.hh"

namespace blender {

static void init_data_RelaxModifierData(ModifierData *md)
{
  RelaxModifierData *data = reinterpret_cast<RelaxModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

/** Weights of a vertex group, or an empty array when there is none. */
static Array<float> vgroup_weights(const Object *ob,
                                   const Mesh *mesh,
                                   const char *name,
                                   const bool invert,
                                   const int verts_num)
{
  if (mesh == nullptr || name[0] == '\0') {
    return {};
  }
  const MDeformVert *dvert = nullptr;
  int defgrp_index = -1;
  MOD_get_vgroup(ob, mesh, name, &dvert, &defgrp_index);
  if (dvert == nullptr || defgrp_index == -1) {
    return {};
  }
  Array<float> weights(verts_num);
  for (const int i : IndexRange(verts_num)) {
    const float weight = BKE_defvert_find_weight(&dvert[i], defgrp_index);
    weights[i] = invert ? 1.0f - weight : weight;
  }
  return weights;
}

static void required_data_mask(ModifierData *md, CustomData_MeshMasks *r_cddata_masks)
{
  const RelaxModifierData &data = *reinterpret_cast<RelaxModifierData *>(md);
  if (data.vertex_group[0] != '\0') {
    r_cddata_masks->vmask |= CD_MASK_MDEFORMVERT;
  }
}

static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  const RelaxModifierData &rmd = *reinterpret_cast<RelaxModifierData *>(md);
  if (rmd.iterations <= 0 || rmd.factor <= 0.0f || mesh->verts_num == 0) {
    return mesh;
  }

  /* The vertex group scales the factor, and points outside of it don't move. */
  Array<float> factors = vgroup_weights(
      ctx->object, mesh, rmd.vertex_group, rmd.flag & MOD_RELAX_INVERT_VGROUP, mesh->verts_num);
  IndexMaskMemory memory;
  IndexMask selection = IndexMask(mesh->verts_num);
  VArray<float> factor_varray;
  if (factors.is_empty()) {
    factor_varray = VArray<float>::from_single(rmd.factor, mesh->verts_num);
  }
  else {
    for (float &factor : factors) {
      factor = std::clamp(factor * rmd.factor, 0.0f, 1.0f);
    }
    selection = IndexMask::from_predicate(
        IndexRange(mesh->verts_num), GrainSize(4096), memory, [&](const int i) {
          return factors[i] > 0.0f;
        });
    factor_varray = VArray<float>::from_span(factors);
  }

  /* Sharp edges can hold their points in place, like the hard edges of the node. */
  VArray<bool> hard_edges;
  if (rmd.flag & MOD_RELAX_KEEP_SHARP) {
    hard_edges = *mesh->attributes().lookup_or_default<bool>(
        "sharp_edge", bke::AttrDomain::Edge, false);
  }
  else {
    hard_edges = VArray<bool>::from_single(false, mesh->edges_num);
  }

  geometry::MeshSmoothParams params;
  params.iterations = std::clamp(rmd.iterations, 0, 10000);
  params.preserve_boundary = rmd.flag & MOD_RELAX_PRESERVE_BOUNDARY;
  geometry::mesh_relax(*mesh,
                       selection,
                       factor_varray,
                       hard_edges,
                       rmd.flag & MOD_RELAX_KEEP_ON_SURFACE,
                       params);
  mesh->tag_positions_changed();
  return mesh;
}

static void panel_draw_Relax(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);

  layout.use_property_split_set(true);
  layout.prop(ptr, "factor", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "iterations", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  ui::Layout &col = layout.column(true);
  col.prop(ptr, "use_keep_on_surface", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  col.prop(ptr, "use_preserve_boundary", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  col.prop(ptr, "use_keep_sharp", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  modifier_vgroup_ui(layout, ptr, &ob_ptr, "vertex_group", "invert_vertex_group", std::nullopt);
  modifier_error_message_draw(layout, ptr);
}

static void panel_register_Relax(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_Relax, panel_draw_Relax);
}

ModifierTypeInfo modifierType_Relax = {
    /*idname*/ "Relax",
    /*name*/ N_("Relax"),
    /*struct_name*/ "RelaxModifierData",
    /*struct_size*/ sizeof(RelaxModifierData),
    /*srna*/ &RNA_RelaxModifier,
    /*type*/ ModifierTypeType::Nonconstructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode |
        eModifierTypeFlag_EnableInEditmode,
    /*icon*/ ICON_MOD_RELAX,

    /*copy_data*/ BKE_modifier_copydata_generic,

    /*deform_verts*/ nullptr,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ modify_mesh,
    /*modify_geometry_set*/ nullptr,

    /*init_data*/ init_data_RelaxModifierData,
    /*required_data_mask*/ required_data_mask,
    /*free_data*/ nullptr,
    /*is_disabled*/ nullptr,
    /*update_depsgraph*/ nullptr,
    /*depends_on_time*/ nullptr,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ nullptr,
    /*foreach_tex_link*/ nullptr,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ panel_register_Relax,
    /*blend_write*/ nullptr,
    /*blend_read*/ nullptr,
    /*foreach_cache*/ nullptr,
    /*foreach_working_space_color*/ nullptr,
};

}  // namespace blender
