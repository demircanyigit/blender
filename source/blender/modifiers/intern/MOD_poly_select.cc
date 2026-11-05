/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Poly Select. The work is done by shared code in the geometry module, which the matching nodes use too.
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


namespace blender {

static void init_data_PolySelectModifierData(ModifierData *md)
{
  PolySelectModifierData *data = reinterpret_cast<PolySelectModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  const PolySelectModifierData &pmd = *reinterpret_cast<PolySelectModifierData *>(md);
  if (pmd.output_attribute[0] == '\0') {
    return mesh;
  }
  const bke::AttrDomain domain = pmd.domain == 0 ? bke::AttrDomain::Point :
                                 pmd.domain == 1 ? bke::AttrDomain::Edge :
                                                   bke::AttrDomain::Face;
  bke::MutableAttributeAccessor attributes = mesh->attributes_for_write();
  const int size = attributes.domain_size(domain);
  Array<bool> selection(size, false);
  if (pmd.stored_attribute[0] != '\0') {
    if (const VArray<bool> stored = *attributes.lookup<bool>(pmd.stored_attribute, domain)) {
      stored.materialize(selection);
    }
  }
  else {
    BKE_modifier_set_warning(ctx->object, md, "Nothing stored yet, use Store Selection");
  }
  if (pmd.flag & MOD_POLY_SELECT_INVERT) {
    for (bool &value : selection) {
      value = !value;
    }
  }
  attributes.remove(pmd.output_attribute);
  attributes.add<bool>(pmd.output_attribute,
                       domain,
                       bke::AttributeInitVArray(VArray<bool>::from_container(std::move(selection))));
  return mesh;
}

static void panel_draw_PolySelect(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);
  UNUSED_VARS(ob_ptr);

  layout.prop(ptr, "domain", ui::ITEM_R_EXPAND, std::nullopt, ICON_NONE);
  layout.op("OBJECT_OT_poly_select_store", IFACE_("Store Selection"), ICON_RESTRICT_SELECT_OFF);
  layout.use_property_split_set(true);
  layout.prop(ptr, "output_attribute", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "invert", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  modifier_error_message_draw(layout, ptr);
}

static void panel_register_PolySelect(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_PolySelect, panel_draw_PolySelect);
}

ModifierTypeInfo modifierType_PolySelect = {
    /*idname*/ "PolySelect",
    /*name*/ N_("Poly Select"),
    /*struct_name*/ "PolySelectModifierData",
    /*struct_size*/ sizeof(PolySelectModifierData),
    /*srna*/ &RNA_PolySelectModifier,
    /*type*/ ModifierTypeType::Nonconstructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode | eModifierTypeFlag_EnableInEditmode,
    /*icon*/ ICON_MOD_POLY_SELECT,

    /*copy_data*/ BKE_modifier_copydata_generic,

    /*deform_verts*/ nullptr,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ modify_mesh,
    /*modify_geometry_set*/ nullptr,

    /*init_data*/ init_data_PolySelectModifierData,
    /*required_data_mask*/ nullptr,
    /*free_data*/ nullptr,
    /*is_disabled*/ nullptr,
    /*update_depsgraph*/ nullptr,
    /*depends_on_time*/ nullptr,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ nullptr,
    /*foreach_tex_link*/ nullptr,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ panel_register_PolySelect,
    /*blend_write*/ nullptr,
    /*blend_read*/ nullptr,
    /*foreach_cache*/ nullptr,
    /*foreach_working_space_color*/ nullptr,
};

}  // namespace blender
