/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Tension. The work is done by shared code in the geometry module, which the matching nodes use too.
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
#include "GEO_mesh_channels.hh"

namespace blender {

static void init_data_TensionModifierData(ModifierData *md)
{
  TensionModifierData *data = reinterpret_cast<TensionModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  const TensionModifierData &tmd = *reinterpret_cast<TensionModifierData *>(md);
  if (mesh->verts_num == 0 || tmd.attribute_name[0] == '\0') {
    return mesh;
  }
  /* The rest shape: the "rest_position" attribute, or else the mesh before any modifier. */
  Array<float3> rest_positions;
  if (const VArray<float3> rest = *mesh->attributes().lookup<float3>("rest_position",
                                                                     bke::AttrDomain::Point))
  {
    rest_positions.reinitialize(mesh->verts_num);
    rest.materialize(rest_positions);
  }
  else {
    const Object *ob_orig = DEG_get_original(ctx->object);
    const Mesh *orig_mesh = ob_orig ? reinterpret_cast<const Mesh *>(ob_orig->data) : nullptr;
    if (orig_mesh == nullptr || orig_mesh->verts_num != mesh->verts_num) {
      BKE_modifier_set_warning(
          ctx->object, md, "Needs a \"rest_position\" attribute or the same points as the mesh");
      return mesh;
    }
    rest_positions = orig_mesh->vert_positions();
  }
  Array<float> tension(mesh->verts_num);
  geometry::mesh_tension(*mesh, rest_positions, tmd.strength, tension);

  bke::MutableAttributeAccessor attributes = mesh->attributes_for_write();
  attributes.remove(tmd.attribute_name);
  attributes.add<float>(tmd.attribute_name,
                        bke::AttrDomain::Point,
                        bke::AttributeInitVArray(VArray<float>::from_container(std::move(tension))));
  return mesh;
}

static void panel_draw_Tension(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);
  UNUSED_VARS(ob_ptr);

  layout.use_property_split_set(true);
  layout.prop(ptr, "attribute_name", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "strength", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  modifier_error_message_draw(layout, ptr);
}

static void panel_register_Tension(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_Tension, panel_draw_Tension);
}

ModifierTypeInfo modifierType_Tension = {
    /*idname*/ "Tension",
    /*name*/ N_("Tension"),
    /*struct_name*/ "TensionModifierData",
    /*struct_size*/ sizeof(TensionModifierData),
    /*srna*/ &RNA_TensionModifier,
    /*type*/ ModifierTypeType::Nonconstructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode | eModifierTypeFlag_EnableInEditmode,
    /*icon*/ ICON_MOD_TENSION,

    /*copy_data*/ BKE_modifier_copydata_generic,

    /*deform_verts*/ nullptr,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ modify_mesh,
    /*modify_geometry_set*/ nullptr,

    /*init_data*/ init_data_TensionModifierData,
    /*required_data_mask*/ nullptr,
    /*free_data*/ nullptr,
    /*is_disabled*/ nullptr,
    /*update_depsgraph*/ nullptr,
    /*depends_on_time*/ nullptr,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ nullptr,
    /*foreach_tex_link*/ nullptr,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ panel_register_Tension,
    /*blend_write*/ nullptr,
    /*blend_read*/ nullptr,
    /*foreach_cache*/ nullptr,
    /*foreach_working_space_color*/ nullptr,
};

}  // namespace blender
