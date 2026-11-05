/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Poly Edit. Runs one edit (move, extrude, inset, poke or delete) on the selected elements, where
 * the selection is a boolean attribute from further down the stack, usually written by a Poly
 * Select modifier. The new faces become the selection, so the next Poly Edit carries on from them.
 */

#include "BLI_array.hh"
#include "BLI_index_mask.hh"
#include "BLI_math_vector.hh"
#include "BLI_utildefines.h"
#include "BLI_virtual_array.hh"

#include "BLT_translation.hh"

#include "DNA_mesh_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_types.h"
#include "DNA_screen_types.h"

#include "BKE_attribute.hh"
#include "BKE_lib_query.hh"
#include "BKE_mesh.hh"
#include "BKE_modifier.hh"

#include "UI_interface_layout.hh"
#include "UI_resources.hh"

#include "RNA_access.hh"
#include "RNA_prototypes.hh"

#include "MOD_modifiertypes.hh"
#include "MOD_ui_common.hh"
#include "MOD_util.hh"

#include "GEO_mesh_copy_selection.hh"
#include "GEO_mesh_face_extrude.hh"
#include "GEO_mesh_topology_ops.hh"
#include "GEO_randomize.hh"

namespace blender {

/** Temporary face attribute for the faces an edit made, renamed to the selection afterwards. */
static const char *TOP_ATTRIBUTE = ".poly_edit_top";

static void init_data_PolyEditModifierData(ModifierData *md)
{
  PolyEditModifierData *data = reinterpret_cast<PolyEditModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

/** The selection on a domain. Every element when no attribute is named, none when it's missing. */
static VArray<bool> selection_on(const Mesh &mesh, const char *name, const bke::AttrDomain domain)
{
  const bke::AttributeAccessor attributes = mesh.attributes();
  const int size = attributes.domain_size(domain);
  if (name[0] == '\0') {
    return VArray<bool>::from_single(true, size);
  }
  if (VArray<bool> selection = *attributes.lookup<bool>(name, domain)) {
    return selection;
  }
  return VArray<bool>::from_single(false, size);
}

/** Move the selected points by the offset and scale them around their center. */
static Mesh *edit_move(const PolyEditModifierData &pmd, Mesh *mesh)
{
  const VArray<bool> selection = selection_on(*mesh, pmd.selection_attribute, bke::AttrDomain::Point);
  IndexMaskMemory memory;
  const IndexMask mask = IndexMask::from_bools(selection, memory);
  if (mask.is_empty()) {
    return mesh;
  }
  MutableSpan<float3> positions = mesh->vert_positions_for_write();
  float3 center(0.0f);
  mask.foreach_index([&](const int i) { center += positions[i]; });
  center /= float(mask.size());
  const float3 offset(pmd.offset);
  mask.foreach_index(GrainSize(4096), [&](const int i) {
    positions[i] = center + (positions[i] - center) * pmd.scale + offset;
  });
  mesh->tag_positions_changed();
  return mesh;
}

/** Delete the selected elements, on the domain the selection is stored on. */
static Mesh *edit_delete(const PolyEditModifierData &pmd, Mesh *mesh)
{
  bke::AttrDomain domain = bke::AttrDomain::Face;
  if (pmd.selection_attribute[0] != '\0') {
    const std::optional<bke::AttributeMetaData> meta_data = mesh->attributes().lookup_meta_data(
        pmd.selection_attribute);
    if (!meta_data) {
      return mesh;
    }
    if (ELEM(meta_data->domain, bke::AttrDomain::Point, bke::AttrDomain::Edge)) {
      domain = meta_data->domain;
    }
  }
  const VArray<bool> selection = selection_on(*mesh, pmd.selection_attribute, domain);
  const VArray<bool> keep = VArray<bool>::from_func(selection.size(),
                                                    [&](const int i) { return !selection[i]; });
  const std::optional<Mesh *> result = geometry::mesh_copy_selection(*mesh, keep, domain);
  if (!result) {
    return mesh;
  }
  if (*result == nullptr) {
    return BKE_mesh_new_nomain_from_template(mesh, 0, 0, 0, 0);
  }
  return *result;
}

/** Make the faces the edit wrote to #TOP_ATTRIBUTE the new selection. */
static void top_to_selection(const PolyEditModifierData &pmd, Mesh &mesh)
{
  bke::MutableAttributeAccessor attributes = mesh.attributes_for_write();
  if (!attributes.contains(TOP_ATTRIBUTE)) {
    return;
  }
  if ((pmd.flag & MOD_POLY_EDIT_SELECT_NEW) && pmd.selection_attribute[0] != '\0') {
    Array<bool> top(mesh.faces_num);
    const VArray<bool> top_varray = *attributes.lookup<bool>(TOP_ATTRIBUTE, bke::AttrDomain::Face);
    top_varray.materialize(top);
    attributes.remove(pmd.selection_attribute);
    attributes.add<bool>(pmd.selection_attribute,
                         bke::AttrDomain::Face,
                         bke::AttributeInitVArray(VArray<bool>::from_container(std::move(top))));
  }
  attributes.remove(TOP_ATTRIBUTE);
}

static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext * /*ctx*/, Mesh *mesh)
{
  const PolyEditModifierData &pmd = *reinterpret_cast<PolyEditModifierData *>(md);
  if (pmd.operation == MOD_POLY_EDIT_MOVE) {
    return edit_move(pmd, mesh);
  }
  if (pmd.operation == MOD_POLY_EDIT_DELETE) {
    return edit_delete(pmd, mesh);
  }

  const VArray<bool> selection = selection_on(*mesh, pmd.selection_attribute, bke::AttrDomain::Face);
  IndexMaskMemory memory;
  const IndexMask faces = IndexMask::from_bools(selection, memory);
  if (faces.is_empty()) {
    return mesh;
  }

  Mesh *result = nullptr;
  switch (pmd.operation) {
    case MOD_POLY_EDIT_EXTRUDE: {
      geometry::FaceExtrudeParams params;
      params.amount = pmd.amount;
      params.scale = pmd.scale;
      params.individual = pmd.flag & MOD_POLY_EDIT_INDIVIDUAL;
      params.top_id = TOP_ATTRIBUTE;
      result = geometry::mesh_face_extrude(*mesh, faces, params);
      break;
    }
    case MOD_POLY_EDIT_INSET: {
      geometry::InsetParams params;
      params.individual = pmd.flag & MOD_POLY_EDIT_INDIVIDUAL;
      params.thickness = pmd.amount;
      params.depth = pmd.depth;
      geometry::TopologyOutputs outputs;
      outputs.top = TOP_ATTRIBUTE;
      result = geometry::mesh_inset(*mesh, faces, params, outputs);
      break;
    }
    case MOD_POLY_EDIT_POKE: {
      geometry::PokeParams params;
      params.offset = pmd.amount;
      geometry::TopologyOutputs outputs;
      outputs.top = TOP_ATTRIBUTE;
      result = geometry::mesh_poke(*mesh, faces, params, outputs);
      break;
    }
    default:
      break;
  }
  if (result == nullptr) {
    return mesh;
  }
  top_to_selection(pmd, *result);
  geometry::debug_randomize_mesh_order(result);
  return result;
}

static void panel_draw_PolyEdit(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);

  layout.use_property_split_set(true);
  layout.prop(ptr, "operation", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "selection_attribute", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  const int operation = RNA_enum_get(ptr, "operation");
  switch (operation) {
    case MOD_POLY_EDIT_MOVE:
      layout.prop(ptr, "offset", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      layout.prop(ptr, "scale", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      break;
    case MOD_POLY_EDIT_EXTRUDE:
      layout.prop(ptr, "amount", UI_ITEM_NONE, IFACE_("Height"), ICON_NONE);
      layout.prop(ptr, "scale", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      layout.prop(ptr, "use_individual", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      break;
    case MOD_POLY_EDIT_INSET:
      layout.prop(ptr, "amount", UI_ITEM_NONE, IFACE_("Thickness"), ICON_NONE);
      layout.prop(ptr, "depth", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      layout.prop(ptr, "use_individual", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      break;
    case MOD_POLY_EDIT_POKE:
      layout.prop(ptr, "amount", UI_ITEM_NONE, IFACE_("Offset"), ICON_NONE);
      break;
    default:
      break;
  }
  if (ELEM(operation, MOD_POLY_EDIT_EXTRUDE, MOD_POLY_EDIT_INSET, MOD_POLY_EDIT_POKE)) {
    layout.prop(ptr, "use_select_new", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  modifier_error_message_draw(layout, ptr);
}

static void panel_register_PolyEdit(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_PolyEdit, panel_draw_PolyEdit);
}

ModifierTypeInfo modifierType_PolyEdit = {
    /*idname*/ "PolyEdit",
    /*name*/ N_("Poly Edit"),
    /*struct_name*/ "PolyEditModifierData",
    /*struct_size*/ sizeof(PolyEditModifierData),
    /*srna*/ &RNA_PolyEditModifier,
    /*type*/ ModifierTypeType::Constructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode |
        eModifierTypeFlag_EnableInEditmode,
    /*icon*/ ICON_MOD_POLY_EDIT,

    /*copy_data*/ BKE_modifier_copydata_generic,

    /*deform_verts*/ nullptr,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ modify_mesh,
    /*modify_geometry_set*/ nullptr,

    /*init_data*/ init_data_PolyEditModifierData,
    /*required_data_mask*/ nullptr,
    /*free_data*/ nullptr,
    /*is_disabled*/ nullptr,
    /*update_depsgraph*/ nullptr,
    /*depends_on_time*/ nullptr,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ nullptr,
    /*foreach_tex_link*/ nullptr,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ panel_register_PolyEdit,
    /*blend_write*/ nullptr,
    /*blend_read*/ nullptr,
    /*foreach_cache*/ nullptr,
    /*foreach_working_space_color*/ nullptr,
};

}  // namespace blender
