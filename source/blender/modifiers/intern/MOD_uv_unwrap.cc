/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Unwrap: flattens the mesh into UVs, cut at the seams, with the same solvers as the Unwrap
 * operator, so the UVs follow the stack.
 */

#include "BLI_array.hh"
#include "BLI_math_vector_types.hh"

#include "BLT_translation.hh"

#include "DNA_mesh_types.h"
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

#include "GEO_uv_pack.hh"
#include "GEO_uv_parametrizer.hh"

#include "MOD_modifiertypes.hh"
#include "MOD_ui_common.hh"

namespace blender {

static void init_data(ModifierData *md)
{
  UVUnwrapModifierData *data = reinterpret_cast<UVUnwrapModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

/** The UV map to write: the named one, else the active one, else "UVMap". */
static std::string uv_map_name(const Mesh &mesh, const char *name)
{
  if (name[0]) {
    return name;
  }
  const StringRef active = mesh.active_uv_map_name();
  return active.is_empty() ? std::string("UVMap") : std::string(active);
}

static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  const UVUnwrapModifierData &umd = *reinterpret_cast<const UVUnwrapModifierData *>(md);
  if (mesh->faces_num == 0) {
    return mesh;
  }
  const Span<float3> positions = mesh->vert_positions();
  const Span<int2> edges = mesh->edges();
  const OffsetIndices faces = mesh->faces();
  const Span<int> corner_verts = mesh->corner_verts();

  bke::MutableAttributeAccessor attributes = mesh->attributes_for_write();
  const VArraySpan<bool> seams = *attributes.lookup_or_default<bool>(
      "uv_seam", bke::AttrDomain::Edge, false);

  Array<float2> uvs(mesh->corners_num, float2(0.0f));
  geometry::ParamHandle *handle = new geometry::ParamHandle();
  for (const int face_index : faces.index_range()) {
    const IndexRange face = faces[face_index];
    Array<geometry::ParamKey, 16> vkeys(face.size());
    Array<bool, 16> pin(face.size(), false);
    Array<bool, 16> select(face.size(), false);
    Array<const float *, 16> co(face.size());
    Array<float *, 16> uv(face.size());
    for (const int i : IndexRange(face.size())) {
      const int corner = face[i];
      const int vert = corner_verts[corner];
      vkeys[i] = vert;
      co[i] = positions[vert];
      uv[i] = uvs[corner];
    }
    geometry::uv_parametrizer_face_add(handle,
                                       face_index,
                                       face.size(),
                                       vkeys.data(),
                                       co.data(),
                                       uv.data(),
                                       nullptr,
                                       pin.data(),
                                       select.data());
  }
  for (const int edge : edges.index_range()) {
    if (seams[edge]) {
      geometry::ParamKey vkeys[2]{geometry::ParamKey(edges[edge][0]),
                                  geometry::ParamKey(edges[edge][1])};
      geometry::uv_parametrizer_edge_set_seam(handle, vkeys);
    }
  }
  geometry::uv_parametrizer_construct_end(
      handle, umd.flag & MOD_UV_UNWRAP_FILL_HOLES, false, nullptr);

  int failed = 0;
  if (umd.method == MOD_UV_UNWRAP_MINIMUM_STRETCH) {
    geometry::ParamSlimOptions slim_options{};
    slim_options.iterations = umd.iterations;
    slim_options.no_flip = umd.flag & MOD_UV_UNWRAP_NO_FLIP;
    slim_options.skip_init = false;
    geometry::uv_parametrizer_slim_solve(handle, &slim_options, nullptr, &failed);
  }
  else {
    geometry::uv_parametrizer_lscm_begin(
        handle, false, umd.method == MOD_UV_UNWRAP_ANGLE_BASED);
    geometry::uv_parametrizer_lscm_solve(handle, nullptr, &failed);
    geometry::uv_parametrizer_lscm_end(handle);
  }
  geometry::uv_parametrizer_average(handle, true, false, false);
  geometry::UVPackIsland_Params pack_params;
  pack_params.margin = umd.margin;
  pack_params.rotate_method = ED_UVPACK_ROTATION_ANY;
  geometry::uv_parametrizer_pack(handle, pack_params);
  geometry::uv_parametrizer_flush(handle);
  delete handle;

  if (failed > 0) {
    BKE_modifier_set_error(ctx->object, md, "%d island(s) could not be unwrapped", failed);
  }

  const std::string name = uv_map_name(*mesh, umd.uv_map);
  if (const std::optional<bke::AttributeMetaData> meta = attributes.lookup_meta_data(name)) {
    if (meta->domain != bke::AttrDomain::Corner || meta->data_type != bke::AttrType::Float2) {
      attributes.remove(name);
    }
  }
  bke::SpanAttributeWriter<float2> writer = attributes.lookup_or_add_for_write_only_span<float2>(
      name, bke::AttrDomain::Corner);
  if (!writer) {
    return mesh;
  }
  writer.span.copy_from(uvs);
  writer.finish();
  if (mesh->active_uv_map_name().is_empty()) {
    mesh->uv_maps_active_set(name);
  }
  return mesh;
}

static void panel_draw(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);
  PointerRNA mesh_ptr = RNA_pointer_get(&ob_ptr, "data");

  layout.use_property_split_set(true);
  layout.prop_search(ptr, "uv_map", &mesh_ptr, "uv_layers", std::nullopt, ICON_GROUP_UVS);
  layout.prop(ptr, "method", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  if (RNA_enum_get(ptr, "method") == MOD_UV_UNWRAP_MINIMUM_STRETCH) {
    layout.prop(ptr, "iterations", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    layout.prop(ptr, "use_no_flip", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  layout.prop(ptr, "use_fill_holes", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "margin", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  modifier_error_message_draw(layout, ptr);
}

static void panel_register(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_UVUnwrap, panel_draw);
}

ModifierTypeInfo modifierType_UVUnwrap = {
    /*idname*/ "UVUnwrap",
    /*name*/ N_("Unwrap"),
    /*struct_name*/ "UVUnwrapModifierData",
    /*struct_size*/ sizeof(UVUnwrapModifierData),
    /*srna*/ &RNA_UVUnwrapModifier,
    /*type*/ ModifierTypeType::Nonconstructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode |
        eModifierTypeFlag_EnableInEditmode,
    /*icon*/ ICON_MOD_UV_UNWRAP,
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
