/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup spshelf
 *
 * The Shelf: a compact editor of operator icons the user picked, added with "Add to Shelf" in
 * the context menu of any button. The items are stored in the preferences, see
 * #ED_screen_shelf_ensure, so every Shelf shows the same items.
 */

#include <algorithm>
#include <array>
#include <cctype>
#include <climits>
#include <cstring>
#include <string>

#include <fmt/format.h>

#include "MEM_guardedalloc.h"

#include "BLI_listbase.h"
#include "BLI_map.hh"
#include "BLI_path_utils.hh"
#include "BLI_serialize.hh"
#include "BLI_vector.hh"
#include "BLI_string.h"
#include "BLI_string_utf8.h"

#include "BLT_translation.hh"

#include "DNA_screen_types.h"
#include "DNA_space_types.h"
#include "DNA_userdef_types.h"

#include "BKE_blender_user_menu.hh"
#include "BKE_context.hh"
#include "BKE_idprop.hh"
#include "BKE_report.hh"
#include "BKE_screen.hh"

#include "ED_screen.hh"
#include "ED_space_api.hh"

#include "RNA_access.hh"
#include "RNA_define.hh"
#include "RNA_enum_types.hh"
#include "RNA_path.hh"
#include "RNA_prototypes.hh"

#include "UI_interface_c.hh"
#include "UI_interface_icons.hh"
#include "UI_interface_layout.hh"
#include "UI_resources.hh"
#include "UI_view2d.hh"

#include "GPU_immediate.hh"
#include "GPU_immediate_util.hh"
#include "GPU_state.hh"

#include "WM_api.hh"
#include "WM_types.hh"

#include "BLO_read_write.hh"

#ifdef WITH_PYTHON
#  include "BPY_extern_run.hh"
#endif

namespace blender {

/* -------------------------------------------------------------------- */
/** \name Shelf Items
 * \{ */

static bUserMenuItem *shelf_item_find(const int index)
{
  bUserMenu *um = ED_screen_shelf_find();
  if (um == nullptr || index < 0) {
    return nullptr;
  }
  return static_cast<bUserMenuItem *>(BLI_findlink(&um->items, index));
}

/**
 * The editor the item was added from, so the item runs there. Operators of the 3D Viewport need
 * a 3D Viewport to run in, and the Shelf is not one.
 */
static ScrArea *shelf_item_area(const bContext *C, const bUserMenuItem &umi)
{
  if (ELEM(umi.space_type, SPACE_EMPTY, SPACE_SHELF)) {
    return nullptr;
  }
  /* The shelf strip of a 3D Viewport runs 3D Viewport items in that viewport. */
  ScrArea *area = CTX_wm_area(C);
  if (area && area->spacetype == umi.space_type) {
    return area;
  }
  const bScreen *screen = CTX_wm_screen(C);
  return screen ? BKE_screen_find_big_area(screen, umi.space_type, 0) : nullptr;
}

/** Makes the item's editor the context, and puts the Shelf back when it goes out of scope. */
class ShelfItemContext {
  bContext *C_;
  ScrArea *area_prev_;
  ARegion *region_prev_;

 public:
  ShelfItemContext(bContext *C, ScrArea *area)
      : C_(C), area_prev_(CTX_wm_area(C)), region_prev_(CTX_wm_region(C))
  {
    if (area) {
      CTX_wm_area_set(C, area);
      CTX_wm_region_set(C, BKE_area_find_region_type(area, RGN_TYPE_WINDOW));
    }
  }

  ~ShelfItemContext()
  {
    CTX_wm_area_set(C_, area_prev_);
    CTX_wm_region_set(C_, region_prev_);
  }
};

static std::string shelf_item_name(const bUserMenuItem &umi)
{
  if (umi.ui_name[0]) {
    return umi.ui_name;
  }
  if (umi.type == USER_MENU_TYPE_OPERATOR) {
    const bUserMenuItem_Op &umi_op = reinterpret_cast<const bUserMenuItem_Op &>(umi);
    if (wmOperatorType *ot = WM_operatortype_find(umi_op.op_idname, true)) {
      return WM_operatortype_name(ot, nullptr);
    }
    return umi_op.op_idname;
  }
  if (umi.type == USER_MENU_TYPE_PROP) {
    return reinterpret_cast<const bUserMenuItem_Prop &>(umi).prop_id;
  }
  return "";
}

/** Short text for items without an icon: the initials of the first two words. */
static std::string shelf_item_abbreviation(const StringRef name)
{
  std::string result;
  bool word_start = true;
  for (const char c : name) {
    if (!std::isalnum((unsigned char)c)) {
      word_start = true;
      continue;
    }
    if (word_start && result.size() < 2) {
      result += char(std::toupper((unsigned char)c));
    }
    else if (result.size() == 1 && name.find(' ') == StringRef::not_found) {
      /* A single word gets its second letter too. */
      result += c;
    }
    word_start = false;
  }
  return result.empty() ? "?" : result;
}

/**
 * The icon is either the name of a built-in icon, or the icon of a tool, which the tool system
 * loads from a file.
 */
int ED_screen_user_menu_item_icon(const bContext *C, const bUserMenuItem &umi)
{
  if (umi.icon_name[0] == '\0') {
    return ICON_NONE;
  }
  int icon = ICON_NONE;
  if (RNA_enum_value_from_id(rna_enum_icon_items, umi.icon_name, &icon)) {
    return icon;
  }
  static Map<std::string, int> tool_icons;
  return tool_icons.lookup_or_add_cb(umi.icon_name, [&]() {
    intptr_t value = ICON_NONE;
#ifdef WITH_PYTHON
    if (strchr(umi.icon_name, '\'') == nullptr) {
      const char *imports[] = {"bl_ui", nullptr};
      char expr[256];
      SNPRINTF(expr,
               "bl_ui.space_toolsystem_common.ToolSelectPanelHelper."
               "_icon_value_from_icon_handle('%s')",
               umi.icon_name);
      if (!BPY_run_string_as_intptr(const_cast<bContext *>(C), imports, expr, nullptr, &value)) {
        value = ICON_NONE;
      }
    }
#else
    UNUSED_VARS(C);
#endif
    return int(value);
  });
}

static void shelf_items_changed(bContext *C)
{
  U.runtime.is_dirty = true;
  WM_event_add_notifier(C, NC_SPACE | ND_SPACE_SHELF, nullptr);
}

static void shelf_tab_changed_cb(bContext *C, void * /*arg1*/, void * /*arg2*/)
{
  shelf_items_changed(C);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Operators
 * \{ */

static wmOperatorStatus shelf_run_item_invoke(bContext *C,
                                              wmOperator *op,
                                              const wmEvent * /*event*/)
{
  bUserMenuItem *umi = shelf_item_find(RNA_int_get(op->ptr, "index"));
  if (umi == nullptr || umi->type != USER_MENU_TYPE_OPERATOR) {
    return OPERATOR_CANCELLED;
  }
  bUserMenuItem_Op *umi_op = reinterpret_cast<bUserMenuItem_Op *>(umi);
  wmOperatorType *ot = WM_operatortype_find(umi_op->op_idname, true);
  if (ot == nullptr) {
    BKE_reportf(op->reports, RPT_ERROR, "Operator \"%s\" not found", umi_op->op_idname);
    return OPERATOR_CANCELLED;
  }

  const std::string name = shelf_item_name(*umi);
  ShelfItemContext item_context(C, shelf_item_area(C, *umi));
  if (!WM_operator_poll(C, ot)) {
    BKE_reportf(op->reports, RPT_WARNING, "\"%s\" can't be used right now", name.c_str());
    return OPERATOR_CANCELLED;
  }

  PointerRNA props = WM_operator_properties_create_ptr(ot);
  if (umi_op->prop) {
    props.data = IDP_CopyProperty(umi_op->prop);
  }
  /* Operators that need to know where the mouse is (like Knife) wait for a click in an editor
   * instead of using the mouse position over the Shelf. */
  WM_operator_name_call_ptr_with_depends_on_cursor(
      C, ot, wm::OpCallContext(umi_op->opcontext), &props, nullptr, name);
  WM_operator_properties_free(&props);
  return OPERATOR_FINISHED;
}

static std::string shelf_run_item_get_name(wmOperatorType *ot, PointerRNA *ptr)
{
  if (const bUserMenuItem *umi = shelf_item_find(RNA_int_get(ptr, "index"))) {
    return shelf_item_name(*umi);
  }
  return CTX_IFACE_(ot->translation_context, ot->name);
}

static std::string shelf_run_item_get_description(bContext *C,
                                                  wmOperatorType * /*ot*/,
                                                  PointerRNA *ptr)
{
  const bUserMenuItem *umi = shelf_item_find(RNA_int_get(ptr, "index"));
  if (umi == nullptr || umi->type != USER_MENU_TYPE_OPERATOR) {
    return "";
  }
  const bUserMenuItem_Op *umi_op = reinterpret_cast<const bUserMenuItem_Op *>(umi);
  wmOperatorType *ot = WM_operatortype_find(umi_op->op_idname, true);
  if (ot == nullptr) {
    return "";
  }
  PointerRNA props = WM_operator_properties_create_ptr(ot);
  props.data = umi_op->prop;
  return WM_operatortype_description(C, ot, &props);
}

static void SHELF_OT_run_item(wmOperatorType *ot)
{
  ot->name = "Run Shelf Item";
  ot->idname = "SHELF_OT_run_item";
  ot->description = "Run this operator in the editor it was added from";

  ot->invoke = shelf_run_item_invoke;
  ot->get_name = shelf_run_item_get_name;
  ot->get_description = shelf_run_item_get_description;

  /* The operator it runs registers itself and pushes its own undo step. */
  ot->flag = OPTYPE_INTERNAL;

  PropertyRNA *prop = RNA_def_int(
      ot->srna, "index", 0, 0, INT_MAX, "Index", "Index of the item on the shelf", 0, INT_MAX);
  RNA_def_property_flag(prop, PROP_SKIP_SAVE | PROP_HIDDEN);
}

static wmOperatorStatus shelf_remove_item_exec(bContext *C, wmOperator *op)
{
  bUserMenu *um = ED_screen_shelf_find();
  bUserMenuItem *umi = shelf_item_find(RNA_int_get(op->ptr, "index"));
  if (umi == nullptr) {
    return OPERATOR_CANCELLED;
  }
  ED_screen_user_menu_item_remove(&um->items, umi);
  shelf_items_changed(C);
  return OPERATOR_FINISHED;
}

static void SHELF_OT_remove_item(wmOperatorType *ot)
{
  ot->name = "Remove from Shelf";
  ot->idname = "SHELF_OT_remove_item";
  ot->description = "Remove this button from the shelf";

  ot->exec = shelf_remove_item_exec;

  ot->flag = OPTYPE_INTERNAL;

  PropertyRNA *prop = RNA_def_int(
      ot->srna, "index", 0, 0, INT_MAX, "Index", "Index of the item on the shelf", 0, INT_MAX);
  RNA_def_property_flag(prop, PROP_SKIP_SAVE | PROP_HIDDEN);
}

static wmOperatorStatus shelf_move_item_exec(bContext *C, wmOperator *op)
{
  bUserMenu *um = ED_screen_shelf_find();
  bUserMenuItem *umi = shelf_item_find(RNA_int_get(op->ptr, "index"));
  if (umi == nullptr) {
    return OPERATOR_CANCELLED;
  }
  if (!BLI_listbase_link_move(&um->items, umi, RNA_int_get(op->ptr, "offset"))) {
    return OPERATOR_CANCELLED;
  }
  shelf_items_changed(C);
  return OPERATOR_FINISHED;
}

static void SHELF_OT_move_item(wmOperatorType *ot)
{
  ot->name = "Move Shelf Item";
  ot->idname = "SHELF_OT_move_item";
  ot->description = "Move this button along the shelf";

  ot->exec = shelf_move_item_exec;

  ot->flag = OPTYPE_INTERNAL;

  PropertyRNA *prop = RNA_def_int(
      ot->srna, "index", 0, 0, INT_MAX, "Index", "Index of the item on the shelf", 0, INT_MAX);
  RNA_def_property_flag(prop, PROP_SKIP_SAVE | PROP_HIDDEN);
  prop = RNA_def_int(ot->srna,
                     "offset",
                     1,
                     INT_MIN,
                     INT_MAX,
                     "Offset",
                     "How many places to move the button, negative moves it to the left",
                     -1,
                     1);
  RNA_def_property_flag(prop, PROP_SKIP_SAVE);
}

static wmOperatorStatus shelf_toggle_item_text_exec(bContext *C, wmOperator *op)
{
  bUserMenuItem *umi = shelf_item_find(RNA_int_get(op->ptr, "index"));
  if (umi == nullptr) {
    return OPERATOR_CANCELLED;
  }
  umi->shelf_flag ^= USER_MENU_ITEM_SHELF_SHOW_TEXT;
  shelf_items_changed(C);
  return OPERATOR_FINISHED;
}

static void SHELF_OT_toggle_item_text(wmOperatorType *ot)
{
  ot->name = "Show Text";
  ot->idname = "SHELF_OT_toggle_item_text";
  ot->description = "Show the name of this button to the right of its icon";

  ot->exec = shelf_toggle_item_text_exec;

  ot->flag = OPTYPE_INTERNAL;

  PropertyRNA *prop = RNA_def_int(
      ot->srna, "index", 0, 0, INT_MAX, "Index", "Index of the item on the shelf", 0, INT_MAX);
  RNA_def_property_flag(prop, PROP_SKIP_SAVE | PROP_HIDDEN);
}

static wmOperatorStatus shelf_toggle_item_icon_exec(bContext *C, wmOperator *op)
{
  bUserMenuItem *umi = shelf_item_find(RNA_int_get(op->ptr, "index"));
  if (umi == nullptr) {
    return OPERATOR_CANCELLED;
  }
  umi->shelf_flag ^= USER_MENU_ITEM_SHELF_HIDE_ICON;
  shelf_items_changed(C);
  return OPERATOR_FINISHED;
}

static void SHELF_OT_toggle_item_icon(wmOperatorType *ot)
{
  ot->name = "Show Icon";
  ot->idname = "SHELF_OT_toggle_item_icon";
  ot->description = "Show this button's icon, without it the button shows its name instead";

  ot->exec = shelf_toggle_item_icon_exec;

  ot->flag = OPTYPE_INTERNAL;

  PropertyRNA *prop = RNA_def_int(
      ot->srna, "index", 0, 0, INT_MAX, "Index", "Index of the item on the shelf", 0, INT_MAX);
  RNA_def_property_flag(prop, PROP_SKIP_SAVE | PROP_HIDDEN);
}

static wmOperatorStatus shelf_set_item_icon_exec(bContext *C, wmOperator *op)
{
  bUserMenuItem *umi = shelf_item_find(RNA_int_get(op->ptr, "index"));
  const char *icon_id = nullptr;
  if (umi == nullptr ||
      !RNA_enum_identifier(rna_enum_icon_items, RNA_enum_get(op->ptr, "icon"), &icon_id))
  {
    return OPERATOR_CANCELLED;
  }
  STRNCPY(umi->icon_name, icon_id);
  shelf_items_changed(C);
  return OPERATOR_FINISHED;
}

static void SHELF_OT_set_item_icon(wmOperatorType *ot)
{
  ot->name = "Change Icon";
  ot->idname = "SHELF_OT_set_item_icon";
  ot->description = "Pick another icon for this button, None shows its initials instead";

  ot->invoke = WM_enum_search_invoke;
  ot->exec = shelf_set_item_icon_exec;

  ot->flag = OPTYPE_INTERNAL;

  PropertyRNA *prop = RNA_def_int(
      ot->srna, "index", 0, 0, INT_MAX, "Index", "Index of the item on the shelf", 0, INT_MAX);
  RNA_def_property_flag(prop, PROP_SKIP_SAVE | PROP_HIDDEN);
  prop = RNA_def_enum(ot->srna, "icon", rna_enum_icon_items, ICON_NONE, "Icon", "");
  RNA_def_property_flag(prop, PROP_SKIP_SAVE);
  ot->prop = prop;
}

static wmOperatorStatus shelf_rename_item_invoke(bContext *C,
                                                 wmOperator *op,
                                                 const wmEvent * /*event*/)
{
  const bUserMenuItem *umi = shelf_item_find(RNA_int_get(op->ptr, "index"));
  if (umi == nullptr) {
    return OPERATOR_CANCELLED;
  }
  RNA_string_set(op->ptr, "name", shelf_item_name(*umi).c_str());
  return WM_operator_props_dialog_popup(
      C, op, 10 * UI_UNIT_X, IFACE_("Rename Shelf Item"), IFACE_("Rename"));
}

static wmOperatorStatus shelf_rename_item_exec(bContext *C, wmOperator *op)
{
  bUserMenuItem *umi = shelf_item_find(RNA_int_get(op->ptr, "index"));
  if (umi == nullptr) {
    return OPERATOR_CANCELLED;
  }
  /* An empty name goes back to the operator's own name. */
  RNA_string_get(op->ptr, "name", umi->ui_name);
  shelf_items_changed(C);
  return OPERATOR_FINISHED;
}

static void SHELF_OT_rename_item(wmOperatorType *ot)
{
  ot->name = "Rename";
  ot->idname = "SHELF_OT_rename_item";
  ot->description = "Change the name this button shows as its text and tooltip";

  ot->invoke = shelf_rename_item_invoke;
  ot->exec = shelf_rename_item_exec;

  ot->flag = OPTYPE_INTERNAL;

  PropertyRNA *prop = RNA_def_int(
      ot->srna, "index", 0, 0, INT_MAX, "Index", "Index of the item on the shelf", 0, INT_MAX);
  RNA_def_property_flag(prop, PROP_SKIP_SAVE | PROP_HIDDEN);
  prop = RNA_def_string(ot->srna,
                        "name",
                        nullptr,
                        sizeof(bUserMenuItem::ui_name),
                        "Name",
                        "Leave empty to use the operator's own name");
  RNA_def_property_flag(prop, PROP_SKIP_SAVE);
}

static wmOperatorStatus shelf_tab_remove_exec(bContext *C, wmOperator *op);

/* Separators. */

static wmOperatorStatus shelf_add_separator_exec(bContext *C, wmOperator *op)
{
  bUserMenu *um = ED_screen_shelf_ensure();
  bUserMenuItem *after = shelf_item_find(RNA_int_get(op->ptr, "index"));
  bUserMenuItem *sep = BKE_blender_user_menu_item_add(&um->items, USER_MENU_TYPE_SEP);
  if (after) {
    BLI_remlink(&um->items, sep);
    BLI_insertlinkafter(&um->items, after, sep);
  }
  shelf_items_changed(C);
  return OPERATOR_FINISHED;
}

static void SHELF_OT_add_separator(wmOperatorType *ot)
{
  ot->name = "Add Separator";
  ot->idname = "SHELF_OT_add_separator";
  ot->description = "Add a line between the buttons of the shelf, to group them";

  ot->exec = shelf_add_separator_exec;

  ot->flag = OPTYPE_INTERNAL;

  PropertyRNA *prop = RNA_def_int(ot->srna,
                                  "index",
                                  -1,
                                  -1,
                                  INT_MAX,
                                  "Index",
                                  "The separator goes after this button, -1 adds it at the end",
                                  -1,
                                  INT_MAX);
  RNA_def_property_flag(prop, PROP_SKIP_SAVE | PROP_HIDDEN);
}

/* Tabs. */

static void shelf_tab_index_prop(wmOperatorType *ot)
{
  PropertyRNA *prop = RNA_def_int(
      ot->srna, "tab", 0, 0, INT_MAX, "Tab", "Index of the tab", 0, INT_MAX);
  RNA_def_property_flag(prop, PROP_SKIP_SAVE | PROP_HIDDEN);
}

static void shelf_tab_name_prop(wmOperatorType *ot)
{
  PropertyRNA *prop = RNA_def_string(
      ot->srna, "name", nullptr, sizeof(bUserMenu::context) - 7, "Name", "");
  RNA_def_property_flag(prop, PROP_SKIP_SAVE);
}

static wmOperatorStatus shelf_tab_add_invoke(bContext *C,
                                             wmOperator *op,
                                             const wmEvent * /*event*/)
{
  if (!RNA_struct_property_is_set(op->ptr, "name")) {
    const std::string name = fmt::format("{} {}", IFACE_("Tab"), ED_screen_shelf_tabs_num() + 1);
    RNA_string_set(op->ptr, "name", name.c_str());
  }
  return WM_operator_props_dialog_popup(
      C, op, 10 * UI_UNIT_X, IFACE_("Add Shelf Tab"), IFACE_("Add"));
}

static wmOperatorStatus shelf_tab_add_exec(bContext *C, wmOperator *op)
{
  /* The buttons that are on the shelf already stay in the first tab. */
  ED_screen_shelf_ensure();
  char name[sizeof(bUserMenu::context)];
  RNA_string_get(op->ptr, "name", name);
  ED_screen_shelf_tab_add(name);
  U.shelf_active_tab = char(std::min(ED_screen_shelf_tabs_num() - 1, 255));
  shelf_items_changed(C);
  return OPERATOR_FINISHED;
}

static void SHELF_OT_tab_add(wmOperatorType *ot)
{
  ot->name = "Add Tab";
  ot->idname = "SHELF_OT_tab_add";
  ot->description = "Add a tab to the shelf, each tab has buttons of its own";

  ot->invoke = shelf_tab_add_invoke;
  ot->exec = shelf_tab_add_exec;

  ot->flag = OPTYPE_INTERNAL;

  shelf_tab_name_prop(ot);
}

static bool shelf_tab_remove_poll(bContext *C)
{
  if (ED_screen_shelf_tabs_num() < 2) {
    CTX_wm_operator_poll_msg_set(C, "The shelf needs at least one tab");
    return false;
  }
  return true;
}

static wmOperatorStatus shelf_tab_remove_invoke(bContext *C,
                                                wmOperator *op,
                                                const wmEvent * /*event*/)
{
  const bUserMenu *um = ED_screen_shelf_tab_get(RNA_int_get(op->ptr, "tab"));
  if (um == nullptr) {
    return OPERATOR_CANCELLED;
  }
  if (BLI_listbase_is_empty(&um->items)) {
    return shelf_tab_remove_exec(C, op);
  }
  return WM_operator_confirm_ex(C,
                                op,
                                IFACE_("Remove the tab and its buttons?"),
                                nullptr,
                                IFACE_("Remove"),
                                ui::AlertIcon::Warning,
                                false);
}

static wmOperatorStatus shelf_tab_remove_exec(bContext *C, wmOperator *op)
{
  const int index = RNA_int_get(op->ptr, "tab");
  bUserMenu *um = ED_screen_shelf_tab_get(index);
  if (um == nullptr || ED_screen_shelf_tabs_num() < 2) {
    return OPERATOR_CANCELLED;
  }
  const int active = ED_screen_shelf_active_tab_index();
  ED_screen_shelf_tab_remove(um);
  if (index < active || active >= ED_screen_shelf_tabs_num()) {
    U.shelf_active_tab = char(std::max(active - 1, 0));
  }
  shelf_items_changed(C);
  return OPERATOR_FINISHED;
}

static void SHELF_OT_tab_remove(wmOperatorType *ot)
{
  ot->name = "Remove Tab";
  ot->idname = "SHELF_OT_tab_remove";
  ot->description = "Remove this tab and the buttons on it";

  ot->invoke = shelf_tab_remove_invoke;
  ot->exec = shelf_tab_remove_exec;
  ot->poll = shelf_tab_remove_poll;

  ot->flag = OPTYPE_INTERNAL;

  shelf_tab_index_prop(ot);
}

static wmOperatorStatus shelf_tab_rename_invoke(bContext *C,
                                                wmOperator *op,
                                                const wmEvent * /*event*/)
{
  const bUserMenu *um = ED_screen_shelf_tab_get(RNA_int_get(op->ptr, "tab"));
  if (um == nullptr) {
    return OPERATOR_CANCELLED;
  }
  RNA_string_set(op->ptr, "name", ED_screen_shelf_tab_name(um).c_str());
  return WM_operator_props_dialog_popup(
      C, op, 10 * UI_UNIT_X, IFACE_("Rename Shelf Tab"), IFACE_("Rename"));
}

static wmOperatorStatus shelf_tab_rename_exec(bContext *C, wmOperator *op)
{
  bUserMenu *um = ED_screen_shelf_tab_get(RNA_int_get(op->ptr, "tab"));
  if (um == nullptr) {
    return OPERATOR_CANCELLED;
  }
  char name[sizeof(bUserMenu::context)];
  RNA_string_get(op->ptr, "name", name);
  ED_screen_shelf_tab_name_set(um, name);
  shelf_items_changed(C);
  return OPERATOR_FINISHED;
}

static void SHELF_OT_tab_rename(wmOperatorType *ot)
{
  ot->name = "Rename Tab";
  ot->idname = "SHELF_OT_tab_rename";
  ot->description = "Change the name of this tab";

  ot->invoke = shelf_tab_rename_invoke;
  ot->exec = shelf_tab_rename_exec;

  ot->flag = OPTYPE_INTERNAL;

  shelf_tab_index_prop(ot);
  shelf_tab_name_prop(ot);
}

static wmOperatorStatus shelf_tab_move_exec(bContext *C, wmOperator *op)
{
  const int index = RNA_int_get(op->ptr, "tab");
  const int offset = RNA_int_get(op->ptr, "offset") < 0 ? -1 : 1;
  bUserMenu *um = ED_screen_shelf_tab_get(index);
  if (um == nullptr || !ED_screen_shelf_tab_move(um, offset)) {
    return OPERATOR_CANCELLED;
  }
  /* The active tab stays the same tab. */
  const int active = ED_screen_shelf_active_tab_index();
  if (active == index) {
    U.shelf_active_tab = char(index + offset);
  }
  else if (active == index + offset) {
    U.shelf_active_tab = char(index);
  }
  shelf_items_changed(C);
  return OPERATOR_FINISHED;
}

static void SHELF_OT_tab_move(wmOperatorType *ot)
{
  ot->name = "Move Tab";
  ot->idname = "SHELF_OT_tab_move";
  ot->description = "Move this tab along the shelf";

  ot->exec = shelf_tab_move_exec;

  ot->flag = OPTYPE_INTERNAL;

  shelf_tab_index_prop(ot);
  PropertyRNA *prop = RNA_def_int(ot->srna,
                                  "offset",
                                  1,
                                  -1,
                                  1,
                                  "Offset",
                                  "Move the tab to the left (-1) or to the right (1)",
                                  -1,
                                  1);
  RNA_def_property_flag(prop, PROP_SKIP_SAVE);
}

/* Saving and loading, as a JSON file of the items. */

static void shelf_file_props(wmOperatorType *ot, const eFileSel_Action action)
{
  WM_operator_properties_filesel(ot,
                                 FILE_TYPE_FOLDER,
                                 FILE_SPECIAL,
                                 action,
                                 WM_FILESEL_FILEPATH,
                                 FILE_DEFAULTDISPLAY,
                                 FILE_SORT_DEFAULT);
  PropertyRNA *prop = RNA_def_string(ot->srna, "filter_glob", "*.json", 0, "", "");
  RNA_def_property_flag(prop, PROP_HIDDEN);
}

static wmOperatorStatus shelf_file_invoke(bContext *C, wmOperator *op, const wmEvent * /*event*/)
{
  if (!RNA_struct_property_is_set(op->ptr, "filepath")) {
    RNA_string_set(op->ptr, "filepath", "shelf.json");
  }
  WM_event_add_fileselect(C, op);
  return OPERATOR_RUNNING_MODAL;
}

static wmOperatorStatus shelf_save_exec(bContext * /*C*/, wmOperator *op)
{
  using namespace io::serialize;
  char filepath[FILE_MAX];
  RNA_string_get(op->ptr, "filepath", filepath);
  BLI_path_extension_ensure(filepath, sizeof(filepath), ".json");

  DictionaryValue root;
  root.append_int("blender_shelf", 1);
  ArrayValue &items = *root.append_array("items");
  int items_num = 0;
  if (const bUserMenu *um = ED_screen_shelf_find()) {
    for (const bUserMenuItem &umi : um->items) {
      if (umi.type == USER_MENU_TYPE_SEP) {
        items.append_dict()->append_str("type", "SEPARATOR");
        items_num++;
        continue;
      }
      if (!ELEM(umi.type, USER_MENU_TYPE_OPERATOR, USER_MENU_TYPE_PROP)) {
        continue;
      }
      DictionaryValue &item = *items.append_dict();
      item.append_str("name", umi.ui_name);
      item.append_str("icon", umi.icon_name);
      const char *editor = "";
      RNA_enum_identifier(rna_enum_space_type_items, umi.space_type, &editor);
      item.append_str("editor", editor);
      item.append_int("show_text", (umi.shelf_flag & USER_MENU_ITEM_SHELF_SHOW_TEXT) ? 1 : 0);
      item.append_int("show_icon", (umi.shelf_flag & USER_MENU_ITEM_SHELF_HIDE_ICON) ? 0 : 1);
      if (umi.type == USER_MENU_TYPE_OPERATOR) {
        const bUserMenuItem_Op &umi_op = reinterpret_cast<const bUserMenuItem_Op &>(umi);
        item.append_str("type", "OPERATOR");
        item.append_str("operator", umi_op.op_idname);
        item.append_str("enum_property", umi_op.op_prop_enum);
        item.append_int("call_context", umi_op.opcontext);
        if (umi_op.prop) {
          item.append("properties", bke::idprop::convert_to_serialize_values(umi_op.prop));
        }
      }
      else {
        const bUserMenuItem_Prop &umi_pr = reinterpret_cast<const bUserMenuItem_Prop &>(umi);
        item.append_str("type", "PROPERTY");
        item.append_str("data_path", umi_pr.context_data_path);
        item.append_str("property", umi_pr.prop_id);
        item.append_int("index", umi_pr.prop_index);
      }
      items_num++;
    }
  }
  write_json_file(filepath, root);
  BKE_reportf(op->reports, RPT_INFO, "Saved %d shelf buttons to \"%s\"", items_num, filepath);
  return OPERATOR_FINISHED;
}

static void SHELF_OT_save(wmOperatorType *ot)
{
  ot->name = "Save Shelf";
  ot->idname = "SHELF_OT_save";
  ot->description = "Save the buttons of the shelf to a file, to load them later or elsewhere";

  ot->invoke = shelf_file_invoke;
  ot->exec = shelf_save_exec;

  shelf_file_props(ot, FILE_SAVE);
}

static wmOperatorStatus shelf_load_exec(bContext *C, wmOperator *op)
{
  using namespace io::serialize;
  char filepath[FILE_MAX];
  RNA_string_get(op->ptr, "filepath", filepath);

  const std::shared_ptr<Value> file = read_json_file(filepath);
  const DictionaryValue *root = file ? file->as_dictionary_value() : nullptr;
  const ArrayValue *items = root ? root->lookup_array("items") : nullptr;
  if (items == nullptr || !root->lookup_int("blender_shelf").has_value()) {
    BKE_reportf(op->reports, RPT_ERROR, "\"%s\" is not a saved shelf", filepath);
    return OPERATOR_CANCELLED;
  }

  bUserMenu *um = ED_screen_shelf_ensure();
  if (RNA_boolean_get(op->ptr, "new_tab")) {
    /* Named after the file. */
    char name[FILE_MAX];
    STRNCPY(name, BLI_path_basename(filepath));
    BLI_path_extension_strip(name);
    for (char *c = name; *c; c++) {
      if (*c == '_') {
        *c = ' ';
      }
    }
    um = ED_screen_shelf_tab_add(name);
    U.shelf_active_tab = char(std::min(ED_screen_shelf_tabs_num() - 1, 255));
  }
  else if (RNA_boolean_get(op->ptr, "replace")) {
    BKE_blender_user_menu_item_free_list(&um->items);
  }

  int loaded = 0, skipped = 0;
  for (const std::shared_ptr<Value> &item_value : items->elements()) {
    const DictionaryValue *item = item_value->as_dictionary_value();
    if (item == nullptr) {
      continue;
    }
    const std::string type(item->lookup_str("type").value_or(""));
    const std::string name(item->lookup_str("name").value_or(""));
    if (type == "SEPARATOR") {
      BKE_blender_user_menu_item_add(&um->items, USER_MENU_TYPE_SEP);
      continue;
    }
    if (type == "OPERATOR") {
      const std::string idname(item->lookup_str("operator").value_or(""));
      wmOperatorType *ot = WM_operatortype_find(idname.c_str(), true);
      if (ot == nullptr) {
        skipped++;
        continue;
      }
      IDProperty *props = nullptr;
      if (const ArrayValue *props_value = item->lookup_array("properties")) {
        props = bke::idprop::convert_from_serialize_value(*props_value);
      }
      ED_screen_user_menu_item_add_operator(
          &um->items,
          name.empty() ? ot->name : name.c_str(),
          ot,
          props,
          std::string(item->lookup_str("enum_property").value_or("")).c_str(),
          wm::OpCallContext(item->lookup_int("call_context").value_or(0)));
      if (props) {
        IDP_FreeProperty(props);
      }
    }
    else if (type == "PROPERTY") {
      ED_screen_user_menu_item_add_prop(
          &um->items,
          name.c_str(),
          std::string(item->lookup_str("data_path").value_or("")).c_str(),
          std::string(item->lookup_str("property").value_or("")).c_str(),
          int(item->lookup_int("index").value_or(0)));
    }
    else {
      skipped++;
      continue;
    }

    bUserMenuItem *umi = static_cast<bUserMenuItem *>(um->items.last);
    STRNCPY(umi->icon_name, std::string(item->lookup_str("icon").value_or("")).c_str());
    int space_type = SPACE_EMPTY;
    RNA_enum_value_from_id(rna_enum_space_type_items,
                           std::string(item->lookup_str("editor").value_or("")).c_str(),
                           &space_type);
    umi->space_type = char(space_type);
    if (item->lookup_int("show_text").value_or(0)) {
      umi->shelf_flag |= USER_MENU_ITEM_SHELF_SHOW_TEXT;
    }
    if (!item->lookup_int("show_icon").value_or(1)) {
      umi->shelf_flag |= USER_MENU_ITEM_SHELF_HIDE_ICON;
    }
    loaded++;
  }

  shelf_items_changed(C);
  if (skipped) {
    BKE_reportf(op->reports,
                RPT_WARNING,
                "Loaded %d shelf buttons, %d need operators this Blender doesn't have",
                loaded,
                skipped);
  }
  else {
    BKE_reportf(op->reports, RPT_INFO, "Loaded %d shelf buttons", loaded);
  }
  return OPERATOR_FINISHED;
}

static void SHELF_OT_load(wmOperatorType *ot)
{
  ot->name = "Load Shelf";
  ot->idname = "SHELF_OT_load";
  ot->description = "Load the buttons of a saved shelf";

  ot->invoke = shelf_file_invoke;
  ot->exec = shelf_load_exec;

  shelf_file_props(ot, FILE_OPENFILE);
  RNA_def_boolean(ot->srna,
                  "replace",
                  true,
                  "Replace",
                  "Remove the buttons on the shelf first, otherwise the loaded ones are added "
                  "after them");
  RNA_def_boolean(ot->srna,
                  "new_tab",
                  false,
                  "New Tab",
                  "Load the buttons into a new tab, named after the file");
}

static void shelf_operatortypes()
{
  WM_operatortype_append(SHELF_OT_save);
  WM_operatortype_append(SHELF_OT_load);
  WM_operatortype_append(SHELF_OT_run_item);
  WM_operatortype_append(SHELF_OT_remove_item);
  WM_operatortype_append(SHELF_OT_move_item);
  WM_operatortype_append(SHELF_OT_toggle_item_text);
  WM_operatortype_append(SHELF_OT_toggle_item_icon);
  WM_operatortype_append(SHELF_OT_set_item_icon);
  WM_operatortype_append(SHELF_OT_rename_item);
  WM_operatortype_append(SHELF_OT_add_separator);
  WM_operatortype_append(SHELF_OT_tab_add);
  WM_operatortype_append(SHELF_OT_tab_remove);
  WM_operatortype_append(SHELF_OT_tab_rename);
  WM_operatortype_append(SHELF_OT_tab_move);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Drawing
 * \{ */

/** How a shelf button looks: its icon and the text next to it (or in place of it). */
struct ShelfButtonLook {
  int icon = ICON_NONE;
  std::string text;
  bool show_text = false;
  int x = 0, y = 0, width = 0, height = 0;
  /** The full name of the item. */
  std::string tip;
  /** A line between buttons, not a button. */
  bool is_separator = false;
};

/** An icon alone stays centered, with text next to it both go to the left. */
static ui::Button *shelf_button_finish(ui::Button *but, const ShelfButtonLook &look)
{
  if (look.show_text) {
    ui::button_drawflag_enable(but, ui::BUT_TEXT_LEFT);
    if (look.icon != ICON_NONE) {
      ui::button_drawflag_enable(but, ui::BUT_ICON_LEFT);
    }
  }
  return but;
}

/** Adds the button of a toggle, looking the property up in the editor it was added from. */
static ui::Button *shelf_prop_button_add(const bContext *C,
                                         ui::Block *block,
                                         const bUserMenuItem_Prop &umi_pr,
                                         const ShelfButtonLook &look)
{
  ShelfItemContext item_context(const_cast<bContext *>(C), shelf_item_area(C, umi_pr.item));

  /* Same lookup as Quick Favorites, the first part of the path is a context member. */
  char context_data_path[sizeof(umi_pr.context_data_path)];
  STRNCPY(context_data_path, umi_pr.context_data_path);
  char *data_path = strchr(context_data_path, '.');
  if (data_path) {
    *data_path = '\0';
    data_path += 1;
  }
  PointerRNA ptr = CTX_data_pointer_get(C, context_data_path);
  if (ptr.type == nullptr) {
    PointerRNA ctx_ptr = RNA_pointer_create_discrete(nullptr, RNA_Context, (void *)C);
    if (!RNA_path_resolve_full(&ctx_ptr, context_data_path, &ptr, nullptr, nullptr)) {
      return nullptr;
    }
  }
  PointerRNA prop_ptr = ptr;
  if (data_path && !RNA_path_resolve_full(&ptr, data_path, &prop_ptr, nullptr, nullptr)) {
    return nullptr;
  }
  PropertyRNA *prop = RNA_struct_find_property(&prop_ptr, umi_pr.prop_id);
  if (prop == nullptr || RNA_property_type(prop) != PROP_BOOLEAN) {
    return nullptr;
  }

  if (look.icon != ICON_NONE && look.text.empty()) {
    return ui::uiDefIconButR_prop(block,
                                  ui::ButtonType::Toggle,
                                  look.icon,
                                  look.x,
                                  look.y,
                                  look.width,
                                  look.height,
                                  &prop_ptr,
                                  prop,
                                  umi_pr.prop_index,
                                  0,
                                  0,
                                  look.tip);
  }
  ui::Button *but = ui::uiDefIconTextButR_prop(block,
                                               ui::ButtonType::Toggle,
                                               look.icon,
                                               look.text,
                                               look.x,
                                               look.y,
                                               look.width,
                                               look.height,
                                               &prop_ptr,
                                               prop,
                                               umi_pr.prop_index,
                                               0,
                                               0,
                                               look.tip);
  return shelf_button_finish(but, look);
}

static ui::Button *shelf_operator_button_add(ui::Block *block,
                                             wmOperatorType *ot_run,
                                             const int index,
                                             const ShelfButtonLook &look)
{
  ui::Button *but;
  if (look.icon != ICON_NONE && look.text.empty()) {
    but = ui::uiDefIconButO_ptr(block,
                                ui::ButtonType::But,
                                ot_run,
                                wm::OpCallContext::InvokeDefault,
                                look.icon,
                                look.x,
                                look.y,
                                look.width,
                                look.height,
                                std::nullopt);
  }
  else {
    but = ui::uiDefIconTextButO_ptr(block,
                                    ui::ButtonType::But,
                                    ot_run,
                                    wm::OpCallContext::InvokeDefault,
                                    look.icon,
                                    look.text,
                                    look.x,
                                    look.y,
                                    look.width,
                                    look.height,
                                    std::nullopt);
    shelf_button_finish(but, look);
  }
  RNA_int_set(ui::button_operator_ptr_ensure(but), "index", index);
  return but;
}

static float shelf_icon_scale()
{
  switch (U.shelf_icon_size) {
    case USER_SHELF_ICON_SIZE_MEDIUM:
      return 1.5f;
    case USER_SHELF_ICON_SIZE_LARGE:
      return 2.0f;
    case USER_SHELF_ICON_SIZE_CUSTOM:
      /* Preferences saved before the setting existed read it as zero. */
      return U.shelf_icon_scale > 0.0f ? std::clamp(U.shelf_icon_scale, 0.75f, 4.0f) : 1.25f;
    default:
      return 1.0f;
  }
}

/** Where every button of the shelf goes, the same for laying the region out and drawing it. */
struct ShelfLayout {
  Vector<ShelfButtonLook> looks;
  float icon_scale = 1.0f;
  int size = 0, gap = 0, pad = 0;
  /** The editor type menu, drawn first by the old Shelf editor when its header is hidden. */
  bool show_editor_menu = false;
  int editor_menu_width = 0;
  /** The tabs, left of the buttons, and the button that adds one. */
  Vector<ShelfButtonLook> tabs;
  int tab_height = 0;
  /** Where the first button of each row starts. */
  int start_x = 0;
  int content_height = 0;

  int region_height() const
  {
    return content_height + 2 * pad;
  }
};

static ShelfLayout shelf_layout_calc(const bContext *C, const ARegion *region)
{
  ShelfLayout layout;
  bUserMenu *um = ED_screen_shelf_find();

  /* The buttons grow by as much as their icons do. */
  layout.icon_scale = shelf_icon_scale();
  layout.size = UI_UNIT_X + int(ICON_DEFAULT_HEIGHT * (layout.icon_scale - 1.0f) * UI_SCALE_FAC);
  layout.gap = int(2.0f * UI_SCALE_FAC);
  /* With small icons, as tall as a header. */
  layout.pad = std::max((int(HEADERY * UI_SCALE_FAC) - UI_UNIT_Y) / 2, int(UI_SCALE_FAC));
  /* Room for the text's margins on both sides, so names are never cut short with "...". */
  const int text_margin = UI_UNIT_X;
  const int icon_width = int(ICON_DEFAULT_HEIGHT * layout.icon_scale * UI_SCALE_FAC) +
                         int(6.0f * UI_SCALE_FAC);

  const ScrArea *area = CTX_wm_area(C);
  if (area && area->spacetype == SPACE_SHELF) {
    const ARegion *header = BKE_area_find_region_type(area, RGN_TYPE_HEADER);
    layout.show_editor_menu = header == nullptr || (header->flag & RGN_FLAG_HIDDEN);
  }
  layout.editor_menu_width = layout.size + int(0.6f * UI_UNIT_X);
  layout.start_x = layout.pad +
                   (layout.show_editor_menu ? layout.editor_menu_width + 2 * layout.gap : 0);

  if (!(U.shelf_flag & USER_SHELF_HIDE_TABS)) {
    layout.tab_height = std::min(layout.size, int(UI_UNIT_Y));
    int tab_x = layout.start_x;
    const int tabs_num = std::max(ED_screen_shelf_tabs_num(), 1);
    for (int i = 0; i < tabs_num; i++) {
      const bUserMenu *tab = ED_screen_shelf_tab_get(i);
      ShelfButtonLook look;
      look.text = tab ? ED_screen_shelf_tab_name(tab) : IFACE_("Main");
      look.tip = look.text;
      look.show_text = true;
      look.x = tab_x;
      look.width = ui::fontstyle_string_width(UI_FSTYLE_WIDGET, look.text.c_str()) +
                   text_margin;
      look.height = layout.tab_height;
      tab_x += look.width;
      layout.tabs.append(std::move(look));
    }
    /* The button that adds a tab. */
    ShelfButtonLook add;
    add.icon = ICON_ADD;
    add.x = tab_x;
    add.width = layout.tab_height;
    add.height = layout.tab_height;
    layout.tabs.append(std::move(add));
    layout.start_x = tab_x + add.width + 4 * layout.gap;
  }

  /* Left to right in rows. Until the buttons are made, #ShelfButtonLook.y is the row. */
  int x = layout.start_x;
  int row = 0;
  if (um) {
    for (bUserMenuItem &umi : um->items) {
      ShelfButtonLook look;
      look.height = layout.size;
      if (umi.type == USER_MENU_TYPE_SEP) {
        look.is_separator = true;
        look.tip = TIP_("Separator");
        look.width = std::max(int(0.5f * UI_UNIT_X), 2 * layout.gap + int(4.0f * UI_SCALE_FAC));
      }
      else {
        look.icon = (umi.shelf_flag & USER_MENU_ITEM_SHELF_HIDE_ICON) ? ICON_NONE :
                                                                          ED_screen_user_menu_item_icon(C, umi);
        look.tip = shelf_item_name(umi);
        look.show_text = (umi.shelf_flag & USER_MENU_ITEM_SHELF_SHOW_TEXT) != 0;
      }
      if (look.is_separator) {
        /* Sized above. */
      }
      else if (look.show_text) {
        look.text = look.tip;
        const int text_width = ui::fontstyle_string_width(UI_FSTYLE_WIDGET, look.text.c_str());
        look.width = (look.icon != ICON_NONE ? icon_width : 0) + text_width + text_margin;
      }
      else if (umi.shelf_flag & USER_MENU_ITEM_SHELF_HIDE_ICON) {
        /* With the icon turned off on purpose, the name stands in for it. */
        look.show_text = true;
        look.text = look.tip;
        look.width = ui::fontstyle_string_width(UI_FSTYLE_WIDGET, look.text.c_str()) +
                     text_margin;
      }
      else {
        look.text = look.icon != ICON_NONE ? "" : shelf_item_abbreviation(look.tip);
        look.width = layout.size;
      }
      if (x > layout.start_x && x + look.width > region->winx - layout.pad) {
        x = layout.start_x;
        row++;
      }
      look.x = x;
      look.y = row;
      x += look.width + layout.gap;
      layout.looks.append(std::move(look));
    }
  }

  const int rows = row + 1;
  layout.content_height = rows * layout.size + (rows - 1) * layout.gap;
  return layout;
}

/** The strip grows a row taller whenever its buttons wrap. */
static void shelf_region_layout(const bContext *C, ARegion *region)
{
  const ShelfLayout layout = shelf_layout_calc(C, region);
  const int new_sizey = std::max(int(layout.region_height() / UI_SCALE_FAC), 1);
  if (region->sizey != new_sizey) {
    region->sizey = new_sizey;
    ED_area_tag_region_size_update(CTX_wm_area(C), region);
  }
}

static void shelf_region_draw(const bContext *C, ARegion *region)
{
  ui::theme::frame_buffer_clear(TH_HEADER);

  bUserMenu *um = ED_screen_shelf_find();
  ShelfLayout layout = shelf_layout_calc(C, region);
  const int size = layout.size;

  /* Centered vertically, from the top when the region isn't tall enough yet. Drawn in region
   * pixels, so y goes up. */
  const int top = std::max((region->winy - layout.content_height) / 2, 0);

  ui::Block *block = ui::block_begin(C, region, __func__, ui::EmbossType::Emboss);
  ui::block_icon_scale_set(block, layout.icon_scale);

  if (layout.show_editor_menu) {
    const ScrArea *area = CTX_wm_area(C);
    PointerRNA area_ptr = RNA_pointer_create_discrete(
        &CTX_wm_screen(C)->id, RNA_Area, const_cast<ScrArea *>(area));
    ui::uiDefButR(block,
                  ui::ButtonType::Menu,
                  "",
                  layout.pad,
                  region->winy - top - size,
                  layout.editor_menu_width,
                  size,
                  &area_ptr,
                  "ui_type",
                  0,
                  0.0f,
                  0.0f,
                  "");
  }

  /* The tabs, centered on the first row of buttons. */
  if (!layout.tabs.is_empty()) {
    const int tab_y = region->winy - top - (size + layout.tab_height) / 2;
    wmOperatorType *ot_tab_add = WM_operatortype_find("SHELF_OT_tab_add", false);
    ui::block_align_begin(block);
    for (const int i : layout.tabs.index_range()) {
      const ShelfButtonLook &look = layout.tabs[i];
      if (i == layout.tabs.size() - 1) {
        ui::block_align_end(block);
        ui::uiDefIconButO_ptr(block,
                              ui::ButtonType::But,
                              ot_tab_add,
                              wm::OpCallContext::InvokeDefault,
                              look.icon,
                              look.x + layout.gap,
                              tab_y,
                              look.width,
                              look.height,
                              TIP_("Add a tab to the shelf"));
        break;
      }
      ui::Button *but = ui::uiDefButC(block,
                                      ui::ButtonType::Row,
                                      look.text,
                                      look.x,
                                      tab_y,
                                      look.width,
                                      look.height,
                                      &U.shelf_active_tab,
                                      0,
                                      float(i),
                                      look.tip);
      ui::button_func_set(but, shelf_tab_changed_cb, nullptr, nullptr);
      ui::button_context_int_set(block, but, "shelf_tab_index", i);
    }
  }

  /* The lines of the separators, drawn over their (invisible) buttons. */
  Vector<std::array<int, 3>> separator_lines;

  if (layout.looks.is_empty()) {
    ui::Layout &label_layout = ui::block_layout(
        block,
        ui::LayoutDirection::Vertical,
        ui::LayoutType::Panel,
        layout.start_x,
        region->winy - std::max((region->winy - UI_UNIT_Y) / 2, 0),
        std::max(region->winx - layout.start_x - layout.pad, size),
        0,
        0,
        ui::style_get_dpi());
    label_layout.label(IFACE_("Right-click a button and choose Add to Shelf"), ICON_INFO);
    ui::block_layout_resolve(block);
  }
  else {
    wmOperatorType *ot_run = WM_operatortype_find("SHELF_OT_run_item", false);
    int index = 0;
    for (bUserMenuItem &umi : um->items) {
      ShelfButtonLook &look = layout.looks[index];
      look.y = region->winy - top - look.y * (size + layout.gap) - size;

      ui::Button *but = nullptr;
      if (look.is_separator) {
        /* An empty button, only there so it can be right-clicked to move or remove it. */
        ui::block_emboss_set(block, ui::EmbossType::None);
        but = ui::uiDefBut(
            block, ui::ButtonType::But, "", look.x, look.y, look.width, look.height, nullptr, 0, 0,
            look.tip);
        ui::block_emboss_set(block, ui::EmbossType::Emboss);
        const int inset = int(3.0f * UI_SCALE_FAC);
        separator_lines.append(
            {look.x + look.width / 2, look.y + inset, look.y + look.height - inset});
        ui::button_context_int_set(block, but, "shelf_item_index", index);
        index++;
        continue;
      }
      if (umi.type == USER_MENU_TYPE_OPERATOR) {
        but = shelf_operator_button_add(block, ot_run, index, look);
      }
      else if (umi.type == USER_MENU_TYPE_PROP) {
        but = shelf_prop_button_add(
            C, block, reinterpret_cast<const bUserMenuItem_Prop &>(umi), look);
      }

      if (but == nullptr) {
        /* The item can't be shown here (its editor is closed, or it's not an operator or a
         * toggle). It still gets a button, so it can be removed. */
        but = ui::uiDefIconTextBut(
            block,
            ui::ButtonType::But,
            look.icon,
            look.text,
            look.x,
            look.y,
            look.width,
            look.height,
            nullptr,
            fmt::format(fmt::runtime(TIP_("{} (not available here)")), look.tip));
        shelf_button_finish(but, look);
        ui::button_flag_enable(but, ui::BUT_INACTIVE);
      }
      ui::button_context_int_set(block, but, "shelf_item_index", index);
      index++;
    }
  }

  ui::block_end(C, block);
  ui::block_draw(C, block);

  if (!separator_lines.is_empty()) {
    wmOrtho2_region_pixelspace(region);
    const uint pos = GPU_vertformat_attr_add(
        immVertexFormat(), "pos", gpu::VertAttrType::SFLOAT_32_32);
    GPU_blend(GPU_BLEND_ALPHA);
    immBindBuiltinProgram(GPU_SHADER_3D_UNIFORM_COLOR);
    immUniformThemeColorAlpha(TH_TEXT, 0.35f);
    const float half = std::max(UI_SCALE_FAC, 1.0f) * 0.5f;
    for (const std::array<int, 3> &line : separator_lines) {
      immRectf(pos, line[0] - half, line[1], line[0] + half, line[2]);
    }
    immUnbindProgram();
    GPU_blend(GPU_BLEND_NONE);
  }
}

static void shelf_region_init(wmWindowManager *wm, ARegion *region)
{
  /* The strip is sized by its layout, so when it's shown again it can start out too small and
   * only become visible when the area updates its region sizes. That update calls this, but
   * doesn't add the default handlers, so add the button handlers here or the buttons don't
   * respond to clicks. Adding them again replaces the old ones. */
  if (region->runtime->visible) {
    ui::region_handlers_add(&region->runtime->handlers);
  }

  wmKeyMap *keymap = WM_keymap_ensure(
      wm->runtime->defaultconf, "Shelf", SPACE_SHELF, RGN_TYPE_WINDOW);
  WM_event_add_keymap_handler(&region->runtime->handlers, keymap);
}

static void shelf_region_listener(const wmRegionListenerParams *params)
{
  /* The toggles on the shelf can change from anywhere, and the shelf is cheap to draw. */
  ED_region_tag_redraw(params->region);
}

ARegionType *ED_shelf_region_type_create()
{
  ARegionType *art = MEM_new_zeroed<ARegionType>("shelf strip region");
  art->regionid = RGN_TYPE_SHELF;
  art->prefsizey = HEADERY;
  art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_FRAMES;
  art->init = shelf_region_init;
  art->layout = shelf_region_layout;
  art->draw = shelf_region_draw;
  art->listener = shelf_region_listener;
  return art;
}

static void shelf_header_region_init(wmWindowManager * /*wm*/, ARegion *region)
{
  ED_region_header_init(region);
}

static void shelf_header_region_draw(const bContext *C, ARegion *region)
{
  ED_region_header(C, region);
}

static void shelf_header_region_listener(const wmRegionListenerParams *params)
{
  const wmNotifier *wmn = params->notifier;
  if (wmn->category == NC_SPACE && wmn->data == ND_SPACE_SHELF) {
    ED_region_tag_redraw(params->region);
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Space Type
 * \{ */

static SpaceLink *shelf_create(const ScrArea * /*area*/, const Scene * /*scene*/)
{
  SpaceShelf *sshelf = MEM_new<SpaceShelf>("init shelf");
  sshelf->spacetype = SPACE_SHELF;

  ARegion *region = BKE_area_region_new();
  BLI_addtail(&sshelf->regionbase, region);
  region->regiontype = RGN_TYPE_HEADER;
  region->alignment = (U.uiflag & USER_HEADER_BOTTOM) ? RGN_ALIGN_BOTTOM : RGN_ALIGN_TOP;
  /* The shelf is meant to be a thin strip of icons, the header would take half of it. */
  region->flag |= RGN_FLAG_HIDDEN;

  region = BKE_area_region_new();
  BLI_addtail(&sshelf->regionbase, region);
  region->regiontype = RGN_TYPE_WINDOW;
  region->v2d.scroll = V2D_SCROLL_RIGHT | V2D_SCROLL_VERTICAL_HIDE;
  region->v2d.align = V2D_ALIGN_NO_NEG_X | V2D_ALIGN_NO_POS_Y;
  region->v2d.keepzoom = V2D_LOCKZOOM_X | V2D_LOCKZOOM_Y | V2D_LIMITZOOM | V2D_KEEPASPECT;
  region->v2d.keeptot = V2D_KEEPTOT_STRICT;
  region->v2d.minzoom = region->v2d.maxzoom = 1.0f;

  return reinterpret_cast<SpaceLink *>(sshelf);
}

/* Doesn't free the space-link itself. */
static void shelf_free(SpaceLink * /*sl*/) {}

static void shelf_init(wmWindowManager * /*wm*/, ScrArea * /*area*/) {}

static SpaceLink *shelf_duplicate(SpaceLink *sl)
{
  return reinterpret_cast<SpaceLink *>(MEM_dupalloc(reinterpret_cast<SpaceShelf *>(sl)));
}

static void shelf_keymap(wmKeyConfig *keyconf)
{
  WM_keymap_ensure(keyconf, "Shelf", SPACE_SHELF, RGN_TYPE_WINDOW);
}

static void shelf_space_blend_write(BlendWriter *writer, SpaceLink *sl)
{
  writer->write_struct_cast<SpaceShelf>(sl);
}

void ED_spacetype_shelf()
{
  std::unique_ptr<SpaceType> st = std::make_unique<SpaceType>();
  ARegionType *art;

  st->spaceid = SPACE_SHELF;
  STRNCPY_UTF8(st->name, "Shelf");

  st->create = shelf_create;
  st->free = shelf_free;
  st->init = shelf_init;
  st->duplicate = shelf_duplicate;
  st->operatortypes = shelf_operatortypes;
  st->keymap = shelf_keymap;
  st->blend_write = shelf_space_blend_write;

  /* regions: main window */
  art = MEM_new_zeroed<ARegionType>("spacetype shelf region");
  art->regionid = RGN_TYPE_WINDOW;
  art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_VIEW2D;
  art->init = shelf_region_init;
  art->draw = shelf_region_draw;
  art->listener = shelf_region_listener;
  BLI_addhead(&st->regiontypes, art);

  /* regions: header */
  art = MEM_new_zeroed<ARegionType>("spacetype shelf header region");
  art->regionid = RGN_TYPE_HEADER;
  art->prefsizey = HEADERY;
  art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_VIEW2D | ED_KEYMAP_HEADER;
  art->init = shelf_header_region_init;
  art->draw = shelf_header_region_draw;
  art->listener = shelf_header_region_listener;
  BLI_addhead(&st->regiontypes, art);

  BKE_spacetype_register(std::move(st));
}

/** \} */

}  // namespace blender
