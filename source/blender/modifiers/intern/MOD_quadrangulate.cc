/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Quadrangulate: join pairs of triangles into quads, the same as Triangles to Quads in edit mode.
 * The work is done by shared code in the geometry module, which the Quadrangulate node uses too.
 */

#include "BLI_index_mask.hh"
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

#include "MOD_modifiertypes.hh"
#include "MOD_ui_common.hh"
#include "MOD_util.hh"

#include "GEO_mesh_quadrangulate.hh"
#include "GEO_randomize.hh"

namespace blender {

static void init_data(ModifierData *md)
{
  QuadrangulateModifierData *qmd = reinterpret_cast<QuadrangulateModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(qmd, modifier);
}

static void required_data_mask(ModifierData *md, CustomData_MeshMasks *r_cddata_masks)
{
  const QuadrangulateModifierData &qmd = *reinterpret_cast<QuadrangulateModifierData *>(md);
  if (qmd.vertex_group[0] != '\0') {
    r_cddata_masks->vmask |= CD_MASK_MDEFORMVERT;
  }
}

static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  const QuadrangulateModifierData &qmd = *reinterpret_cast<QuadrangulateModifierData *>(md);
  geometry::QuadrangulateParams params;
  params.face_threshold = qmd.face_threshold;
  params.shape_threshold = qmd.shape_threshold;
  params.topology_influence = qmd.topology_influence;
  params.compare_uvs = qmd.flag & MOD_QUADRANGULATE_COMPARE_UVS;
  params.compare_vcols = qmd.flag & MOD_QUADRANGULATE_COMPARE_VCOLS;
  params.compare_seam = qmd.flag & MOD_QUADRANGULATE_COMPARE_SEAM;
  params.compare_sharp = qmd.flag & MOD_QUADRANGULATE_COMPARE_SHARP;
  params.compare_materials = qmd.flag & MOD_QUADRANGULATE_COMPARE_MATERIALS;

  /* Faces to work on: all, or the faces whose vertices are all in the vertex group. */
  IndexMaskMemory memory;
  IndexMask selection = IndexMask(mesh->faces_num);
  const MDeformVert *dvert = nullptr;
  int defgrp_index = -1;
  MOD_get_vgroup(ctx->object, mesh, qmd.vertex_group, &dvert, &defgrp_index);
  if (dvert != nullptr && defgrp_index != -1) {
    const bool invert = qmd.flag & MOD_QUADRANGULATE_INVERT_VGROUP;
    const OffsetIndices faces = mesh->faces();
    const Span<int> corner_verts = mesh->corner_verts();
    selection = IndexMask::from_predicate(
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

  Mesh *result = geometry::mesh_quadrangulate(*mesh, selection, params);
  if (result == nullptr) {
    return mesh;
  }
  geometry::debug_randomize_mesh_order(result);
  return result;
}

static void panel_draw(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);

  layout.use_property_split_set(true);
  layout.prop(ptr, "face_threshold", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "shape_threshold", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "topology_influence", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  ui::Layout &col = layout.column(true, IFACE_("Compare"));
  col.prop(ptr, "use_compare_uvs", UI_ITEM_NONE, IFACE_("UVs"), ICON_NONE);
  col.prop(ptr, "use_compare_vcols", UI_ITEM_NONE, IFACE_("Color Attributes"), ICON_NONE);
  col.prop(ptr, "use_compare_seam", UI_ITEM_NONE, IFACE_("Seam"), ICON_NONE);
  col.prop(ptr, "use_compare_sharp", UI_ITEM_NONE, IFACE_("Sharp"), ICON_NONE);
  col.prop(ptr, "use_compare_materials", UI_ITEM_NONE, IFACE_("Materials"), ICON_NONE);

  modifier_vgroup_ui(layout, ptr, &ob_ptr, "vertex_group", "invert_vertex_group", std::nullopt);
  modifier_error_message_draw(layout, ptr);
}

static void panel_register(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_Quadrangulate, panel_draw);
}

ModifierTypeInfo modifierType_Quadrangulate = {
    /*idname*/ "Quadrangulate",
    /*name*/ N_("Quadrangulate"),
    /*struct_name*/ "QuadrangulateModifierData",
    /*struct_size*/ sizeof(QuadrangulateModifierData),
    /*srna*/ &RNA_QuadrangulateModifier,
    /*type*/ ModifierTypeType::Constructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode |
        eModifierTypeFlag_EnableInEditmode,
    /*icon*/ ICON_MOD_QUADRANGULATE,

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
