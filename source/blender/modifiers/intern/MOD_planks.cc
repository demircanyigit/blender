/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Floors and walls of boards on the faces of a mesh, and crates and barrels made of boards.
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

#include "GEO_mesh_sim_cache.hh"
#include "GEO_mesh_planks.hh"
#include "GEO_randomize.hh"

#include "MOD_modifiertypes.hh"
#include "MOD_ui_common.hh"
#include "MOD_util.hh"

namespace blender {

using geometry::PlanksPattern;
using geometry::PlanksType;
using geometry::PlanksWallStyle;

static void init_data(ModifierData *md)
{
  auto *data = reinterpret_cast<PlanksModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

static void required_data_mask(ModifierData *md, CustomData_MeshMasks *r_cddata_masks)
{
  const auto &data = *reinterpret_cast<PlanksModifierData *>(md);
  if (data.vertex_group[0] != '\0') {
    r_cddata_masks->vmask |= CD_MASK_MDEFORMVERT;
  }
}

static Mesh *modify_mesh_compute(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  const auto &data = *reinterpret_cast<PlanksModifierData *>(md);
  const PlanksType type = PlanksType(data.type);
  const bool on_faces = ELEM(type, PlanksType::Floor, PlanksType::Wall);
  if (on_faces && mesh->faces_num == 0) {
    return mesh;
  }

  geometry::MeshPlanksParams params;
  params.type = type;
  params.width = data.width;
  params.thickness = data.thickness;
  params.gap = data.gap;
  params.pattern = PlanksPattern(data.pattern);
  params.wall_style = PlanksWallStyle(data.wall_style);
  params.length = data.length;
  params.random_length = data.random_length;
  params.min_length = data.min_length;
  params.joints = geometry::PlanksJoints(data.joints);
  params.angle = data.angle;
  params.border = data.flag & MOD_PLANKS_BORDER;
  params.supports = data.flag & MOD_PLANKS_SUPPORTS;
  params.support_spacing = data.support_spacing;
  params.support_width = data.support_width;
  params.support_depth = data.support_depth;
  params.lap = data.lap;
  params.batten_width = data.batten_width;
  params.keep_surface = data.flag & MOD_PLANKS_KEEP_SURFACE;
  params.crate_size = float3(data.crate_size);
  params.slatted = data.flag & MOD_PLANKS_SLATTED;
  params.slat_gap = data.slat_gap;
  params.frame = data.flag & MOD_PLANKS_FRAME;
  params.brace = geometry::PlanksBrace(data.brace);
  params.corner_posts = data.flag & MOD_PLANKS_CORNER_POSTS;
  params.post_width = data.post_width;
  params.lid = geometry::PlanksLid(data.lid);
  params.lid_angle = data.lid_angle;
  params.barrel_height = data.barrel_height;
  params.barrel_radius = data.barrel_radius;
  params.bulge = data.bulge;
  params.staves = data.staves;
  params.chime = data.chime;
  params.barrel_ends = geometry::PlanksBarrelEnds(data.barrel_ends);
  params.hoops = data.hoops;
  params.hoop_width = data.hoop_width;
  params.hoop_thickness = data.hoop_thickness;
  params.hoop_offset = data.hoop_offset;
  params.random_height = data.random_height;
  params.tilt = data.tilt;
  params.cup = data.cup;
  params.bow = data.bow;
  params.missing = data.missing;
  params.seed = data.seed;
  const int totcol = ctx->object->totcol;
  params.material = data.material >= 0 && data.material < std::max(totcol, 1) ? data.material :
                                                                               -1;
  params.material_count = data.material_count;
  params.metal_material = data.metal_material >= 0 && data.metal_material < std::max(totcol, 1) ?
                              data.metal_material :
                              -1;
  params.board_index_id = "plank_id";
  params.random_id = "plank_random";
  params.part_id = "plank_part";

  Array<bool> selection;
  if (on_faces) {
    const MDeformVert *dvert = nullptr;
    int defgrp_index = -1;
    MOD_get_vgroup(ctx->object, mesh, data.vertex_group, &dvert, &defgrp_index);
    if (dvert != nullptr) {
      const bool invert = data.flag & MOD_PLANKS_INVERT_VGROUP;
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
  }

  std::string message;
  Mesh *result = geometry::mesh_planks(mesh, params, message);
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

  layout.prop(ptr, "plank_type", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  const PlanksType type = PlanksType(RNA_enum_get(ptr, "plank_type"));

  ui::Layout &boards = layout.column(true);
  boards.prop(ptr, "width", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  boards.prop(ptr, "thickness", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  boards.prop(ptr, "gap", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  switch (type) {
    case PlanksType::Floor:
    case PlanksType::Wall: {
      const bool floor = type == PlanksType::Floor;
      bool rows = true;
      if (floor) {
        layout.prop(ptr, "pattern", UI_ITEM_NONE, std::nullopt, ICON_NONE);
        rows = PlanksPattern(RNA_enum_get(ptr, "pattern")) == PlanksPattern::Straight;
      }
      else {
        layout.prop(ptr, "wall_style", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      }
      ui::Layout &col = layout.column(true);
      col.prop(ptr, "length", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      if (rows) {
        col.prop(ptr, "random_length", UI_ITEM_NONE, std::nullopt, ICON_NONE);
        col.prop(ptr, "min_length", UI_ITEM_NONE, std::nullopt, ICON_NONE);
        layout.prop(ptr, "joints", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      }
      if (!floor) {
        const PlanksWallStyle style = PlanksWallStyle(RNA_enum_get(ptr, "wall_style"));
        if (style == PlanksWallStyle::Lap) {
          layout.prop(ptr, "lap", UI_ITEM_NONE, std::nullopt, ICON_NONE);
        }
        else if (style == PlanksWallStyle::BoardAndBatten) {
          layout.prop(ptr, "batten_width", UI_ITEM_NONE, std::nullopt, ICON_NONE);
        }
      }
      layout.prop(ptr, "angle", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      if (floor) {
        layout.prop(ptr, "use_border", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      }
      layout.prop(ptr, "use_keep_surface", UI_ITEM_NONE, std::nullopt, ICON_NONE);

      ui::Layout &supports = layout.column(true, floor ? IFACE_("Joists") : IFACE_("Studs"));
      supports.prop(ptr, "use_supports", UI_ITEM_NONE, "", ICON_NONE);
      ui::Layout &support_values = supports.column(true);
      support_values.active_set(RNA_boolean_get(ptr, "use_supports"));
      support_values.prop(ptr, "support_spacing", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      support_values.prop(ptr, "support_width", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      support_values.prop(ptr, "support_depth", UI_ITEM_NONE, std::nullopt, ICON_NONE);

      modifier_vgroup_ui(
          layout, ptr, &ob_ptr, "vertex_group", "invert_vertex_group", std::nullopt);
      break;
    }
    case PlanksType::Crate: {
      layout.prop(ptr, "crate_size", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      ui::Layout &slats = layout.column(true, IFACE_("Slatted"));
      slats.prop(ptr, "use_slatted", UI_ITEM_NONE, "", ICON_NONE);
      ui::Layout &slat_gap = slats.column(true);
      slat_gap.active_set(RNA_boolean_get(ptr, "use_slatted"));
      slat_gap.prop(ptr, "slat_gap", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      layout.prop(ptr, "use_frame", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      layout.prop(ptr, "brace", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      ui::Layout &posts = layout.column(true, IFACE_("Corner Posts"));
      posts.prop(ptr, "use_corner_posts", UI_ITEM_NONE, "", ICON_NONE);
      ui::Layout &post_width = posts.column(true);
      post_width.active_set(RNA_boolean_get(ptr, "use_corner_posts"));
      post_width.prop(ptr, "post_width", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      layout.prop(ptr, "lid", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      if (geometry::PlanksLid(RNA_enum_get(ptr, "lid")) == geometry::PlanksLid::Open) {
        layout.prop(ptr, "lid_angle", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      }
      break;
    }
    case PlanksType::Barrel: {
      ui::Layout &shape = layout.column(true);
      shape.prop(ptr, "barrel_height", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      shape.prop(ptr, "barrel_radius", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      shape.prop(ptr, "bulge", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      layout.prop(ptr, "staves", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      layout.prop(ptr, "barrel_ends", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      layout.prop(ptr, "chime", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      ui::Layout &hoops = layout.column(true);
      hoops.prop(ptr, "hoops", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      hoops.prop(ptr, "hoop_width", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      hoops.prop(ptr, "hoop_thickness", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      hoops.prop(ptr, "hoop_offset", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      break;
    }
  }

  modifier_error_message_draw(layout, ptr);
}

static void variation_panel_draw(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, nullptr);
  layout.use_property_split_set(true);

  const PlanksType type = PlanksType(RNA_enum_get(ptr, "plank_type"));
  ui::Layout &col = layout.column(true);
  col.prop(ptr, "random_height", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  if (type != PlanksType::Barrel) {
    col.prop(ptr, "tilt", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    col.prop(ptr, "cup", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    col.prop(ptr, "bow", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  layout.prop(ptr, "missing", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "seed", UI_ITEM_NONE, std::nullopt, ICON_NONE);
}

static void materials_panel_draw(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, nullptr);
  layout.use_property_split_set(true);

  ui::Layout &col = layout.column(true);
  col.prop(ptr, "material_index", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  col.prop(ptr, "material_count", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  if (PlanksType(RNA_enum_get(ptr, "plank_type")) == PlanksType::Barrel) {
    layout.prop(ptr, "metal_material_index", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
}

static void panel_register(ARegionType *region_type)
{
  PanelType *parent = modifier_panel_register(region_type, eModifierType_Planks, panel_draw);
  modifier_subpanel_register(
      region_type, "variation", "Variation", nullptr, variation_panel_draw, parent);
  modifier_subpanel_register(
      region_type, "materials", "Materials", nullptr, materials_panel_draw, parent);
}

/* Taken from the cache when nothing it depends on changed, see #MOD_cached_modify_mesh. */
static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  /* The materials are spread over the object's material slots. */
  return MOD_cached_modify_mesh(
      md, ctx, mesh, modify_mesh_compute, [&](geometry::SimCacheHasher &hasher) {
        hasher.add(ctx->object->totcol);
      });
}

ModifierTypeInfo modifierType_Planks = {
    /*idname*/ "Planks",
    /*name*/ N_("Planks"),
    /*struct_name*/ "PlanksModifierData",
    /*struct_size*/ sizeof(PlanksModifierData),
    /*srna*/ &RNA_PlanksModifier,
    /*type*/ ModifierTypeType::Constructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode,
    /*icon*/ ICON_MOD_PLANKS,

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
