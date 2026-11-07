/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Edit Poly: a copy of the mesh at this point of the stack that is edited in Edit Mode. The
 * modifiers above it keep working on the edited mesh, and when the incoming mesh only moves (the
 * point count stays the same), the edited points move with it.
 */

#include <algorithm>
#include <cstring>
#include <string>

#include "MEM_guardedalloc.h"

#include "BLI_kdtree.hh"
#include "BLI_math_vector.hh"

#include "BLT_translation.hh"

#include "DNA_mesh_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_types.h"
#include "DNA_screen_types.h"

#include "BKE_attribute.hh"
#include "BKE_lib_query.hh"
#include "BKE_mesh.hh"
#include "BKE_modifier.hh"

#include "BLO_read_write.hh"

#include "DEG_depsgraph_build.hh"
#include "DEG_depsgraph_query.hh"

#include "UI_interface_layout.hh"
#include "UI_resources.hh"

#include "RNA_access.hh"
#include "RNA_prototypes.hh"

#include "MOD_modifiertypes.hh"
#include "MOD_ui_common.hh"
#include "MOD_util.hh"

namespace blender {

/** Point attribute of the edited mesh: where each point was when the copy was made. New points
 * get the value of the points they're made from. */
static const char *EDIT_POLY_BASE_ATTRIBUTE = ".edit_poly_base";

static void free_input_positions(EditPolyModifierData &emd)
{
  if (emd.input_positions) {
    MEM_delete_void(static_cast<void *>(emd.input_positions));
    emd.input_positions = nullptr;
  }
}

static void init_data(ModifierData *md)
{
  EditPolyModifierData *data = reinterpret_cast<EditPolyModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

static void free_data(ModifierData *md)
{
  EditPolyModifierData *emd = reinterpret_cast<EditPolyModifierData *>(md);
  free_input_positions(*emd);
}

static void copy_data(const ModifierData *md, ModifierData *target, const int flag)
{
  const EditPolyModifierData *emd = reinterpret_cast<const EditPolyModifierData *>(md);
  EditPolyModifierData *temd = reinterpret_cast<EditPolyModifierData *>(target);
  BKE_modifier_copydata_generic(md, target, flag);
  temd->input_positions = emd->input_positions ?
                              static_cast<float *>(MEM_dupalloc_void(emd->input_positions)) :
                              nullptr;
}

static void foreach_ID_link(ModifierData *md, Object *ob, IDWalkFunc walk, void *user_data)
{
  EditPolyModifierData *emd = reinterpret_cast<EditPolyModifierData *>(md);
  walk(user_data, ob, reinterpret_cast<ID **>(&emd->mesh), IDWALK_CB_USER);
  walk(user_data, ob, reinterpret_cast<ID **>(&emd->object_mesh), IDWALK_CB_USER);
}

static void update_depsgraph(ModifierData *md, const ModifierUpdateDepsgraphContext *ctx)
{
  EditPolyModifierData *emd = reinterpret_cast<EditPolyModifierData *>(md);
  if (emd->mesh != nullptr && !(emd->flag & MOD_EDIT_POLY_EDITING)) {
    DEG_add_generic_id_relation(ctx->node, &emd->mesh->id, "Edit Poly Modifier");
  }
}

static void store_input(ModifierData *md, const ModifierEvalContext *ctx, const Mesh &mesh)
{
  if (!DEG_is_active(ctx->depsgraph)) {
    return;
  }
  EditPolyModifierData *orig = reinterpret_cast<EditPolyModifierData *>(
      BKE_modifier_get_original(ctx->object, md));
  if (orig == nullptr) {
    return;
  }
  free_input_positions(*orig);
  const Span<float3> positions = mesh.vert_positions();
  orig->input_positions = static_cast<float *>(
      MEM_new_array_uninitialized(std::max<size_t>(positions.size(), 1), sizeof(float3), __func__));
  if (!positions.is_empty()) {
    memcpy(orig->input_positions, positions.data(), positions.size_in_bytes());
  }
  orig->input_verts_num = int(positions.size());
  orig->flag &= ~MOD_EDIT_POLY_STORE_INPUT;
}

/** Move the edited points by how far the incoming point they came from has moved. */
static void follow_input(const EditPolyModifierData &emd, const Mesh &input, Mesh &result)
{
  if (emd.input_positions == nullptr || input.verts_num != emd.input_verts_num ||
      input.verts_num == 0)
  {
    return;
  }
  const Span<float3> old_positions(reinterpret_cast<const float3 *>(emd.input_positions),
                                   emd.input_verts_num);
  const Span<float3> new_positions = input.vert_positions();
  Array<float3> offsets(input.verts_num);
  bool moved = false;
  for (const int i : offsets.index_range()) {
    offsets[i] = new_positions[i] - old_positions[i];
    moved |= !math::is_zero(offsets[i]);
  }
  if (!moved) {
    return;
  }
  bke::MutableAttributeAccessor attributes = result.attributes_for_write();
  bke::SpanAttributeWriter<float3> base = attributes.lookup_for_write_span<float3>(
      EDIT_POLY_BASE_ATTRIBUTE);
  if (!base) {
    return;
  }
  if (base.domain != bke::AttrDomain::Point) {
    base.finish();
    return;
  }
  KDTree_3d *tree = kdtree_3d_new(old_positions.size());
  for (const int i : old_positions.index_range()) {
    kdtree_3d_insert(tree, i, old_positions[i]);
  }
  kdtree_3d_balance(tree);
  MutableSpan<float3> positions = result.vert_positions_for_write();
  for (const int v : positions.index_range()) {
    const int i = kdtree_3d_find_nearest(tree, base.span[v], nullptr);
    if (i >= 0) {
      positions[v] += offsets[i];
      /* Kept up to date, so editing again can start from the moved points. */
      base.span[v] += offsets[i];
    }
  }
  base.finish();
  kdtree_3d_free(tree);
  result.tag_positions_changed();
}

static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  const EditPolyModifierData &emd = *reinterpret_cast<EditPolyModifierData *>(md);
  if (emd.mesh == nullptr) {
    return mesh;
  }
  if (emd.flag & MOD_EDIT_POLY_STORE_INPUT) {
    store_input(md, ctx, *mesh);
  }
  if (emd.flag & MOD_EDIT_POLY_EDITING) {
    /* The edited mesh is the object's mesh for now, and the modifiers before this one are off. */
    return mesh;
  }

  Mesh *result = BKE_mesh_copy_for_eval(*emd.mesh);
  if (emd.flag & MOD_EDIT_POLY_FOLLOW_INPUT) {
    follow_input(emd, *mesh, *result);
  }
  return result;
}

static void panel_draw(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);

  const bool has_mesh = RNA_pointer_get(ptr, "mesh").data != nullptr;
  const bool editing = RNA_boolean_get(ptr, "is_editing");
  const std::string name = RNA_string_get(ptr, "name");

  ui::Layout &col = layout.column(true);
  if (editing) {
    PointerRNA op_ptr = col.op("OBJECT_OT_edit_poly_finish", IFACE_("Finish Editing"), ICON_CHECKMARK);
    RNA_string_set(&op_ptr, "modifier", name.c_str());
  }
  else {
    ui::Layout &row = col.row(true);
    PointerRNA op_ptr = row.op(
        "OBJECT_OT_edit_poly_start", has_mesh ? IFACE_("Edit") : IFACE_("Start Editing"), ICON_EDITMODE_HLT);
    RNA_string_set(&op_ptr, "modifier", name.c_str());
    if (has_mesh) {
      op_ptr = row.op("OBJECT_OT_edit_poly_reset", IFACE_("Reset"), ICON_LOOP_BACK);
      RNA_string_set(&op_ptr, "modifier", name.c_str());
    }
  }

  layout.use_property_split_set(true);
  layout.prop(ptr, "use_follow_input", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  modifier_error_message_draw(layout, ptr);
}

static void panel_register(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_EditPoly, panel_draw);
}

static void blend_write(BlendWriter *writer, const ID * /*id_owner*/, const ModifierData *md)
{
  const EditPolyModifierData emd = *reinterpret_cast<const EditPolyModifierData *>(md);
  if (emd.input_positions) {
    BLO_write_float3_array(writer, emd.input_verts_num, emd.input_positions);
  }
  writer->write_struct_at_address(md, &emd);
}

static void blend_read(BlendDataReader *reader, ModifierData *md)
{
  EditPolyModifierData *emd = reinterpret_cast<EditPolyModifierData *>(md);
  if (emd->input_positions) {
    BLO_read_float3_array(reader, emd->input_verts_num, &emd->input_positions);
  }
}

ModifierTypeInfo modifierType_EditPoly = {
    /*idname*/ "EditPoly",
    /*name*/ N_("Edit Poly"),
    /*struct_name*/ "EditPolyModifierData",
    /*struct_size*/ sizeof(EditPolyModifierData),
    /*srna*/ &RNA_EditPolyModifier,
    /*type*/ ModifierTypeType::Constructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode,
    /*icon*/ ICON_MOD_EDIT_POLY,

    /*copy_data*/ copy_data,

    /*deform_verts*/ nullptr,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ modify_mesh,
    /*modify_geometry_set*/ nullptr,

    /*init_data*/ init_data,
    /*required_data_mask*/ nullptr,
    /*free_data*/ free_data,
    /*is_disabled*/ nullptr,
    /*update_depsgraph*/ update_depsgraph,
    /*depends_on_time*/ nullptr,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ foreach_ID_link,
    /*foreach_tex_link*/ nullptr,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ panel_register,
    /*blend_write*/ blend_write,
    /*blend_read*/ blend_read,
    /*foreach_cache*/ nullptr,
    /*foreach_working_space_color*/ nullptr,
};

}  // namespace blender
