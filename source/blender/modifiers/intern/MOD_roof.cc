/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Build roofs on the outlines of faces, or dress faces modeled as a roof.
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
#include "BKE_mesh.hh"
#include "BKE_modifier.hh"

#include "UI_interface_layout.hh"
#include "UI_resources.hh"

#include "RNA_access.hh"
#include "RNA_prototypes.hh"

#include "GEO_mesh_roof.hh"
#include "GEO_randomize.hh"

#include "MOD_modifiertypes.hh"
#include "MOD_ui_common.hh"
#include "MOD_util.hh"

namespace blender {

using geometry::RoofCovering;
using geometry::RoofMode;
using geometry::RoofShape;

static void init_data(ModifierData *md)
{
  auto *data = reinterpret_cast<RoofModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

static void required_data_mask(ModifierData *md, CustomData_MeshMasks *r_cddata_masks)
{
  const auto &data = *reinterpret_cast<RoofModifierData *>(md);
  if (data.vertex_group[0] != '\0') {
    r_cddata_masks->vmask |= CD_MASK_MDEFORMVERT;
  }
}

static int valid_material(const int material, const Object *object)
{
  return material >= 0 && material < std::max<int>(object->totcol, 1) ? material : -1;
}

static Mesh *modify_mesh_compute(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  const auto &data = *reinterpret_cast<RoofModifierData *>(md);
  if (mesh->faces_num == 0) {
    return mesh;
  }

  geometry::MeshRoofParams params;
  params.mode = RoofMode(data.mode);
  params.shape = RoofShape(data.shape);
  params.pitch = data.pitch;
  params.lower_pitch = data.lower_pitch;
  params.break_ratio = data.break_ratio;
  params.auto_ridge = data.flag & MOD_ROOF_AUTO_RIDGE;
  params.ridge_angle = data.ridge_angle;
  params.parapet_height = data.parapet_height;
  params.parapet_width = data.parapet_width;
  params.eave_overhang = data.eave_overhang;
  params.rake_overhang = data.rake_overhang;
  params.thickness = data.thickness;
  params.fascia_height = data.fascia_height;
  params.fascia_thickness = data.fascia_thickness;
  params.gutters = data.flag & MOD_ROOF_GUTTERS;
  params.gutter_size = data.gutter_size;
  params.covering = RoofCovering(data.covering);
  params.row_spacing = data.row_spacing;
  params.tile_width = data.tile_width;
  params.covering_thickness = data.covering_thickness;
  params.overlap = data.overlap;
  params.covering_gap = data.covering_gap;
  params.ridge_caps = data.flag & MOD_ROOF_RIDGE_CAPS;
  params.cap_width = data.cap_width;
  params.cap_angle = data.cap_angle;
  params.valleys = data.flag & MOD_ROOF_VALLEYS;
  params.valley_width = data.valley_width;
  params.flat_angle = data.flat_angle;
  params.flat_roofing = data.flag & MOD_ROOF_FLAT_ROOFING;
  params.random_height = data.random_height;
  params.tilt = data.tilt;
  params.missing = data.missing;
  params.seed = data.seed;
  params.roof_material = valid_material(data.roof_material, ctx->object);
  params.covering_material = valid_material(data.covering_material, ctx->object);
  params.trim_material = valid_material(data.trim_material, ctx->object);
  params.wall_material = valid_material(data.wall_material, ctx->object);
  params.part_id = "roof_part";
  params.random_id = "roof_random";

  Array<bool> selection;
  const MDeformVert *dvert = nullptr;
  int defgrp_index = -1;
  MOD_get_vgroup(ctx->object, mesh, data.vertex_group, &dvert, &defgrp_index);
  if (dvert != nullptr) {
    const bool invert = data.flag & MOD_ROOF_INVERT_VGROUP;
    const OffsetIndices<int> faces = mesh->faces();
    const Span<int> corner_verts = mesh->corner_verts();
    selection.reinitialize(mesh->faces_num);
    bool any = false;
    for (const int face : faces.index_range()) {
      bool all_in = true;
      for (const int vert : corner_verts.slice(faces[face])) {
        if (!(BKE_defvert_find_weight(&dvert[vert], defgrp_index) > 0.0f)) {
          all_in = false;
          break;
        }
      }
      selection[face] = all_in != invert;
      any |= selection[face];
    }
    if (!any) {
      return mesh;
    }
    params.selection = selection;
  }

  std::string message;
  Mesh *result = geometry::mesh_roof(*mesh, params, message);
  if (!message.empty()) {
    BKE_modifier_set_warning(ctx->object, md, "%s", message.c_str());
  }
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

  layout.prop(ptr, "mode", ui::ITEM_R_EXPAND, std::nullopt, ICON_NONE);
  const RoofMode mode = RoofMode(RNA_enum_get(ptr, "mode"));
  if (mode == RoofMode::Build) {
    layout.prop(ptr, "shape", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    const RoofShape shape = RoofShape(RNA_enum_get(ptr, "shape"));
    if (shape == RoofShape::Flat) {
      ui::Layout &parapet = layout.column(true);
      parapet.prop(ptr, "parapet_height", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      parapet.prop(ptr, "parapet_width", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    }
    else {
      const bool two_slopes = ELEM(shape, RoofShape::Gambrel, RoofShape::Mansard);
      ui::Layout &slope = layout.column(true);
      slope.prop(ptr, "pitch", UI_ITEM_NONE, two_slopes ? std::optional<StringRef>(IFACE_("Upper Pitch")) : std::nullopt,
                 ICON_NONE);
      if (two_slopes) {
        slope.prop(ptr, "lower_pitch", UI_ITEM_NONE, std::nullopt, ICON_NONE);
        slope.prop(ptr, "break_ratio", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      }
      if (shape != RoofShape::Pyramid) {
        ui::Layout &ridge = layout.column(true, IFACE_("Auto Ridge"));
        ridge.prop(ptr, "use_auto_ridge", UI_ITEM_NONE, "", ICON_NONE);
        ui::Layout &angle = ridge.column(true);
        angle.active_set(!RNA_boolean_get(ptr, "use_auto_ridge"));
        angle.prop(ptr, "ridge_angle", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      }
    }
  }
  else {
    layout.prop(ptr, "flat_angle", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    layout.prop(ptr, "use_flat_roofing", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }

  ui::Layout &overhang = layout.column(true);
  overhang.prop(ptr, "eave_overhang", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  overhang.prop(ptr, "rake_overhang", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "thickness", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  modifier_vgroup_ui(layout, ptr, &ob_ptr, "vertex_group", "invert_vertex_group", std::nullopt);
  modifier_error_message_draw(layout, ptr);
}

static void covering_panel_draw(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, nullptr);
  layout.use_property_split_set(true);

  layout.prop(ptr, "covering", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  const RoofCovering covering = RoofCovering(RNA_enum_get(ptr, "covering"));
  if (covering == RoofCovering::None) {
    return;
  }
  const bool rows = covering != RoofCovering::Metal;
  ui::Layout &col = layout.column(true);
  if (rows) {
    col.prop(ptr, "row_spacing", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  col.prop(ptr,
           "tile_width",
           UI_ITEM_NONE,
           covering == RoofCovering::Metal ? std::optional<StringRef>(IFACE_("Sheet Width")) :
                                            std::nullopt,
           ICON_NONE);
  col.prop(ptr, "covering_thickness", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  if (ELEM(covering, RoofCovering::Tiles, RoofCovering::Shingles)) {
    col.prop(ptr, "overlap", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  if (rows) {
    col.prop(ptr, "covering_gap", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }

  ui::Layout &variation = layout.column(true);
  variation.prop(ptr, "random_height", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  variation.prop(ptr, "tilt", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "missing", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "seed", UI_ITEM_NONE, std::nullopt, ICON_NONE);
}

static void trim_panel_draw(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, nullptr);
  layout.use_property_split_set(true);

  ui::Layout &caps = layout.column(true, IFACE_("Ridge Caps"));
  caps.prop(ptr, "use_ridge_caps", UI_ITEM_NONE, "", ICON_NONE);
  ui::Layout &cap_width = caps.column(true);
  cap_width.active_set(RNA_boolean_get(ptr, "use_ridge_caps"));
  cap_width.prop(ptr, "cap_width", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  ui::Layout &valleys = layout.column(true, IFACE_("Valleys"));
  valleys.prop(ptr, "use_valleys", UI_ITEM_NONE, "", ICON_NONE);
  ui::Layout &valley_width = valleys.column(true);
  valley_width.active_set(RNA_boolean_get(ptr, "use_valleys"));
  valley_width.prop(ptr, "valley_width", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "cap_angle", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  ui::Layout &fascia = layout.column(true);
  fascia.prop(ptr, "fascia_height", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  fascia.prop(ptr, "fascia_thickness", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  ui::Layout &gutters = layout.column(true, IFACE_("Gutters"));
  gutters.prop(ptr, "use_gutters", UI_ITEM_NONE, "", ICON_NONE);
  ui::Layout &gutter_size = gutters.column(true);
  gutter_size.active_set(RNA_boolean_get(ptr, "use_gutters"));
  gutter_size.prop(ptr, "gutter_size", UI_ITEM_NONE, std::nullopt, ICON_NONE);
}

static void materials_panel_draw(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, nullptr);
  layout.use_property_split_set(true);

  ui::Layout &col = layout.column(true);
  col.prop(ptr, "roof_material_index", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  col.prop(ptr, "covering_material_index", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  col.prop(ptr, "trim_material_index", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  col.prop(ptr, "wall_material_index", UI_ITEM_NONE, std::nullopt, ICON_NONE);
}

static void panel_register(ARegionType *region_type)
{
  PanelType *parent = modifier_panel_register(region_type, eModifierType_Roof, panel_draw);
  modifier_subpanel_register(
      region_type, "covering", "Covering", nullptr, covering_panel_draw, parent);
  modifier_subpanel_register(region_type, "trim", "Trim", nullptr, trim_panel_draw, parent);
  modifier_subpanel_register(
      region_type, "materials", "Materials", nullptr, materials_panel_draw, parent);
}

/* Taken from the cache when nothing it depends on changed, see #MOD_cached_modify_mesh. */
static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  return MOD_cached_modify_mesh(md, ctx, mesh, modify_mesh_compute);
}

ModifierTypeInfo modifierType_Roof = {
    /*idname*/ "Roof",
    /*name*/ N_("Roof"),
    /*struct_name*/ "RoofModifierData",
    /*struct_size*/ sizeof(RoofModifierData),
    /*srna*/ &RNA_RoofModifier,
    /*type*/ ModifierTypeType::Constructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode,
    /*icon*/ ICON_MOD_ROOF,

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
