/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Rebuilds the mesh out of quads with QuadriFlow, like 3ds Max's Quadify Mesh. The same
 * operation as the QuadriFlow Remesh node, see #geometry::quadriflow_remesh.
 */

#include "BLT_translation.hh"

#include "DNA_mesh_types.h"
#include "DNA_modifier_types.h"
#include "DNA_screen_types.h"

#include "BKE_mesh.hh"
#include "BKE_modifier.hh"

#include "UI_interface_layout.hh"
#include "UI_resources.hh"

#include "RNA_access.hh"
#include "RNA_prototypes.hh"

#include "GEO_quadriflow_remesh.hh"

#include "MOD_modifiertypes.hh"
#include "MOD_ui_common.hh"
#include "MOD_util.hh"

namespace blender {

static void init_data(ModifierData *md)
{
  QuadriflowRemeshModifierData *qmd = reinterpret_cast<QuadriflowRemeshModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(qmd, modifier);
}

static Mesh *modify_mesh_compute(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  const QuadriflowRemeshModifierData &qmd = *reinterpret_cast<QuadriflowRemeshModifierData *>(
      md);

  geometry::QuadriflowRemeshParams params;
  params.target = geometry::QuadriflowTarget(qmd.target);
  params.face_count = qmd.face_count;
  params.ratio = qmd.ratio;
  params.edge_length = qmd.edge_length;
  params.seed = qmd.seed;
  params.preserve_sharp = qmd.flag & MOD_QUADRIFLOW_REMESH_PRESERVE_SHARP;
  params.preserve_boundary = qmd.flag & MOD_QUADRIFLOW_REMESH_PRESERVE_BOUNDARY;

  geometry::QuadriflowRemeshResult result_kind;
  Mesh *result = geometry::quadriflow_remesh(*mesh, params, result_kind);
  switch (result_kind) {
    case geometry::QuadriflowRemeshResult::Success:
      break;
    case geometry::QuadriflowRemeshResult::Skipped:
      return mesh;
    case geometry::QuadriflowRemeshResult::Unsupported:
      BKE_modifier_set_error(ctx->object,
                             md,
                             "The mesh must be manifold, with faces pointing the same way and no "
                             "zero-length edges");
      return mesh;
    case geometry::QuadriflowRemeshResult::Failed:
      BKE_modifier_set_error(ctx->object, md, "The mesh could not be remeshed");
      return mesh;
    case geometry::QuadriflowRemeshResult::Disabled:
      BKE_modifier_set_error(ctx->object, md, "Disabled, built without QuadriFlow");
      return mesh;
  }
  bke::mesh_smooth_set(*result, qmd.flag & MOD_QUADRIFLOW_REMESH_SMOOTH_SHADING);
  return result;
}

static void panel_draw(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;

  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);

  layout.prop(ptr, "target", ui::ITEM_R_EXPAND, std::nullopt, ICON_NONE);
  layout.use_property_split_set(true);

  ui::Layout &col = layout.column(false);
  switch (geometry::QuadriflowTarget(RNA_enum_get(ptr, "target"))) {
    case geometry::QuadriflowTarget::FaceCount:
      col.prop(ptr, "face_count", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      break;
    case geometry::QuadriflowTarget::Ratio:
      col.prop(ptr, "ratio", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      break;
    case geometry::QuadriflowTarget::EdgeLength:
      col.prop(ptr, "edge_length", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      break;
  }
  col.prop(ptr, "seed", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  ui::Layout &keep = layout.column(true, IFACE_("Preserve"));
  keep.prop(ptr, "use_preserve_sharp", UI_ITEM_NONE, IFACE_("Sharp"), ICON_NONE);
  keep.prop(ptr, "use_preserve_boundary", UI_ITEM_NONE, IFACE_("Boundary"), ICON_NONE);

  layout.prop(ptr, "use_smooth_shade", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  modifier_error_message_draw(layout, ptr);
}

static void panel_register(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_QuadriflowRemesh, panel_draw);
}

/* Taken from the cache when nothing it depends on changed, see #MOD_cached_modify_mesh. */
static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  return MOD_cached_modify_mesh(md, ctx, mesh, modify_mesh_compute);
}

ModifierTypeInfo modifierType_QuadriflowRemesh = {
    /*idname*/ "QuadriflowRemesh",
    /*name*/ N_("QuadriFlow Remesh"),
    /*struct_name*/ "QuadriflowRemeshModifierData",
    /*struct_size*/ sizeof(QuadriflowRemeshModifierData),
    /*srna*/ &RNA_QuadriflowRemeshModifier,
    /*type*/ ModifierTypeType::Nonconstructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode,
    /*icon*/ ICON_MOD_QUADIFY,

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
