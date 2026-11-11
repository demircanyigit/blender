/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Remove points, edges or faces and merge the faces around them, without leaving holes.
 */

#include "BLI_array.hh"
#include "BLI_utildefines.h"

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

#include "UI_interface_layout.hh"
#include "UI_resources.hh"

#include "RNA_access.hh"
#include "RNA_prototypes.hh"

#include "GEO_mesh_dissolve.hh"
#include "GEO_randomize.hh"

#include "MOD_modifiertypes.hh"
#include "MOD_ui_common.hh"
#include "MOD_util.hh"

namespace blender {

using geometry::DissolveMode;

static void init_data(ModifierData *md)
{
  DissolveModifierData *data = reinterpret_cast<DissolveModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

static Mesh *modify_mesh_compute(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  const DissolveModifierData &dmd = *reinterpret_cast<DissolveModifierData *>(md);
  if (mesh->verts_num == 0) {
    return mesh;
  }

  geometry::MeshDissolveParams params;
  params.mode = DissolveMode(dmd.mode);
  params.angle = dmd.angle;
  params.remove_inline_points = dmd.flag & MOD_DISSOLVE_REMOVE_INLINE_POINTS;
  params.face_split = dmd.flag & MOD_DISSOLVE_FACE_SPLIT;
  params.tear_boundary = dmd.flag & MOD_DISSOLVE_TEAR_BOUNDARY;
  params.all_boundaries = dmd.flag & MOD_DISSOLVE_ALL_BOUNDARIES;
  params.keep_materials = dmd.flag & MOD_DISSOLVE_KEEP_MATERIALS;
  params.keep_seams = dmd.flag & MOD_DISSOLVE_KEEP_SEAMS;
  params.keep_sharp = dmd.flag & MOD_DISSOLVE_KEEP_SHARP;
  params.keep_uvs = dmd.flag & MOD_DISSOLVE_KEEP_UVS;

  const bke::AttrDomain domain = geometry::dissolve_selection_domain(params.mode);
  Array<bool> selection(mesh->attributes().domain_size(domain), true);

  const MDeformVert *dvert = nullptr;
  int defgrp_index = -1;
  MOD_get_vgroup(ctx->object, mesh, dmd.vertex_group, &dvert, &defgrp_index);
  if (dvert != nullptr) {
    const bool invert = dmd.flag & MOD_DISSOLVE_INVERT_VGROUP;
    Array<bool> in_group(mesh->verts_num);
    for (const int i : in_group.index_range()) {
      in_group[i] = (BKE_defvert_find_weight(&dvert[i], defgrp_index) > 0.0f) != invert;
    }
    switch (domain) {
      case bke::AttrDomain::Point:
        selection = std::move(in_group);
        break;
      case bke::AttrDomain::Edge: {
        const Span<int2> edges = mesh->edges();
        for (const int i : edges.index_range()) {
          selection[i] = in_group[edges[i][0]] && in_group[edges[i][1]];
        }
        break;
      }
      default: {
        const OffsetIndices<int> faces = mesh->faces();
        const Span<int> corner_verts = mesh->corner_verts();
        for (const int face : faces.index_range()) {
          bool all_in = true;
          for (const int vert : corner_verts.slice(faces[face])) {
            if (!in_group[vert]) {
              all_in = false;
              break;
            }
          }
          selection[face] = all_in;
        }
        break;
      }
    }
  }

  Mesh *result = geometry::mesh_dissolve(*mesh, selection, params);
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
  const DissolveMode mode = DissolveMode(RNA_enum_get(ptr, "mode"));

  layout.prop(ptr, "mode", ui::ITEM_R_EXPAND, std::nullopt, ICON_NONE);
  layout.use_property_split_set(true);

  if (mode == DissolveMode::Limited) {
    layout.prop(ptr, "angle", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }

  ui::Layout &options = layout.column(true, IFACE_("Options"));
  if (mode != DissolveMode::Vertices) {
    options.prop(ptr, "use_remove_inline_points", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  if (ELEM(mode, DissolveMode::Vertices, DissolveMode::Edges)) {
    options.prop(ptr, "use_face_split", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  if (mode == DissolveMode::Vertices) {
    options.prop(ptr, "use_tear_boundary", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  if (mode == DissolveMode::Limited) {
    options.prop(ptr, "use_all_boundaries", UI_ITEM_NONE, std::nullopt, ICON_NONE);

    ui::Layout &keep = layout.column(true, IFACE_("Keep"));
    keep.prop(ptr, "use_keep_materials", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    keep.prop(ptr, "use_keep_seams", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    keep.prop(ptr, "use_keep_sharp", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    keep.prop(ptr, "use_keep_uvs", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }

  modifier_vgroup_ui(layout, ptr, &ob_ptr, "vertex_group", "invert_vertex_group", std::nullopt);

  modifier_error_message_draw(layout, ptr);
}

static void panel_register(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_Dissolve, panel_draw);
}

/* Taken from the cache when nothing it depends on changed, see #MOD_cached_modify_mesh. */
static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  return MOD_cached_modify_mesh(md, ctx, mesh, modify_mesh_compute);
}

ModifierTypeInfo modifierType_Dissolve = {
    /*idname*/ "Dissolve",
    /*name*/ N_("Dissolve"),
    /*struct_name*/ "DissolveModifierData",
    /*struct_size*/ sizeof(DissolveModifierData),
    /*srna*/ &RNA_DissolveModifier,
    /*type*/ ModifierTypeType::Nonconstructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode |
        eModifierTypeFlag_AcceptsCVs,
    /*icon*/ ICON_MOD_DISSOLVE,

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
