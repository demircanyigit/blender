/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Rebuilds the mesh out of even, near-equilateral triangles. The same operation as the
 * Isotropic Remesh node, see #geometry::isotropic_remesh.
 */

#include "BLT_translation.hh"

#include "DNA_mesh_types.h"
#include "DNA_modifier_types.h"
#include "DNA_screen_types.h"

#include "BKE_attribute.hh"
#include "BKE_mesh.hh"
#include "BKE_modifier.hh"

#include "UI_interface_layout.hh"
#include "UI_resources.hh"

#include "RNA_access.hh"
#include "RNA_prototypes.hh"

#include "GEO_isotropic_remesh.hh"
#include "GEO_randomize.hh"

#include "MOD_modifiertypes.hh"
#include "MOD_ui_common.hh"
#include "MOD_util.hh"

namespace blender {

static void init_data(ModifierData *md)
{
  IsotropicRemeshModifierData *imd = reinterpret_cast<IsotropicRemeshModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(imd, modifier);
}

static Mesh *modify_mesh_compute(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  const IsotropicRemeshModifierData &imd = *reinterpret_cast<IsotropicRemeshModifierData *>(md);
  if (mesh->faces_num == 0) {
    return mesh;
  }

  geometry::IsotropicRemeshParams params;
  params.mode = geometry::IsotropicRemeshMode(imd.mode);
  params.target_edge_length = std::max(imd.target_edge_length, 0.0f);
  params.min_edge_length = imd.min_edge_length;
  params.max_edge_length = imd.max_edge_length;
  params.adaptive_tolerance = imd.tolerance;
  params.iterations = std::max(imd.iterations, 0);
  params.preserve_boundary = imd.flag & MOD_ISOTROPIC_REMESH_PRESERVE_BOUNDARY;
  params.preserve_sharp_edges = imd.flag & MOD_ISOTROPIC_REMESH_PRESERVE_SHARP;
  params.sharp_angle = imd.sharp_angle;
  params.transfer_attributes = true;
  if (imd.flag & MOD_ISOTROPIC_REMESH_KEEP_MARKED_SHARP) {
    params.hard_edges = *mesh->attributes().lookup_or_default<bool>(
        "sharp_edge", bke::AttrDomain::Edge, false);
  }

  int detached_tris = 0;
  Mesh *result = geometry::isotropic_remesh(
      *mesh, params, bke::AttributeFilter::default_filter(), nullptr, &detached_tris);
  if (result == nullptr) {
    BKE_modifier_set_warning(ctx->object, md, "Remeshing produced no geometry");
    return mesh;
  }
  if (detached_tris > 0) {
    BKE_modifier_set_warning(
        ctx->object, md, "%d non-manifold triangles were detached, leaving seams", detached_tris);
  }
  BKE_mesh_copy_parameters_for_eval(result, mesh);
  bke::mesh_smooth_set(*result, imd.flag & MOD_ISOTROPIC_REMESH_SMOOTH_SHADING);
  geometry::debug_randomize_mesh_order(result);
  return result;
}

static void panel_draw(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;

  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);

  layout.prop(ptr, "mode", ui::ITEM_R_EXPAND, std::nullopt, ICON_NONE);
  layout.use_property_split_set(true);

  ui::Layout &col = layout.column(false);
  if (RNA_enum_get(ptr, "mode") == int(geometry::IsotropicRemeshMode::Uniform)) {
    col.prop(ptr, "target_edge_length", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  else {
    col.prop(ptr, "min_edge_length", UI_ITEM_NONE, IFACE_("Edge Length Min"), ICON_NONE);
    col.prop(ptr, "max_edge_length", UI_ITEM_NONE, IFACE_("Max"), ICON_NONE);
    col.prop(ptr, "tolerance", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  col.prop(ptr, "iterations", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  ui::Layout &keep = layout.column(true, IFACE_("Preserve"));
  keep.prop(ptr, "use_preserve_boundary", UI_ITEM_NONE, IFACE_("Boundary"), ICON_NONE);
  keep.prop(ptr, "use_keep_marked_sharp", UI_ITEM_NONE, IFACE_("Marked Sharp"), ICON_NONE);
  keep.prop(ptr, "use_preserve_sharp", UI_ITEM_NONE, IFACE_("Sharp Edges"), ICON_NONE);
  ui::Layout &angle = layout.row(false);
  angle.active_set(RNA_boolean_get(ptr, "use_preserve_sharp"));
  angle.prop(ptr, "sharp_angle", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  layout.prop(ptr, "use_smooth_shade", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  modifier_error_message_draw(layout, ptr);
}

static void panel_register(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_IsotropicRemesh, panel_draw);
}

/* Taken from the cache when nothing it depends on changed, see #MOD_cached_modify_mesh. */
static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  return MOD_cached_modify_mesh(md, ctx, mesh, modify_mesh_compute);
}

ModifierTypeInfo modifierType_IsotropicRemesh = {
    /*idname*/ "IsotropicRemesh",
    /*name*/ N_("Isotropic Remesh"),
    /*struct_name*/ "IsotropicRemeshModifierData",
    /*struct_size*/ sizeof(IsotropicRemeshModifierData),
    /*srna*/ &RNA_IsotropicRemeshModifier,
    /*type*/ ModifierTypeType::Nonconstructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode,
    /*icon*/ ICON_MOD_ISOTROPIC_REMESH,

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
