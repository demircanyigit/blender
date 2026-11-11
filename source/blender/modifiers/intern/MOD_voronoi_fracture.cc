/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Voronoi Fracture. Cuts a closed mesh into solid pieces, one for the area around each point.
 * The cutting is done by shared code in the geometry module, which the Voronoi Fracture node
 * uses too.
 */

#include "BLI_math_matrix.hh"
#include "BLI_math_vector.hh"
#include "BLI_rand.hh"
#include "BLI_vector.hh"

#include "BLT_translation.hh"

#include "DNA_mesh_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_types.h"
#include "DNA_pointcloud_types.h"
#include "DNA_screen_types.h"

#include "BKE_attribute.hh"
#include "BKE_curves.hh"
#include "BKE_geometry_set.hh"
#include "BKE_geometry_set_instances.hh"
#include "BKE_instances.hh"
#include "BKE_lib_query.hh"
#include "BKE_mesh.hh"
#include "BKE_modifier.hh"

#include "DEG_depsgraph_build.hh"
#include "DEG_depsgraph_query.hh"

#include "UI_interface_layout.hh"
#include "UI_resources.hh"

#include "RNA_access.hh"
#include "RNA_prototypes.hh"

#include "GEO_mesh_voronoi_fracture.hh"
#include "GEO_randomize.hh"

#include "MOD_modifiertypes.hh"
#include "MOD_ui_common.hh"
#include "MOD_util.hh"

namespace blender {

/** Internal face attribute used to find the cut faces for the interior material. */
static const char *INTERIOR_ATTRIBUTE = ".voronoi_fracture_interior";

static void init_data_VoronoiFractureModifierData(ModifierData *md)
{
  VoronoiFractureModifierData *data = reinterpret_cast<VoronoiFractureModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

static bool is_disabled(const Scene * /*scene*/, ModifierData *md, bool /*use_render_params*/)
{
  const VoronoiFractureModifierData *vmd = reinterpret_cast<VoronoiFractureModifierData *>(md);
  return vmd->source == MOD_VORONOI_SOURCE_OBJECT && vmd->points_object == nullptr;
}

static void foreach_ID_link(ModifierData *md, Object *ob, IDWalkFunc walk, void *user_data)
{
  VoronoiFractureModifierData *vmd = reinterpret_cast<VoronoiFractureModifierData *>(md);
  walk(user_data, ob, reinterpret_cast<ID **>(&vmd->points_object), IDWALK_CB_NOP);
}

static void update_depsgraph(ModifierData *md, const ModifierUpdateDepsgraphContext *ctx)
{
  VoronoiFractureModifierData *vmd = reinterpret_cast<VoronoiFractureModifierData *>(md);
  if (vmd->source == MOD_VORONOI_SOURCE_OBJECT && vmd->points_object != nullptr) {
    DEG_add_object_relation(
        ctx->node, vmd->points_object, DEG_OB_COMP_GEOMETRY, "Voronoi Fracture Modifier");
    DEG_add_object_relation(
        ctx->node, vmd->points_object, DEG_OB_COMP_TRANSFORM, "Voronoi Fracture Modifier");
    DEG_add_depends_on_transform_relation(ctx->node, "Voronoi Fracture Modifier");
  }
}

/** Random points in the bounds of the mesh. Cells of points outside the mesh make no piece. */
static Vector<float3> random_points(const Mesh &mesh, const int count, const int seed)
{
  Vector<float3> points;
  const std::optional<Bounds<float3>> bounds = mesh.bounds_min_max();
  if (!bounds) {
    return points;
  }
  RandomNumberGenerator rng(uint32_t(seed) * 2654435761u + 7u);
  const float3 size = bounds->max - bounds->min;
  points.reserve(count);
  for ([[maybe_unused]] const int i : IndexRange(count)) {
    const float3 t(rng.get_float(), rng.get_float(), rng.get_float());
    points.append(bounds->min + size * t);
  }
  return points;
}

/** Points, mesh vertices, curve points and instances of another object, in this object's
 * space. */
static Vector<float3> object_points(const Object &self, const Object &points_ob)
{
  Vector<float3> points;
  const bke::GeometrySet geometry = bke::object_get_evaluated_geometry_set(points_ob);
  if (const PointCloud *pointcloud = geometry.get_pointcloud()) {
    points.extend(pointcloud->positions());
  }
  if (const Mesh *mesh = geometry.get_mesh()) {
    points.extend(mesh->vert_positions());
  }
  if (const Curves *curves = geometry.get_curves()) {
    points.extend(curves->geometry.wrap().positions());
  }
  if (const bke::Instances *instances = geometry.get_instances()) {
    for (const float4x4 &transform : instances->transforms()) {
      points.append(transform.location());
    }
  }
  const float4x4 to_self = self.world_to_object() * points_ob.object_to_world();
  for (float3 &point : points) {
    point = math::transform_point(to_self, point);
  }
  return points;
}

static Mesh *modify_mesh_compute(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  const VoronoiFractureModifierData &vmd = *reinterpret_cast<VoronoiFractureModifierData *>(md);
  if (mesh->faces_num == 0) {
    return mesh;
  }

  Vector<float3> points;
  if (vmd.source == MOD_VORONOI_SOURCE_OBJECT) {
    if (vmd.points_object == nullptr) {
      return mesh;
    }
    points = object_points(*ctx->object, *vmd.points_object);
  }
  else {
    points = random_points(*mesh, vmd.count, vmd.seed);
  }
  if (points.is_empty()) {
    return mesh;
  }

  const bool use_interior_material = vmd.flag & MOD_VORONOI_INTERIOR_MATERIAL;
  geometry::MeshVoronoiFractureParams params;
  params.cut_gap = vmd.cut_gap;
  params.separate_islands = vmd.flag & MOD_VORONOI_SEPARATE_ISLANDS;
  if (use_interior_material) {
    params.interior_id = INTERIOR_ATTRIBUTE;
  }

  geometry::MeshVoronoiFractureError error = geometry::MeshVoronoiFractureError::NoError;
  Mesh *result = geometry::mesh_voronoi_fracture(*mesh, points, params, &error);
  if (result == nullptr) {
    if (error == geometry::MeshVoronoiFractureError::NonManifold) {
      BKE_modifier_set_error(ctx->object, md, "The mesh must be closed and manifold");
    }
    else if (error == geometry::MeshVoronoiFractureError::SolverNotAvailable) {
      BKE_modifier_set_error(ctx->object, md, "Built without the Manifold library");
    }
    else {
      BKE_modifier_set_error(ctx->object, md, "The mesh could not be fractured");
    }
    return mesh;
  }

  if (use_interior_material) {
    bke::MutableAttributeAccessor attributes = result->attributes_for_write();
    {
      const VArraySpan<bool> interior = *attributes.lookup_or_default<bool>(
          INTERIOR_ATTRIBUTE, bke::AttrDomain::Face, false);
      bke::SpanAttributeWriter<int> material_index =
          attributes.lookup_or_add_for_write_span<int>("material_index", bke::AttrDomain::Face);
      if (material_index) {
        for (const int face : interior.index_range()) {
          if (interior[face]) {
            material_index.span[face] = vmd.interior_material;
          }
        }
        material_index.finish();
      }
    }
    attributes.remove(INTERIOR_ATTRIBUTE);
  }

  geometry::debug_randomize_mesh_order(result);
  return result;
}

static void panel_draw_VoronoiFracture(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);

  layout.prop(ptr, "source", ui::ITEM_R_EXPAND, std::nullopt, ICON_NONE);
  layout.use_property_split_set(true);
  if (RNA_enum_get(ptr, "source") == MOD_VORONOI_SOURCE_OBJECT) {
    layout.prop(ptr, "points_object", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  else {
    layout.prop(ptr, "count", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    layout.prop(ptr, "seed", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  layout.prop(ptr, "cut_gap", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "use_separate_islands", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  ui::Layout &row = layout.row(true, IFACE_("Interior Material"));
  row.prop(ptr, "use_interior_material", UI_ITEM_NONE, "", ICON_NONE);
  ui::Layout &sub = row.row(true);
  sub.active_set(RNA_boolean_get(ptr, "use_interior_material"));
  sub.prop(ptr, "interior_material", UI_ITEM_NONE, "", ICON_NONE);

  modifier_error_message_draw(layout, ptr);
}

static void panel_register_VoronoiFracture(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_VoronoiFracture, panel_draw_VoronoiFracture);
}

/* Taken from the cache when nothing it depends on changed, see #MOD_cached_modify_mesh. Points
 * from another object are not checked for changes, so then it is not cached. */
static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  const VoronoiFractureModifierData &vmd = *reinterpret_cast<VoronoiFractureModifierData *>(md);
  if (vmd.source == MOD_VORONOI_SOURCE_OBJECT && vmd.points_object != nullptr) {
    return modify_mesh_compute(md, ctx, mesh);
  }
  return MOD_cached_modify_mesh(md, ctx, mesh, modify_mesh_compute);
}

ModifierTypeInfo modifierType_VoronoiFracture = {
    /*idname*/ "VoronoiFracture",
    /*name*/ N_("Voronoi Fracture"),
    /*struct_name*/ "VoronoiFractureModifierData",
    /*struct_size*/ sizeof(VoronoiFractureModifierData),
    /*srna*/ &RNA_VoronoiFractureModifier,
    /*type*/ ModifierTypeType::Constructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode,
    /*icon*/ ICON_MOD_VORONOI_FRACTURE,

    /*copy_data*/ BKE_modifier_copydata_generic,

    /*deform_verts*/ nullptr,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ modify_mesh,
    /*modify_geometry_set*/ nullptr,

    /*init_data*/ init_data_VoronoiFractureModifierData,
    /*required_data_mask*/ nullptr,
    /*free_data*/ nullptr,
    /*is_disabled*/ is_disabled,
    /*update_depsgraph*/ update_depsgraph,
    /*depends_on_time*/ nullptr,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ foreach_ID_link,
    /*foreach_tex_link*/ nullptr,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ panel_register_VoronoiFracture,
    /*blend_write*/ nullptr,
    /*blend_read*/ nullptr,
    /*foreach_cache*/ nullptr,
    /*foreach_working_space_color*/ nullptr,
};

}  // namespace blender
