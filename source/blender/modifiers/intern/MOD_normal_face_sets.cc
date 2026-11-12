/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Normals from Face Sets: the shading is hard where one face set meets another and smooth inside
 * each set. With Weighted, big faces count more inside a set, so they stay flat.
 */

#include "BLI_array.hh"
#include "BLI_math_vector.hh"
#include "BLI_task.hh"

#include "BLT_translation.hh"

#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_types.h"
#include "DNA_screen_types.h"

#include "BKE_attribute.hh"
#include "BKE_mesh.hh"
#include "BKE_modifier.hh"

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
  NormalFaceSetsModifierData *data = reinterpret_cast<NormalFaceSetsModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  const NormalFaceSetsModifierData &nmd =
      *reinterpret_cast<const NormalFaceSetsModifierData *>(md);
  if (mesh->faces_num == 0) {
    return mesh;
  }
  Array<int> face_sets(mesh->faces_num);
  {
    const VArray<int> varray = *mesh->attributes().lookup<int>(".sculpt_face_set",
                                                               bke::AttrDomain::Face);
    if (!varray) {
      BKE_modifier_set_error(ctx->object, md, "No face sets");
      return mesh;
    }
    varray.materialize(face_sets);
  }
  const bool keep_sharp = nmd.flag & MOD_NORMAL_FACE_SETS_KEEP_SHARP;

  Mesh *result = BKE_mesh_copy_for_eval(*mesh);
  const OffsetIndices faces = result->faces();
  const Span<int> corner_verts = result->corner_verts();
  const Span<int> corner_edges = result->corner_edges();
  const int edges_num = result->edges_num;

  /* An edge is a border when its faces aren't all in one set. */
  Array<int> edge_set(edges_num, -1);
  Array<bool> border(edges_num, false);
  Array<bool> seen(edges_num, false);
  for (const int f : faces.index_range()) {
    for (const int corner : faces[f]) {
      const int e = corner_edges[corner];
      if (!seen[e]) {
        seen[e] = true;
        edge_set[e] = face_sets[f];
      }
      else if (edge_set[e] != face_sets[f]) {
        border[e] = true;
      }
    }
  }

  bke::MutableAttributeAccessor attributes = result->attributes_for_write();
  /* Sharp edges that are kept and aren't borders, inside a set. */
  Array<bool> inner_sharp(edges_num, false);
  {
    bke::SpanAttributeWriter<bool> sharp_edges = attributes.lookup_or_add_for_write_span<bool>(
        "sharp_edge", bke::AttrDomain::Edge);
    for (const int e : IndexRange(edges_num)) {
      const bool old = keep_sharp && sharp_edges.span[e];
      inner_sharp[e] = old && !border[e];
      sharp_edges.span[e] = old || border[e];
    }
    sharp_edges.finish();
  }
  attributes.remove("sharp_face");
  result->tag_sharpness_changed();

  if (!(nmd.flag & MOD_NORMAL_FACE_SETS_WEIGHTED)) {
    return result;
  }

  /* Weighted: each corner takes the normals of the faces around its point that are in the same
   * set, by face area. Points at a kept sharp edge inside a set keep the usual normals, so that
   * edge stays hard. */
  const Span<float3> positions = result->vert_positions();
  const Span<float3> face_normals = result->face_normals();
  const GroupedSpan<int> vert_to_face = result->vert_to_face_map();
  Array<float> areas(faces.size());
  threading::parallel_for(faces.index_range(), 1024, [&](const IndexRange range) {
    for (const int f : range) {
      areas[f] = bke::mesh::face_area_calc(positions, corner_verts.slice(faces[f]));
    }
  });
  Array<bool> vert_inner_sharp(result->verts_num, false);
  const Span<int2> edges = result->edges();
  for (const int e : IndexRange(edges_num)) {
    if (inner_sharp[e]) {
      vert_inner_sharp[edges[e][0]] = true;
      vert_inner_sharp[edges[e][1]] = true;
    }
  }

  const Span<float3> corner_normals = result->corner_normals();
  Array<float3> custom(corner_verts.size());
  threading::parallel_for(faces.index_range(), 512, [&](const IndexRange range) {
    for (const int f : range) {
      for (const int corner : faces[f]) {
        const int v = corner_verts[corner];
        if (vert_inner_sharp[v]) {
          custom[corner] = corner_normals[corner];
          continue;
        }
        float3 sum(0.0f);
        for (const int other : vert_to_face[v]) {
          if (face_sets[other] == face_sets[f]) {
            sum += face_normals[other] * areas[other];
          }
        }
        const float3 n = math::normalize(sum);
        custom[corner] = math::is_zero(n) ? corner_normals[corner] : n;
      }
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
  layout.prop(ptr, "use_weighted", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "use_keep_sharp", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  modifier_error_message_draw(layout, ptr);
}

static void panel_register(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_NormalFaceSets, panel_draw);
}

ModifierTypeInfo modifierType_NormalFaceSets = {
    /*idname*/ "NormalFaceSets",
    /*name*/ N_("Normals from Face Sets"),
    /*struct_name*/ "NormalFaceSetsModifierData",
    /*struct_size*/ sizeof(NormalFaceSetsModifierData),
    /*srna*/ &RNA_NormalFaceSetsModifier,
    /*type*/ ModifierTypeType::Constructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsMapping |
        eModifierTypeFlag_SupportsEditmode | eModifierTypeFlag_EnableInEditmode,
    /*icon*/ ICON_MOD_NORMAL_FACE_SETS,
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
