/* SPDX-FileCopyrightText: 2024 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup edinterface
 *
 * Template for building the panel layout for the active object's modifiers.
 */

#include "BKE_context.hh"
#include "BKE_modifier.hh"
#include "BKE_object.hh"
#include "BKE_screen.hh"

#include "BLI_listbase.h"
#include "BLI_string.h"
#include "BLI_utildefines.h"

#include "ED_object.hh"

#include "RNA_access.hh"
#include "RNA_prototypes.hh"

#include "UI_interface.hh"
#include "UI_interface_layout.hh"

namespace blender::ui {

static void modifier_panel_id(void *md_link, char *r_name)
{
  ModifierData *md = static_cast<ModifierData *>(md_link);
  BKE_modifier_type_panel_id(ModifierType(md->type), r_name);
}

/** Show only the active modifier's panel, used by the modifier list view. */
static void template_active_modifier(bContext *C, ARegion *region, Object *ob)
{
  ModifierData *md = BKE_object_active_modifier(ob);
  const ModifierTypeInfo *mti = md ? BKE_modifier_get_info(ModifierType(md->type)) : nullptr;
  if (mti && mti->panel_register == nullptr) {
    md = nullptr;
  }

  char panel_idname[MAX_NAME] = "";
  if (md) {
    modifier_panel_id(md, panel_idname);
  }

  int instanced_num = 0;
  Panel *instanced = nullptr;
  for (Panel &panel : region->panels) {
    if (panel.type && (panel.type->flag & PANEL_TYPE_INSTANCED)) {
      instanced_num++;
      instanced = &panel;
    }
  }

  if (md == nullptr) {
    if (instanced_num) {
      panels_free_instanced(C, region);
    }
    return;
  }

  PointerRNA *md_ptr = MEM_new<PointerRNA>(__func__);
  *md_ptr = RNA_pointer_create_id_subdata(ob->id, RNA_Modifier, md);

  if (instanced_num == 1 && STREQ(instanced->type->idname, panel_idname)) {
    const PointerRNA *old_ptr = panel_custom_data_get(instanced);
    if (old_ptr && old_ptr->data == md) {
      panel_custom_data_set(instanced, md_ptr);
      return;
    }
  }

  panels_free_instanced(C, region);
  panel_add_instanced(C, region, &region->panels, panel_idname, md_ptr);
}

void template_modifiers(Layout * /*layout*/, bContext *C, const bool active_only)
{
  ARegion *region = CTX_wm_region(C);

  Object *ob = ed::object::context_active_object(C);
  ListBaseT<ModifierData> *modifiers = &ob->modifiers;

  if (active_only) {
    template_active_modifier(C, region, ob);
    return;
  }

  const bool panels_match = panel_list_matches_data(region, modifiers, modifier_panel_id);

  if (!panels_match) {
    panels_free_instanced(C, region);
    for (ModifierData &md : *modifiers) {
      const ModifierTypeInfo *mti = BKE_modifier_get_info(ModifierType(md.type));
      if (mti->panel_register == nullptr) {
        continue;
      }

      char panel_idname[MAX_NAME];
      modifier_panel_id(&md, panel_idname);

      /* Create custom data RNA pointer. */
      PointerRNA *md_ptr = MEM_new<PointerRNA>(__func__);
      *md_ptr = RNA_pointer_create_id_subdata(ob->id, RNA_Modifier, &md);

      panel_add_instanced(C, region, &region->panels, panel_idname, md_ptr);
    }
  }
  else {
    /* Assuming there's only one group of instanced panels, update the custom data pointers. */
    Panel *panel = static_cast<Panel *>(region->panels.first);
    for (ModifierData &md : *modifiers) {
      const ModifierTypeInfo *mti = BKE_modifier_get_info(ModifierType(md.type));
      if (mti->panel_register == nullptr) {
        continue;
      }

      /* Move to the next instanced panel corresponding to the next modifier. */
      while ((panel->type == nullptr) || !(panel->type->flag & PANEL_TYPE_INSTANCED)) {
        panel = panel->next;
        BLI_assert(panel !=
                   nullptr); /* There shouldn't be fewer panels than modifiers with UIs. */
      }

      PointerRNA *md_ptr = MEM_new<PointerRNA>(__func__);
      *md_ptr = RNA_pointer_create_id_subdata(ob->id, RNA_Modifier, &md);
      panel_custom_data_set(panel, md_ptr);

      panel = panel->next;
    }
  }
}

}  // namespace blender::ui
