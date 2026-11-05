/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup sptoolbox
 *
 * The Toolbox: a column of tool buttons in sections, with vertical tabs on the right side. The
 * tabs are panel categories, the panels and the buttons in them are defined in Python, see
 * `space_toolbox.py`.
 */

#include <memory>

#include "MEM_guardedalloc.h"

#include "BLI_listbase.h"
#include "BLI_string_utf8.h"

#include "DNA_screen_types.h"
#include "DNA_space_types.h"
#include "DNA_userdef_types.h"

#include "BKE_context.hh"
#include "BKE_screen.hh"

#include "BLO_read_write.hh"

#include "ED_screen.hh"
#include "ED_space_api.hh"

#include "UI_interface_c.hh"
#include "UI_view2d.hh"

#include "WM_api.hh"
#include "WM_types.hh"

namespace blender {

/* -------------------------------------------------------------------- */
/** \name Main Region
 * \{ */

static void toolbox_main_region_init(wmWindowManager *wm, ARegion *region)
{
  ED_region_panels_init(wm, region);

  wmKeyMap *keymap = WM_keymap_ensure(
      wm->runtime->defaultconf, "Toolbox", SPACE_TOOLBOX, RGN_TYPE_WINDOW);
  WM_event_add_keymap_handler(&region->runtime->handlers, keymap);
}

static void toolbox_main_region_layout(const bContext *C, ARegion *region)
{
  ED_region_panels_layout(C, region);

  /* The large buttons are made taller in Python, their icons are scaled to fit them here. */
  const SpaceToolbox *stoolbox = reinterpret_cast<const SpaceToolbox *>(CTX_wm_space_data(C));
  const float icon_scale = (stoolbox && stoolbox->icon_scale >= 1.0f) ? stoolbox->icon_scale :
                                                                         1.5f;
  for (ui::Block &block : region->runtime->uiblocks) {
    ui::block_icon_scale_set(&block, icon_scale);
    ui::block_buttons_text_left_set(&block);
  }
}

static void toolbox_main_region_draw(const bContext *C, ARegion *region)
{
  ED_region_panels_draw(C, region);
}

static void toolbox_main_region_listener(const wmRegionListenerParams *params)
{
  /* The toggles in the toolbox can change from anywhere, like the ones on the shelf. */
  ED_region_tag_redraw(params->region);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Header Region
 * \{ */

static void toolbox_header_region_init(wmWindowManager * /*wm*/, ARegion *region)
{
  ED_region_header_init(region);
}

static void toolbox_header_region_draw(const bContext *C, ARegion *region)
{
  ED_region_header(C, region);
}

static void toolbox_header_region_listener(const wmRegionListenerParams *params)
{
  const wmNotifier *wmn = params->notifier;
  if (wmn->category == NC_SPACE && wmn->data == ND_SPACE_TOOLBOX) {
    ED_region_tag_redraw(params->region);
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Space Type
 * \{ */

static SpaceLink *toolbox_create(const ScrArea * /*area*/, const Scene * /*scene*/)
{
  SpaceToolbox *stoolbox = MEM_new<SpaceToolbox>("init toolbox");
  stoolbox->spacetype = SPACE_TOOLBOX;

  ARegion *region = BKE_area_region_new();
  BLI_addtail(&stoolbox->regionbase, region);
  region->regiontype = RGN_TYPE_HEADER;
  region->alignment = (U.uiflag & USER_HEADER_BOTTOM) ? RGN_ALIGN_BOTTOM : RGN_ALIGN_TOP;

  region = BKE_area_region_new();
  BLI_addtail(&stoolbox->regionbase, region);
  region->regiontype = RGN_TYPE_WINDOW;

  return reinterpret_cast<SpaceLink *>(stoolbox);
}

/* Doesn't free the space-link itself. */
static void toolbox_free(SpaceLink * /*sl*/) {}

static void toolbox_init(wmWindowManager * /*wm*/, ScrArea * /*area*/) {}

static SpaceLink *toolbox_duplicate(SpaceLink *sl)
{
  return reinterpret_cast<SpaceLink *>(MEM_dupalloc(reinterpret_cast<SpaceToolbox *>(sl)));
}

static void toolbox_keymap(wmKeyConfig *keyconf)
{
  WM_keymap_ensure(keyconf, "Toolbox", SPACE_TOOLBOX, RGN_TYPE_WINDOW);
}

static void toolbox_space_blend_write(BlendWriter *writer, SpaceLink *sl)
{
  writer->write_struct_cast<SpaceToolbox>(sl);
}

void ED_spacetype_toolbox()
{
  std::unique_ptr<SpaceType> st = std::make_unique<SpaceType>();
  ARegionType *art;

  st->spaceid = SPACE_TOOLBOX;
  STRNCPY_UTF8(st->name, "Toolbox");

  st->create = toolbox_create;
  st->free = toolbox_free;
  st->init = toolbox_init;
  st->duplicate = toolbox_duplicate;
  st->keymap = toolbox_keymap;
  st->blend_write = toolbox_space_blend_write;

  /* regions: main window, the tabs are panel categories. */
  art = MEM_new_zeroed<ARegionType>("spacetype toolbox region");
  art->regionid = RGN_TYPE_WINDOW;
  art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_FRAMES;
  art->flag = ARegionTypeFlag::UsePanelCategoryTabs | ARegionTypeFlag::PanelCategoryTabsRight;
  art->init = toolbox_main_region_init;
  art->layout = toolbox_main_region_layout;
  art->draw = toolbox_main_region_draw;
  art->listener = toolbox_main_region_listener;
  BLI_addhead(&st->regiontypes, art);

  /* regions: header */
  art = MEM_new_zeroed<ARegionType>("spacetype toolbox header region");
  art->regionid = RGN_TYPE_HEADER;
  art->prefsizey = HEADERY;
  art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_VIEW2D | ED_KEYMAP_HEADER;
  art->init = toolbox_header_region_init;
  art->draw = toolbox_header_region_draw;
  art->listener = toolbox_header_region_listener;
  BLI_addhead(&st->regiontypes, art);

  BKE_spacetype_register(std::move(st));
}

/** \} */

}  // namespace blender
