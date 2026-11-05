/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Face Extrude. The work is done by shared code in the geometry module, which the matching nodes use too.
 */

#include <algorithm>

#include "BLI_array.hh"
#include "BLI_math_euler.hh"
#include "BLI_math_matrix.hh"
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
#include "BLI_index_mask.hh"
#include "GEO_mesh_face_extrude.hh"
#include "GEO_randomize.hh"

namespace blender {

static void init_data_FaceExtrudeModifierData(ModifierData *md)
{
  FaceExtrudeModifierData *data = reinterpret_cast<FaceExtrudeModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

/**
 * The faces selected by \a name: a vertex group or attribute, read on faces, above 0.5.
 * All faces when the name is empty.
 */
static IndexMask selection_from_name(const Mesh &mesh,
                                     const char *name,
                                     const bke::AttrDomain domain,
                                     const float threshold,
                                     const bool invert,
                                     IndexMaskMemory &memory,
                                     bool &r_found)
{
  const int size = mesh.attributes().domain_size(domain);
  r_found = true;
  if (name[0] == '\0') {
    return invert ? IndexMask() : IndexMask(size);
  }
  const VArray<float> values = *mesh.attributes().lookup<float>(name, domain);
  if (!values) {
    r_found = false;
    return {};
  }
  return IndexMask::from_predicate(IndexRange(size), GrainSize(4096), memory, [&](const int i) {
    return (values[i] > threshold) != invert;
  });
}

static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  const FaceExtrudeModifierData &fmd = *reinterpret_cast<FaceExtrudeModifierData *>(md);
  if (mesh->faces_num == 0) {
    return mesh;
  }
  IndexMaskMemory memory;
  bool found;
  const IndexMask selection = selection_from_name(*mesh,
                                                  fmd.selection,
                                                  bke::AttrDomain::Face,
                                                  0.5f,
                                                  fmd.flag & MOD_FACE_EXTRUDE_INVERT,
                                                  memory,
                                                  found);
  if (!found) {
    BKE_modifier_set_warning(ctx->object, md, "No vertex group or attribute \"%s\"", fmd.selection);
    return mesh;
  }
  geometry::FaceExtrudeParams params;
  params.amount = fmd.amount;
  params.scale = fmd.scale;
  params.individual = fmd.flag & MOD_FACE_EXTRUDE_INDIVIDUAL;
  Mesh *result = geometry::mesh_face_extrude(*mesh, selection, params);
  if (result == nullptr) {
    return mesh;
  }
  geometry::debug_randomize_mesh_order(result);
  return result;
}

static void panel_draw_FaceExtrude(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);
  UNUSED_VARS(ob_ptr);

  layout.use_property_split_set(true);
  layout.prop(ptr, "amount", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "scale", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "use_individual", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "selection", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "invert", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  modifier_error_message_draw(layout, ptr);
}

static void panel_register_FaceExtrude(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_FaceExtrude, panel_draw_FaceExtrude);
}

ModifierTypeInfo modifierType_FaceExtrude = {
    /*idname*/ "FaceExtrude",
    /*name*/ N_("Face Extrude"),
    /*struct_name*/ "FaceExtrudeModifierData",
    /*struct_size*/ sizeof(FaceExtrudeModifierData),
    /*srna*/ &RNA_FaceExtrudeModifier,
    /*type*/ ModifierTypeType::Constructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode | eModifierTypeFlag_EnableInEditmode,
    /*icon*/ ICON_MOD_FACE_EXTRUDE,

    /*copy_data*/ BKE_modifier_copydata_generic,

    /*deform_verts*/ nullptr,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ modify_mesh,
    /*modify_geometry_set*/ nullptr,

    /*init_data*/ init_data_FaceExtrudeModifierData,
    /*required_data_mask*/ nullptr,
    /*free_data*/ nullptr,
    /*is_disabled*/ nullptr,
    /*update_depsgraph*/ nullptr,
    /*depends_on_time*/ nullptr,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ nullptr,
    /*foreach_tex_link*/ nullptr,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ panel_register_FaceExtrude,
    /*blend_write*/ nullptr,
    /*blend_read*/ nullptr,
    /*foreach_cache*/ nullptr,
    /*foreach_working_space_color*/ nullptr,
};

}  // namespace blender
