/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Inset. The work is done by shared code in the geometry module, which the matching nodes use too.
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
#include "GEO_randomize.hh"

namespace blender {

static void init_data_InsetModifierData(ModifierData *md)
{
  InsetModifierData *data = reinterpret_cast<InsetModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

/** All faces, or the faces whose points are all in the vertex group. */
static IndexMask faces_in_vgroup(const Object *ob,
                                 const Mesh &mesh,
                                 const char *name,
                                 const bool invert,
                                 IndexMaskMemory &memory)
{
  const MDeformVert *dvert = nullptr;
  int defgrp_index = -1;
  MOD_get_vgroup(ob, &mesh, name, &dvert, &defgrp_index);
  if (dvert == nullptr || defgrp_index == -1) {
    return IndexMask(mesh.faces_num);
  }
  const OffsetIndices faces = mesh.faces();
  const Span<int> corner_verts = mesh.corner_verts();
  return IndexMask::from_predicate(
      faces.index_range(), GrainSize(4096), memory, [&](const int face) {
        for (const int vert : corner_verts.slice(faces[face])) {
          const bool in_group = BKE_defvert_find_weight(&dvert[vert], defgrp_index) > 0.0f;
          if (in_group == invert) {
            return false;
          }
        }
        return true;
      });
}

static void required_data_mask(ModifierData *md, CustomData_MeshMasks *r_cddata_masks)
{
  const InsetModifierData &data = *reinterpret_cast<InsetModifierData *>(md);
  if (data.vertex_group[0] != '\0') {
    r_cddata_masks->vmask |= CD_MASK_MDEFORMVERT;
  }
}

static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  const InsetModifierData &imd = *reinterpret_cast<InsetModifierData *>(md);
  geometry::InsetParams params;
  params.individual = imd.flag & MOD_INSET_INDIVIDUAL;
  params.thickness = imd.thickness;
  params.depth = imd.depth;
  params.use_even_offset = imd.flag & MOD_INSET_EVEN;
  params.use_relative_offset = imd.flag & MOD_INSET_RELATIVE;
  params.use_boundary = imd.flag & MOD_INSET_BOUNDARY;
  params.use_edge_rail = imd.flag & MOD_INSET_EDGE_RAIL;
  params.use_outset = imd.flag & MOD_INSET_OUTSET;
  params.side_material_offset = imd.material_offset;
  geometry::TopologyOutputs outputs;
  outputs.top = imd.top_name;
  outputs.side = imd.side_name;
  IndexMaskMemory memory;
  const IndexMask selection = faces_in_vgroup(
      ctx->object, *mesh, imd.vertex_group, imd.flag & MOD_INSET_INVERT_VGROUP, memory);
  Mesh *result = geometry::mesh_inset(*mesh, selection, params, outputs);
  if (result == nullptr) {
    return mesh;
  }
  geometry::debug_randomize_mesh_order(result);
  return result;
}

static void panel_draw_Inset(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);
  UNUSED_VARS(ob_ptr);

  layout.use_property_split_set(true);
  layout.prop(ptr, "thickness", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "depth", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "use_individual", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "use_even_offset", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "use_relative_offset", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  if (!RNA_boolean_get(ptr, "use_individual")) {
    layout.prop(ptr, "use_boundary", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  if (!RNA_boolean_get(ptr, "use_individual")) {
    layout.prop(ptr, "use_edge_rail", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  if (!RNA_boolean_get(ptr, "use_individual")) {
    layout.prop(ptr, "use_outset", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  layout.prop(ptr, "material_offset", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  {
    ui::Layout &col = layout.column(true, IFACE_("Output"));
    col.prop(ptr, "top_name", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    col.prop(ptr, "side_name", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  modifier_vgroup_ui(layout, ptr, &ob_ptr, "vertex_group", "invert_vertex_group", std::nullopt);
  modifier_error_message_draw(layout, ptr);
}

static void panel_register_Inset(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_Inset, panel_draw_Inset);
}

ModifierTypeInfo modifierType_Inset = {
    /*idname*/ "Inset",
    /*name*/ N_("Inset"),
    /*struct_name*/ "InsetModifierData",
    /*struct_size*/ sizeof(InsetModifierData),
    /*srna*/ &RNA_InsetModifier,
    /*type*/ ModifierTypeType::Constructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode | eModifierTypeFlag_EnableInEditmode,
    /*icon*/ ICON_MOD_INSET,

    /*copy_data*/ BKE_modifier_copydata_generic,

    /*deform_verts*/ nullptr,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ modify_mesh,
    /*modify_geometry_set*/ nullptr,

    /*init_data*/ init_data_InsetModifierData,
    /*required_data_mask*/ required_data_mask,
    /*free_data*/ nullptr,
    /*is_disabled*/ nullptr,
    /*update_depsgraph*/ nullptr,
    /*depends_on_time*/ nullptr,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ nullptr,
    /*foreach_tex_link*/ nullptr,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ panel_register_Inset,
    /*blend_write*/ nullptr,
    /*blend_read*/ nullptr,
    /*foreach_cache*/ nullptr,
    /*foreach_working_space_color*/ nullptr,
};

}  // namespace blender
