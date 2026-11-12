/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Normal Shape: points the shading normals as if the mesh were a sphere, cylinder or rounded box
 * that fits around it. Good for soft looking tree crowns, bushes and stylized hair.
 */

#include <algorithm>

#include "BLI_array.hh"
#include "BLI_bounds.hh"
#include "BLI_math_matrix.hh"
#include "BLI_math_vector.hh"
#include "BLI_task.hh"

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

#include "UI_interface_layout.hh"
#include "UI_resources.hh"

#include "RNA_access.hh"
#include "RNA_prototypes.hh"

#include "MOD_modifiertypes.hh"
#include "MOD_ui_common.hh"
#include "MOD_util.hh"

namespace blender {

static void init_data(ModifierData *md)
{
  NormalShapeModifierData *data = reinterpret_cast<NormalShapeModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

static void required_data_mask(ModifierData *md, CustomData_MeshMasks *r_cddata_masks)
{
  const NormalShapeModifierData &nmd = *reinterpret_cast<NormalShapeModifierData *>(md);
  if (nmd.vertex_group[0] != '\0') {
    r_cddata_masks->vmask |= CD_MASK_MDEFORMVERT;
  }
}

static bool is_disabled(const Scene * /*scene*/, ModifierData *md, bool /*use_render_params*/)
{
  const NormalShapeModifierData &nmd = *reinterpret_cast<NormalShapeModifierData *>(md);
  return nmd.factor <= 0.0f;
}

static void foreach_ID_link(ModifierData *md, Object *ob, IDWalkFunc walk, void *user_data)
{
  NormalShapeModifierData *nmd = reinterpret_cast<NormalShapeModifierData *>(md);
  walk(user_data, ob, reinterpret_cast<ID **>(&nmd->center), IDWALK_CB_NOP);
}

static void update_depsgraph(ModifierData *md, const ModifierUpdateDepsgraphContext *ctx)
{
  NormalShapeModifierData *nmd = reinterpret_cast<NormalShapeModifierData *>(md);
  if (nmd->center != nullptr) {
    DEG_add_object_relation(
        ctx->node, nmd->center, DEG_OB_COMP_TRANSFORM, "Normal Shape Modifier");
    DEG_add_depends_on_transform_relation(ctx->node, "Normal Shape Modifier");
  }
}

/**
 * Custom normals don't show on flat faces. Faces the modifier changes are made smooth, and their
 * edges to flat faces it doesn't change are made sharp, so those stay flat.
 */
static void smooth_changed_faces(Mesh &mesh, const Span<float> weights)
{
  bke::MutableAttributeAccessor attributes = mesh.attributes_for_write();
  Array<bool> sharp_faces;
  {
    const VArraySpan<bool> span = *attributes.lookup<bool>("sharp_face", bke::AttrDomain::Face);
    if (span.is_empty()) {
      return;
    }
    sharp_faces = Array<bool>(span);
  }
  const OffsetIndices faces = mesh.faces();
  const Span<int> corner_verts = mesh.corner_verts();
  const Span<int> corner_edges = mesh.corner_edges();

  Array<bool> changed(faces.size(), false);
  for (const int f : faces.index_range()) {
    for (const int corner : faces[f]) {
      if (weights[corner_verts[corner]] > 0.0f) {
        changed[f] = true;
        break;
      }
    }
  }
  /* Bit 1: next to a changed face. Bit 2: next to a flat face that isn't changed. */
  Array<uint8_t> edge_state(mesh.edges_num, 0);
  for (const int f : faces.index_range()) {
    const uint8_t bit = changed[f] ? 1 : (sharp_faces[f] ? 2 : 0);
    for (const int corner : faces[f]) {
      edge_state[corner_edges[corner]] |= bit;
    }
  }
  bke::SpanAttributeWriter<bool> sharp_edges = attributes.lookup_or_add_for_write_span<bool>(
      "sharp_edge", bke::AttrDomain::Edge);
  for (const int e : edge_state.index_range()) {
    if (edge_state[e] == 3) {
      sharp_edges.span[e] = true;
    }
  }
  sharp_edges.finish();

  bool any_sharp = false;
  for (const int f : faces.index_range()) {
    sharp_faces[f] = sharp_faces[f] && !changed[f];
    any_sharp |= sharp_faces[f];
  }
  attributes.remove("sharp_face");
  if (any_sharp) {
    attributes.add<bool>("sharp_face",
                         bke::AttrDomain::Face,
                         bke::AttributeInitVArray(VArray<bool>::from_span(sharp_faces)));
  }
  mesh.tag_sharpness_changed();
}

static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  const NormalShapeModifierData &nmd = *reinterpret_cast<const NormalShapeModifierData *>(md);
  if (mesh->corners_num == 0 || nmd.factor <= 0.0f) {
    return mesh;
  }
  const int verts_num = mesh->verts_num;
  const Span<float3> positions = mesh->vert_positions();
  const std::optional<Bounds<float3>> bounds = bounds::min_max(positions);
  if (!bounds) {
    return mesh;
  }
  /* The shape fits the bounds of the mesh, so a flat or long mesh gets a squashed shape. */
  float3 center = math::midpoint(bounds->min, bounds->max);
  const float3 half = math::max((bounds->max - bounds->min) * 0.5f, float3(1e-6f));
  if (nmd.center != nullptr) {
    center = math::transform_point(
        ctx->object->world_to_object(), float3(nmd.center->object_to_world().location()));
  }

  Array<float> weights(verts_num, std::min(nmd.factor, 1.0f));
  const MDeformVert *dvert = nullptr;
  int defgrp_index = -1;
  MOD_get_vgroup(ctx->object, mesh, nmd.vertex_group, &dvert, &defgrp_index);
  if (dvert != nullptr && defgrp_index != -1) {
    const bool invert = nmd.flag & MOD_NORMAL_SHAPE_INVERT_VGROUP;
    for (const int i : IndexRange(verts_num)) {
      const float w = BKE_defvert_find_weight(&dvert[i], defgrp_index);
      weights[i] *= invert ? 1.0f - w : w;
    }
  }

  const int axis = std::clamp(nmd.axis, 0, 2);
  Array<float3> shape_normals(verts_num);
  threading::parallel_for(IndexRange(verts_num), 4096, [&](const IndexRange range) {
    for (const int v : range) {
      /* Position inside the bounds, -1 to 1 on each axis. */
      const float3 q = (positions[v] - center) / half;
      float3 n;
      switch (nmd.shape) {
        case MOD_NORMAL_SHAPE_CYLINDER:
          n = q / half;
          n[axis] = 0.0f;
          break;
        case MOD_NORMAL_SHAPE_BOX:
          /* A rounded box: the side the point is closest to wins, with soft corners. */
          n = (q * q * q) / half;
          break;
        default:
          /* Outward direction of the ellipsoid that fits the bounds. */
          n = q / half;
          break;
      }
      shape_normals[v] = math::normalize(n);
      if (math::is_zero(shape_normals[v])) {
        weights[v] = 0.0f;
      }
    }
  });

  Mesh *result = BKE_mesh_copy_for_eval(*mesh);
  smooth_changed_faces(*result, weights);

  const Span<int> corner_verts = result->corner_verts();
  const Span<float3> corner_normals = result->corner_normals();
  Array<float3> custom(corner_verts.size());
  threading::parallel_for(corner_verts.index_range(), 4096, [&](const IndexRange range) {
    for (const int corner : range) {
      const int v = corner_verts[corner];
      custom[corner] = math::normalize(
          math::interpolate(corner_normals[corner], shape_normals[v], weights[v]));
    }
  });
  bke::mesh_set_custom_normals_normalized(*result, custom);
  return result;
}

static void panel_draw(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);

  layout.use_property_split_set(true);
  layout.prop(ptr, "shape", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  if (RNA_enum_get(ptr, "shape") == MOD_NORMAL_SHAPE_CYLINDER) {
    layout.prop(ptr, "axis", ui::ITEM_R_EXPAND, std::nullopt, ICON_NONE);
  }
  layout.prop(ptr, "center", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "factor", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  modifier_vgroup_ui(layout, ptr, &ob_ptr, "vertex_group", "invert_vertex_group", std::nullopt);
  modifier_error_message_draw(layout, ptr);
}

static void panel_register(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_NormalShape, panel_draw);
}

ModifierTypeInfo modifierType_NormalShape = {
    /*idname*/ "NormalShape",
    /*name*/ N_("Normal Shape"),
    /*struct_name*/ "NormalShapeModifierData",
    /*struct_size*/ sizeof(NormalShapeModifierData),
    /*srna*/ &RNA_NormalShapeModifier,
    /*type*/ ModifierTypeType::Constructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsMapping |
        eModifierTypeFlag_SupportsEditmode | eModifierTypeFlag_EnableInEditmode,
    /*icon*/ ICON_MOD_NORMAL_SHAPE,
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
    /*is_disabled*/ is_disabled,
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
