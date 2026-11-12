/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Pack UVs: packs the islands of an existing UV map into the 0 to 1 square, with the same
 * packer as the Pack Islands operator.
 */

#include "BLI_array.hh"
#include "BLI_math_vector_types.hh"

#include "BLT_translation.hh"

#include "DNA_mesh_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_types.h"
#include "DNA_screen_types.h"

#include "BKE_attribute.hh"
#include "BKE_customdata.hh"
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
  UVPackModifierData *data = reinterpret_cast<UVPackModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

static void required_data_mask(ModifierData * /*md*/, CustomData_MeshMasks *mask)
{
  mask->lmask |= CD_MASK_PROP_FLOAT2;
}

static eUVPackIsland_ShapeMethod shape_method(const int shape)
{
  switch (shape) {
    case MOD_UV_PACK_SHAPE_BOX:
      return ED_UVPACK_SHAPE_AABB;
    case MOD_UV_PACK_SHAPE_EXACT:
      return ED_UVPACK_SHAPE_CONCAVE;
    default:
      return ED_UVPACK_SHAPE_CONVEX;
  }
}

static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  const UVPackModifierData &pmd = *reinterpret_cast<const UVPackModifierData *>(md);
  if (mesh->faces_num == 0) {
    return mesh;
  }
  const StringRef name = pmd.uv_map[0] ? StringRef(pmd.uv_map) : mesh->active_uv_map_name();
  bke::MutableAttributeAccessor attributes = mesh->attributes_for_write();
  const std::optional<bke::AttributeMetaData> meta = name.is_empty() ?
                                                         std::nullopt :
                                                         attributes.lookup_meta_data(name);
  if (!meta || meta->domain != bke::AttrDomain::Corner ||
      meta->data_type != bke::AttrType::Float2)
  {
    BKE_modifier_set_error(ctx->object, md, "Missing UV map");
    return mesh;
  }
  bke::SpanAttributeWriter<float2> uvs = attributes.lookup_for_write_span<float2>(name);
  if (!uvs) {
    return mesh;
  }

  const Span<float3> positions = mesh->vert_positions();
  const OffsetIndices faces = mesh->faces();
  const Span<int> corner_verts = mesh->corner_verts();

  /* The islands are the parts that are connected in the UVs. */
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
      uv[i] = uvs.span[corner];
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
  geometry::uv_parametrizer_construct_end(handle, true, true, nullptr);

  geometry::UVPackIsland_Params params;
  params.shape_method = shape_method(pmd.shape);
  params.rotate_method = (pmd.flag & MOD_UV_PACK_ROTATE) ? ED_UVPACK_ROTATION_ANY :
                                                           ED_UVPACK_ROTATION_NONE;
  params.margin = pmd.margin;
  geometry::uv_parametrizer_pack(handle, params);
  geometry::uv_parametrizer_flush(handle);
  delete handle;

  uvs.finish();
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
  layout.prop(ptr, "shape", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "use_rotate", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "margin", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  modifier_error_message_draw(layout, ptr);
}

static void panel_register(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_UVPack, panel_draw);
}

ModifierTypeInfo modifierType_UVPack = {
    /*idname*/ "UVPack",
    /*name*/ N_("Pack UVs"),
    /*struct_name*/ "UVPackModifierData",
    /*struct_size*/ sizeof(UVPackModifierData),
    /*srna*/ &RNA_UVPackModifier,
    /*type*/ ModifierTypeType::Nonconstructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode |
        eModifierTypeFlag_EnableInEditmode,
    /*icon*/ ICON_MOD_UV_PACK,
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
