/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Cover faces with blocks laid in a brick, stone or paving pattern.
 */

#include "BLI_array.hh"
#include "BLI_utildefines.h"
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
#include "BKE_modifier.hh"

#include "DEG_depsgraph_build.hh"

#include "UI_interface_layout.hh"
#include "UI_resources.hh"

#include "RNA_access.hh"
#include "RNA_prototypes.hh"

#include "GEO_mesh_masonry.hh"
#include "GEO_randomize.hh"

#include "MOD_modifiertypes.hh"
#include "MOD_ui_common.hh"
#include "MOD_util.hh"

namespace blender {

using geometry::MasonryPattern;

static void init_data(ModifierData *md)
{
  MasonryModifierData *data = reinterpret_cast<MasonryModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

static void foreach_ID_link(ModifierData *md, Object *ob, IDWalkFunc walk, void *user_data)
{
  MasonryModifierData *mmd = reinterpret_cast<MasonryModifierData *>(md);
  walk(user_data, ob, reinterpret_cast<ID **>(&mmd->center), IDWALK_CB_NOP);
}

static void update_depsgraph(ModifierData *md, const ModifierUpdateDepsgraphContext *ctx)
{
  const MasonryModifierData *mmd = reinterpret_cast<MasonryModifierData *>(md);
  if (mmd->center != nullptr) {
    DEG_add_object_relation(ctx->node, mmd->center, DEG_OB_COMP_TRANSFORM, "Masonry Modifier");
    DEG_add_depends_on_transform_relation(ctx->node, "Masonry Modifier");
  }
}

static bool uses_rows(const int pattern)
{
  return ELEM(MasonryPattern(pattern),
              MasonryPattern::Running,
              MasonryPattern::Stack,
              MasonryPattern::Flemish,
              MasonryPattern::English);
}

static bool uses_center(const int pattern)
{
  return ELEM(MasonryPattern(pattern), MasonryPattern::Fan, MasonryPattern::Radial);
}

static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  const MasonryModifierData &mmd = *reinterpret_cast<MasonryModifierData *>(md);
  if (mesh->faces_num == 0) {
    return mesh;
  }

  geometry::MeshMasonryParams params;
  params.pattern = MasonryPattern(mmd.pattern);
  params.direction = geometry::MasonryDirection(mmd.direction);
  params.length = mmd.length;
  params.height = mmd.height;
  params.depth = mmd.depth;
  params.mortar = mmd.mortar;
  params.offset = mmd.offset;
  params.backing = mmd.flag & MOD_MASONRY_BACKING;
  params.angle = mmd.angle;
  params.random_length = mmd.random_length;
  params.random_height = mmd.random_height;
  params.random_depth = mmd.random_depth;
  params.tilt = mmd.tilt;
  params.stone_shape = mmd.stone_shape;
  params.dome = mmd.dome;
  params.soldier_rows = mmd.soldier_rows;
  params.border = mmd.flag & MOD_MASONRY_BORDER;
  params.toothed = mmd.flag & MOD_MASONRY_TOOTHED;
  params.seed = mmd.seed;
  if (mmd.center != nullptr && mmd.center != ctx->object) {
    const float4x4 to_self = ctx->object->world_to_object() * mmd.center->object_to_world();
    params.center = to_self.location();
  }
  if (mmd.flag & MOD_MASONRY_MATERIALS) {
    params.block_material = mmd.block_material;
    params.mortar_material = mmd.mortar_material;
  }

  Array<bool> selection;
  const MDeformVert *dvert = nullptr;
  int defgrp_index = -1;
  MOD_get_vgroup(ctx->object, mesh, mmd.vertex_group, &dvert, &defgrp_index);
  if (dvert != nullptr) {
    const bool invert = mmd.flag & MOD_MASONRY_INVERT_VGROUP;
    const OffsetIndices<int> faces = mesh->faces();
    const Span<int> corner_verts = mesh->corner_verts();
    selection.reinitialize(mesh->faces_num);
    for (const int face : faces.index_range()) {
      bool all_in = true;
      for (const int vert : corner_verts.slice(faces[face])) {
        if (!(BKE_defvert_find_weight(&dvert[vert], defgrp_index) > 0.0f)) {
          all_in = false;
          break;
        }
      }
      selection[face] = all_in != invert;
    }
    params.selection = selection;
  }

  Mesh *result = geometry::mesh_masonry(*mesh, params);
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
  const int pattern = RNA_enum_get(ptr, "pattern");

  layout.use_property_split_set(true);
  layout.prop(ptr, "pattern", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  ui::Layout &frame = layout.column(true);
  frame.prop(ptr, "direction", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  frame.prop(ptr, "angle", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  if (uses_center(pattern)) {
    layout.prop(ptr, "center", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }

  ui::Layout &size = layout.column(true);
  size.prop(ptr, "length", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  size.prop(ptr, "height", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  size.prop(ptr, "depth", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  ui::Layout &joint = layout.column(true);
  joint.prop(ptr, "mortar", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  joint.prop(ptr, "offset", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  if (uses_rows(pattern)) {
    layout.prop(ptr, "soldier_rows", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }

  ui::Layout &options = layout.column(true, IFACE_("Options"));
  options.prop(ptr, "use_backing", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  options.prop(ptr, "use_border", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  options.prop(ptr, "use_toothed", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  modifier_vgroup_ui(layout, ptr, &ob_ptr, "vertex_group", "invert_vertex_group", std::nullopt);

  modifier_error_message_draw(layout, ptr);
}

static void variation_panel_draw(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, nullptr);

  layout.use_property_split_set(true);
  ui::Layout &size = layout.column(true);
  size.prop(ptr, "random_length", UI_ITEM_NONE, IFACE_("Length"), ICON_NONE);
  size.prop(ptr, "random_height", UI_ITEM_NONE, IFACE_("Height"), ICON_NONE);
  size.prop(ptr, "random_depth", UI_ITEM_NONE, IFACE_("Depth"), ICON_NONE);

  ui::Layout &shape = layout.column(true);
  shape.prop(ptr, "tilt", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  shape.prop(ptr, "stone_shape", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  shape.prop(ptr, "dome", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  layout.prop(ptr, "seed", UI_ITEM_NONE, std::nullopt, ICON_NONE);
}

static void materials_panel_header_draw(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, nullptr);
  layout.prop(ptr, "use_materials", UI_ITEM_NONE, std::nullopt, ICON_NONE);
}

static void materials_panel_draw(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, nullptr);

  layout.use_property_split_set(true);
  ui::Layout &col = layout.column(true);
  col.active_set(RNA_boolean_get(ptr, "use_materials"));
  col.prop(ptr, "block_material", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  col.prop(ptr, "mortar_material", UI_ITEM_NONE, std::nullopt, ICON_NONE);
}

static void panel_register(ARegionType *region_type)
{
  PanelType *panel_type = modifier_panel_register(
      region_type, eModifierType_Masonry, panel_draw);
  modifier_subpanel_register(
      region_type, "variation", "Variation", nullptr, variation_panel_draw, panel_type);
  modifier_subpanel_register(
      region_type, "materials", "", materials_panel_header_draw, materials_panel_draw, panel_type);
}

ModifierTypeInfo modifierType_Masonry = {
    /*idname*/ "Masonry",
    /*name*/ N_("Masonry"),
    /*struct_name*/ "MasonryModifierData",
    /*struct_size*/ sizeof(MasonryModifierData),
    /*srna*/ &RNA_MasonryModifier,
    /*type*/ ModifierTypeType::Constructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode,
    /*icon*/ ICON_MOD_MASONRY,

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
