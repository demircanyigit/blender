/* SPDX-FileCopyrightText: 2026 Blender Authors
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "BKE_modifier.hh"
#include "BLI_utildefines.h"
#include "BLT_translation.hh"
#include "DNA_mesh_types.h"
#include "DNA_modifier_types.h"
#include "DNA_screen_types.h"
#include "GEO_mesh_blast.hh"
#include "MOD_ui_common.hh"
#include "MOD_util.hh"
#include "RNA_access.hh"
#include "RNA_prototypes.hh"
#include "UI_interface_layout.hh"
#include "UI_resources.hh"

namespace blender {
static void init_data(ModifierData *md)
{
  auto *data = reinterpret_cast<BlastModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}
static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  const auto &data = *reinterpret_cast<BlastModifierData *>(md);
  geometry::MeshBlastParams params;
  params.metric = geometry::BlastMetric(data.metric);
  params.filter = geometry::BlastFilter(data.filter);
  params.keep_count = data.keep_count;
  params.invert = data.flag & MOD_BLAST_INVERT;
  switch (data.metric) {
    case MOD_BLAST_FACE_COUNT:
      params.threshold = data.minimum_faces;
      break;
    case MOD_BLAST_AREA:
      params.threshold = data.minimum_area;
      break;
    case MOD_BLAST_VOLUME:
      params.threshold = data.minimum_volume;
      break;
    case MOD_BLAST_BOUNDS_SIZE:
      params.threshold = data.minimum_size;
      break;
  }
  std::string message;
  Mesh *result = geometry::mesh_blast(*mesh, params, message);
  if (!message.empty())
    BKE_modifier_set_warning(ctx->object, md, "%s", message.c_str());
  return result ? result : mesh;
}
static void panel_draw(const bContext * /*C*/, Panel *panel)
{
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, nullptr);
  ui::Layout &layout = *panel->layout;
  layout.use_property_split_set(true);
  layout.prop(ptr, "metric", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "filter", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  const int filter = RNA_enum_get(ptr, "filter");
  const int metric = RNA_enum_get(ptr, "metric");
  if (filter != MOD_BLAST_LARGEST) {
    const char *threshold = metric == MOD_BLAST_FACE_COUNT ? "minimum_faces" :
                            metric == MOD_BLAST_AREA       ? "minimum_area" :
                            metric == MOD_BLAST_VOLUME     ? "minimum_volume" :
                                                             "minimum_size";
    layout.prop(ptr, threshold, UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  if (filter != MOD_BLAST_THRESHOLD) {
    layout.prop(ptr, "keep_count", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  layout.prop(ptr, "invert", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  if (metric == MOD_BLAST_VOLUME)
    layout.label(IFACE_("Open islands are preserved"), ICON_INFO);
  modifier_error_message_draw(layout, ptr);
}
static void panel_register(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_Blast, panel_draw);
}

ModifierTypeInfo modifierType_Blast = {
    /*idname*/ "Blast",
    /*name*/ N_("Blast"),
    /*struct_name*/ "BlastModifierData",
    /*struct_size*/ sizeof(BlastModifierData),
    /*srna*/ &RNA_BlastModifier,
    /*type*/ ModifierTypeType::Nonconstructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode,
    /*icon*/ ICON_MOD_BLAST,

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
