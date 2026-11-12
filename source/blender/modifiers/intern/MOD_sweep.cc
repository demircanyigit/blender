/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Sweep. The work is done by shared code in the geometry module, which the matching nodes use too.
 * The path can be curves, or the edges of a mesh, and can be resampled and smoothed first.
 */

#include <algorithm>

#include "MEM_guardedalloc.h"

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
#include "BKE_curves.hh"
#include "BKE_geometry_set.hh"
#include "BKE_geometry_set_instances.hh"
#include "DNA_curves_types.h"
#include "MEM_guardedalloc.h"
#include "GEO_curve_sweep.hh"
#include "GEO_mesh_to_curve.hh"
#include "GEO_resample_curves.hh"
#include "GEO_smooth_curves.hh"

namespace blender {

static void init_data_SweepModifierData(ModifierData *md)
{
  SweepModifierData *data = reinterpret_cast<SweepModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

static bool is_disabled(const Scene * /*scene*/, ModifierData *md, bool /*use_render_params*/)
{
  const SweepModifierData *smd = reinterpret_cast<SweepModifierData *>(md);
  return smd->shape == int(geometry::SweepShape::CrossSection) && smd->profile_object == nullptr;
}

static void foreach_ID_link(ModifierData *md, Object *ob, IDWalkFunc walk, void *user_data)
{
  SweepModifierData *smd = reinterpret_cast<SweepModifierData *>(md);
  walk(user_data, ob, reinterpret_cast<ID **>(&smd->profile_object), IDWALK_CB_NOP);
}

static void update_depsgraph(ModifierData *md, const ModifierUpdateDepsgraphContext *ctx)
{
  SweepModifierData *smd = reinterpret_cast<SweepModifierData *>(md);
  if (smd->profile_object != nullptr && smd->shape == int(geometry::SweepShape::CrossSection)) {
    DEG_add_object_relation(
        ctx->node, smd->profile_object, DEG_OB_COMP_GEOMETRY, "Sweep Modifier");
  }
}

/** The edges of a mesh as curves: connected edges become one curve, closed loops cyclic. */
static bke::CurvesGeometry mesh_edges_as_curves(const Mesh &mesh)
{
  return geometry::mesh_edges_to_curves_convert(
      mesh, IndexMask(IndexRange(mesh.edges_num)), bke::AttributeFilter());
}

static void modify_geometry_set(ModifierData *md,
                                const ModifierEvalContext *ctx,
                                bke::GeometrySet *geometry_set)
{
  const SweepModifierData &smd = *reinterpret_cast<SweepModifierData *>(md);
  /* The path: curves, or else the edges of a mesh. */
  const Curves *curves_id = geometry_set->get_curves();
  const Mesh *path_mesh = curves_id ? nullptr : geometry_set->get_mesh();
  bke::CurvesGeometry main;
  if (curves_id != nullptr) {
    main = curves_id->geometry.wrap();
  }
  else if (path_mesh != nullptr && path_mesh->edges_num > 0) {
    main = mesh_edges_as_curves(*path_mesh);
  }
  else {
    return;
  }
  if (main.curves_num() == 0) {
    return;
  }

  /* Even points along the path, then smoothing, which rounds the corners. */
  if (smd.resample == MOD_SWEEP_RESAMPLE_COUNT) {
    main = geometry::resample_to_count(
        main,
        main.curves_range(),
        VArray<int>::from_single(std::max(smd.resample_count, 2), main.curves_num()));
  }
  else if (smd.resample == MOD_SWEEP_RESAMPLE_LENGTH) {
    main = geometry::resample_to_length(
        main,
        main.curves_range(),
        VArray<float>::from_single(std::max(smd.resample_length, 1e-4f), main.curves_num()),
        {},
        true);
  }
  if (smd.smooth_iterations > 0 && smd.smooth_factor > 0.0f) {
    geometry::smooth_curve_positions(main,
                                     main.curves_range(),
                                     VArray<bool>::from_single(true, main.points_num()),
                                     smd.smooth_iterations,
                                     smd.smooth_factor,
                                     !(smd.flag & MOD_SWEEP_SMOOTH_KEEP_ENDS),
                                     false);
    main.tag_positions_changed();
  }

  bke::CurvesGeometry built_profile;
  const bke::CurvesGeometry *profile = &built_profile;
  bke::GeometrySet profile_set;
  if (smd.shape == int(geometry::SweepShape::CrossSection)) {
    if (smd.profile_object == nullptr) {
      return;
    }
    profile_set = bke::object_get_evaluated_geometry_set(*smd.profile_object);
    const Curves *profile_curves = profile_set.get_curves();
    const Mesh *profile_mesh = profile_set.get_mesh();
    if (profile_curves != nullptr) {
      profile = &profile_curves->geometry.wrap();
    }
    else if (profile_mesh != nullptr && profile_mesh->edges_num > 0) {
      built_profile = mesh_edges_as_curves(*profile_mesh);
    }
    else {
      BKE_modifier_set_warning(
          ctx->object, md, "The cross section object has no curves or edges");
      return;
    }
  }
  else {
    built_profile = geometry::sweep_shape_profile(
        geometry::SweepShape(smd.shape), smd.columns, smd.radius, smd.width);
  }

  const int points_num = main.points_num();
  geometry::sweep_add_roll_and_twist(main,
                                     VArray<float>::from_single(smd.roll, points_num),
                                     VArray<float>::from_single(smd.twist, points_num));
  Array<float> scales(points_num, smd.scale);
  if (smd.flag & MOD_SWEEP_USE_RADIUS) {
    const VArray<float> radii = *main.attributes().lookup_or_default<float>(
        "radius", bke::AttrDomain::Point, 1.0f);
    for (const int i : scales.index_range()) {
      scales[i] *= radii[i];
    }
  }
  geometry::SweepOutputs outputs;
  if (smd.uv_map[0] != '\0') {
    outputs.uv_map = std::string(smd.uv_map);
  }
  Mesh *mesh = geometry::curve_sweep(main,
                                     *profile,
                                     VArray<float>::from_span(scales),
                                     smd.flag & MOD_SWEEP_FILL_CAPS,
                                     smd.flag & MOD_SWEEP_LENGTH_UVS,
                                     outputs,
                                     {});
  if (mesh != nullptr) {
    if (curves_id != nullptr) {
      mesh->mat = MEM_dupalloc(curves_id->mat);
      mesh->totcol = curves_id->totcol;
    }
    else if (path_mesh->mat != nullptr) {
      mesh->mat = MEM_dupalloc(path_mesh->mat);
      mesh->totcol = path_mesh->totcol;
    }
    if (outputs.uv_map) {
      mesh->uv_maps_active_set(*outputs.uv_map);
      mesh->uv_maps_default_set(*outputs.uv_map);
    }
  }
  geometry_set->replace_mesh(mesh);
  geometry_set->keep_only({bke::GeometryComponent::Type::Mesh,
                           bke::GeometryComponent::Type::Instance,
                           bke::GeometryComponent::Type::Edit});
}

static void panel_draw_Sweep(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);
  UNUSED_VARS(ob_ptr);

  layout.prop(ptr, "shape", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.use_property_split_set(true);
  if (RNA_enum_get(ptr, "shape") == 3) {
    layout.prop(ptr, "profile_object", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  if (RNA_enum_get(ptr, "shape") <= 2) {
    layout.prop(ptr, "columns", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  if (RNA_enum_get(ptr, "shape") == 0) {
    layout.prop(ptr, "radius", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  if (RNA_enum_get(ptr, "shape") == 1 || RNA_enum_get(ptr, "shape") == 2) {
    layout.prop(ptr, "width", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  layout.prop(ptr, "scale", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "use_radius", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "roll", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "twist", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "use_fill_caps", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "uv_map", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "use_length_uvs", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  layout.separator();
  layout.prop(ptr, "resample", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  const int resample = RNA_enum_get(ptr, "resample");
  if (resample == MOD_SWEEP_RESAMPLE_COUNT) {
    layout.prop(ptr, "resample_count", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  else if (resample == MOD_SWEEP_RESAMPLE_LENGTH) {
    layout.prop(ptr, "resample_length", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  layout.prop(ptr, "smooth_iterations", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  ui::Layout &col = layout.column(true);
  col.active_set(RNA_int_get(ptr, "smooth_iterations") > 0);
  col.prop(ptr, "smooth_factor", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  col.prop(ptr, "use_smooth_keep_ends", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  modifier_error_message_draw(layout, ptr);
}

static void panel_register_Sweep(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_Sweep, panel_draw_Sweep);
}

ModifierTypeInfo modifierType_Sweep = {
    /*idname*/ "Sweep",
    /*name*/ N_("Sweep"),
    /*struct_name*/ "SweepModifierData",
    /*struct_size*/ sizeof(SweepModifierData),
    /*srna*/ &RNA_SweepModifier,
    /*type*/ ModifierTypeType::Constructive,
    /*flags*/ eModifierTypeFlag_AcceptsCurves | eModifierTypeFlag_AcceptsMesh |
        eModifierTypeFlag_SupportsEditmode,
    /*icon*/ ICON_MOD_SWEEP,

    /*copy_data*/ BKE_modifier_copydata_generic,

    /*deform_verts*/ nullptr,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ nullptr,
    /*modify_geometry_set*/ modify_geometry_set,

    /*init_data*/ init_data_SweepModifierData,
    /*required_data_mask*/ nullptr,
    /*free_data*/ nullptr,
    /*is_disabled*/ is_disabled,
    /*update_depsgraph*/ update_depsgraph,
    /*depends_on_time*/ nullptr,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ foreach_ID_link,
    /*foreach_tex_link*/ nullptr,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ panel_register_Sweep,
    /*blend_write*/ nullptr,
    /*blend_read*/ nullptr,
    /*foreach_cache*/ nullptr,
    /*foreach_working_space_color*/ nullptr,
};

}  // namespace blender
