# SPDX-FileCopyrightText: 2026 Blender Authors
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os

import bpy
from bpy.props import IntProperty, StringProperty
from bpy.types import Header, Menu, Operator


class SHELF_HT_header(Header):
    bl_space_type = 'SHELF'

    def draw(self, context):
        # Kept small, the shelf is meant to be a thin strip. New shelves hide the header.
        layout = self.layout
        layout.template_header()
        view = context.preferences.view
        layout.prop(view, "shelf_icon_size", text="")
        if view.shelf_icon_size == 'CUSTOM':
            layout.prop(view, "shelf_icon_scale", text="Scale")


class SHELF_MT_presets(Menu):
    bl_label = "Starter Presets"

    @staticmethod
    def preset_files():
        files = []
        for directory in bpy.utils.preset_paths("shelf"):
            for filename in sorted(os.listdir(directory)):
                if filename.lower().endswith(".json"):
                    files.append(os.path.join(directory, filename))
        return files

    def draw(self, _context):
        layout = self.layout
        # Load the file right away, without the file browser.
        layout.operator_context = 'EXEC_DEFAULT'
        files = self.preset_files()
        if not files:
            layout.label(text="No presets found")
            return
        for filepath in files:
            props = layout.operator(
                "shelf.load", text=bpy.path.display_name(filepath), icon='PRESET',
            )
            props.filepath = filepath
            props.new_tab = True


class SHELF_MT_context_menu(Menu):
    bl_label = "Shelf"

    def draw(self, context):
        layout = self.layout
        space = context.space_data
        view = context.preferences.view

        layout.operator("shelf.add_separator", text="Add Separator")
        layout.separator()
        layout.operator("shelf.tab_add", text="Add Tab...", icon='ADD')
        layout.prop(view, "shelf_show_tabs", text="Show Tabs")
        layout.menu("SHELF_MT_presets", icon='PRESET')
        layout.separator()
        layout.label(text="Icon Size")
        layout.props_enum(view, "shelf_icon_size")
        if view.shelf_icon_size == 'CUSTOM':
            layout.prop(view, "shelf_icon_scale", text="Icon Scale")
        layout.separator()
        layout.operator("shelf.save", text="Save Shelf...", icon='FILE_TICK')
        layout.operator("shelf.load", text="Load Shelf...", icon='FILE_FOLDER')
        layout.separator()
        if space.type == 'SHELF':
            layout.prop(space, "show_region_header")
        else:
            region = context.region
            if region is not None and region.type == 'SHELF':
                text = "Flip to Bottom" if region.alignment == 'TOP' else "Flip to Top"
                layout.operator("screen.region_flip", text=text)
            layout.prop(space, "show_region_shelf", text="Show Shelf")


class SHELF_OT_pick_icon(Operator):
    """Pick another icon for this button"""
    bl_idname = "shelf.pick_icon"
    bl_label = "Change Icon"
    bl_options = {'INTERNAL'}

    index: IntProperty(
        name="Index",
        description="Index of the item on the shelf",
        options={'HIDDEN', 'SKIP_SAVE'},
    )
    filter: StringProperty(
        name="Filter",
        description="Only show the icons with this in their name",
        options={'TEXTEDIT_UPDATE', 'SKIP_SAVE'},
    )

    COLUMNS = 24
    # More would make the popup taller than most screens, typing narrows it down.
    MAX_SHOWN = COLUMNS * 20

    _icons = None

    @classmethod
    def icons(cls):
        if cls._icons is None:
            items = bpy.types.UILayout.bl_rna.functions["prop"].parameters["icon"].enum_items
            cls._icons = [
                item.identifier for item in items
                if item.identifier != 'NONE' and not item.identifier.startswith("BLANK")
            ]
        return cls._icons

    def invoke(self, context, _event):
        return context.window_manager.invoke_popup(self, width=self.COLUMNS * 26)

    def execute(self, _context):
        return {'FINISHED'}

    def draw(self, _context):
        layout = self.layout
        # Set the icon right away, instead of searching for it.
        layout.operator_context = 'EXEC_DEFAULT'

        row = layout.row()
        row.prop(self, "filter", text="", icon='VIEWZOOM')
        props = row.operator("shelf.set_item_icon", text="No Icon")
        props.index = self.index
        props.icon = 'NONE'

        text = self.filter.strip().upper().replace(" ", "_")
        icons = [name for name in self.icons() if text in name]
        if not icons:
            layout.label(text="No icons match")
            return

        grid = layout.grid_flow(
            row_major=True, columns=self.COLUMNS, even_columns=True, even_rows=True, align=True,
        )
        for name in icons[:self.MAX_SHOWN]:
            props = grid.operator("shelf.set_item_icon", text="", icon=name)
            props.index = self.index
            props.icon = name
        hidden = len(icons) - self.MAX_SHOWN
        if hidden > 0:
            layout.label(
                text="{:d} more, type in the filter to find them".format(hidden), icon='INFO',
            )


classes = (
    SHELF_HT_header,
    SHELF_MT_presets,
    SHELF_MT_context_menu,
    SHELF_OT_pick_icon,
)

if __name__ == "__main__":  # only for live edit.
    from bpy.utils import register_class
    for cls in classes:
        register_class(cls)
