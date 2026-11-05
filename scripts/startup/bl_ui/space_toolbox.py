# SPDX-FileCopyrightText: 2026 Blender Authors
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""
The Toolbox editor: tool buttons in sections, with vertical tabs on the right.

The contents are kept in ``toolbox.json`` in the user configuration folder, the default contents
in ``presets/toolbox/default.json``. Each tab is a panel category, its panel is registered when
the contents are loaded, see :func:`register_tabs`.
"""

import json
import os

import bpy
from bpy.props import EnumProperty, IntProperty, StringProperty
from bpy.types import Header, Menu, Operator, Panel

FILE_NAME = "toolbox.json"
STYLES = (
    ('ICONS', "Icons", "Large icon buttons in a grid"),
    ('LIST', "List", "One button with its name per row"),
)

# Set by tests, so they don't write the user's file.
user_path_override = None

_data = None
_tab_panels = []


# -----------------------------------------------------------------------------
# Contents

def _user_path():
    if user_path_override:
        return user_path_override
    return os.path.join(bpy.utils.user_resource('CONFIG'), FILE_NAME)


def default_path():
    for directory in bpy.utils.preset_paths("toolbox"):
        filepath = os.path.join(directory, "default.json")
        if os.path.isfile(filepath):
            return filepath
    return None


def _read(filepath):
    if not filepath or not os.path.isfile(filepath):
        return None
    try:
        with open(filepath, encoding="utf-8") as fh:
            data = json.load(fh)
    except (OSError, ValueError) as ex:
        print("Toolbox: can't read {!r}: {!s}".format(filepath, ex))
        return None
    if not isinstance(data, dict) or not isinstance(data.get("tabs"), list):
        return None
    for tab in data["tabs"]:
        tab.setdefault("name", "Tab")
        tab.setdefault("sections", [])
        for sec in tab["sections"]:
            sec.setdefault("name", "Section")
            sec.setdefault("style", 'ICONS')
            sec.setdefault("items", [])
    return data


def _empty():
    return {"blender_toolbox": 1, "tabs": [{"name": "Tools", "sections": []}]}


def data():
    global _data
    if _data is None:
        _data = _read(_user_path()) or _read(default_path()) or _empty()
        if not _data["tabs"]:
            _data["tabs"] = _empty()["tabs"]
    return _data


def tabs():
    return data()["tabs"]


def _redraw():
    wm = bpy.context.window_manager
    if wm is None:
        return
    for window in wm.windows:
        for area in window.screen.areas:
            if area.type == 'TOOLBOX':
                area.tag_redraw()


def save():
    filepath = _user_path()
    try:
        os.makedirs(os.path.dirname(filepath), exist_ok=True)
        with open(filepath, "w", encoding="utf-8", newline="\n") as fh:
            json.dump(data(), fh, indent=1)
            fh.write("\n")
    except OSError as ex:
        print("Toolbox: can't save {!r}: {!s}".format(filepath, ex))
    _redraw()


def reset():
    global _data
    _data = _read(default_path()) or _empty()
    save()
    register_tabs()


def get_tab(tab):
    tab_list = tabs()
    return tab_list[tab] if 0 <= tab < len(tab_list) else None


def get_section(tab, section):
    tab_data = get_tab(tab)
    if tab_data is None:
        return None
    sections = tab_data["sections"]
    return sections[section] if 0 <= section < len(sections) else None


def get_item(tab, section, item):
    sec = get_section(tab, section)
    if sec is None:
        return None
    items = sec["items"]
    return items[item] if 0 <= item < len(items) else None


def _unique_tab_name(name, skip=-1):
    names = {t["name"] for i, t in enumerate(tabs()) if i != skip}
    base = name.strip() or "Tab"
    name = base
    number = 1
    while name in names:
        number += 1
        name = "{:s} {:d}".format(base, number)
    return name


def active_tab_index(context):
    region = context.region
    if region is None or context.area is None or context.area.type != 'TOOLBOX':
        return None
    try:
        name = region.active_panel_category
    except (AttributeError, TypeError):
        return None
    for i, tab in enumerate(tabs()):
        if tab["name"] == name:
            return i
    return 0 if tabs() else None


def _move(items, index, offset):
    new_index = index + offset
    if not (0 <= index < len(items) and 0 <= new_index < len(items)):
        return False
    items.insert(new_index, items.pop(index))
    return True


# -----------------------------------------------------------------------------
# Items

_icon_names = None


def icon_names():
    global _icon_names
    if _icon_names is None:
        items = bpy.types.UILayout.bl_rna.functions["prop"].parameters["icon"].enum_items
        _icon_names = [
            item.identifier for item in items
            if item.identifier != 'NONE' and not item.identifier.startswith("BLANK")
        ]
    return _icon_names


_icon_set = None


def _icon(name):
    global _icon_set
    if _icon_set is None:
        _icon_set = set(icon_names())
    return name if name in _icon_set else 'NONE'


def _operator(idname):
    """The ``bpy.ops`` function of an operator, or None when there is no such operator."""
    module, _, name = (idname or "").partition(".")
    if not module or not name:
        return None
    func = getattr(getattr(bpy.ops, module), name)
    try:
        func.get_rna_type()
    except KeyError:
        return None
    return func


def _python_idname(rna_idname):
    """``MESH_OT_bevel`` -> ``mesh.bevel``."""
    module, sep, name = rna_idname.partition("_OT_")
    if not sep:
        return rna_idname
    return "{:s}.{:s}".format(module.lower(), name)


def _find_area(context, space_type):
    """The largest editor of this type, in the current window first."""
    if context.area and context.area.type == space_type:
        return context.window, context.area
    windows = [context.window] if context.window else []
    windows += [w for w in context.window_manager.windows if w != context.window]
    for window in windows:
        areas = [a for a in window.screen.areas if a.type == space_type]
        if areas:
            return window, max(areas, key=lambda a: a.width * a.height)
    return None, None


def _view3d_space(context):
    _window, area = _find_area(context, 'VIEW_3D')
    return area.spaces.active if area else None


def _property_root(context, root):
    if root == "scene":
        return context.scene
    if root == "object":
        return context.object
    if root == "object_data":
        return context.object.data if context.object else None
    if root == "view3d":
        return _view3d_space(context)
    if root == "preferences":
        return context.preferences
    return None


def _property_owner(context, item):
    owner = _property_root(context, item.get("root", ""))
    path = item.get("path", "")
    if owner is not None and path:
        try:
            owner = owner.path_resolve(path)
        except ValueError:
            return None
    if owner is None or item.get("property", "") not in owner.bl_rna.properties:
        return None
    return owner


def item_name(context, item):
    if item.get("name"):
        return item["name"]
    item_type = item.get("type", 'OPERATOR')
    if item_type == 'OPERATOR':
        func = _operator(item.get("operator"))
        return func.get_rna_type().name if func else item.get("operator", "")
    if item_type == 'TOOL':
        return item.get("tool", "").rpartition(".")[2].replace("_", " ").title()
    if item_type == 'MENU':
        menu_type = getattr(bpy.types, item.get("menu", ""), None)
        return getattr(menu_type, "bl_label", item.get("menu", ""))
    if item_type == 'PROPERTY':
        owner = _property_owner(context, item)
        if owner is not None:
            return owner.bl_rna.properties[item["property"]].name
        return item.get("property", "")
    return ""


def item_description(context, item):
    item_type = item.get("type", 'OPERATOR')
    if item_type == 'OPERATOR':
        func = _operator(item.get("operator"))
        if func is None:
            return "Unknown operator {!r}".format(item.get("operator", ""))
        return func.get_rna_type().description or func.get_rna_type().name
    if item_type == 'TOOL':
        return "Use the {:s} tool".format(item_name(context, item))
    if item_type == 'MENU':
        return "Open the {:s} menu".format(item_name(context, item))
    return ""


def _operator_kwargs(func, properties):
    rna_props = func.get_rna_type().properties
    kwargs = {}
    for key, value in (properties or {}).items():
        prop = rna_props.get(key)
        if prop is None:
            continue
        if prop.type == 'ENUM' and prop.is_enum_flag and isinstance(value, list):
            value = set(value)
        kwargs[key] = value
    return kwargs


def _tool_active(context, item):
    try:
        tool = context.workspace.tools.from_space_view3d_mode(context.mode, create=False)
    except (AttributeError, TypeError):
        return False
    return tool is not None and tool.idname == item.get("tool")


def run_item(context, item, call_context):
    """Run the item in its editor. Returns an error message, or an empty string."""
    item_type = item.get("type", 'OPERATOR')
    space_type = item.get("editor") or 'VIEW_3D'
    window, area = _find_area(context, space_type)
    if area is None and space_type != 'VIEW_3D':
        window, area = _find_area(context, 'VIEW_3D')
    override = {}
    if area is not None:
        region = next((r for r in area.regions if r.type == 'WINDOW'), None)
        override = {"window": window, "screen": window.screen, "area": area, "region": region}

    if item_type == 'OPERATOR':
        func = _operator(item.get("operator"))
        if func is None:
            return "Unknown operator {!r}".format(item.get("operator", ""))
        # Pushes an undo step and registers the operator, so Adjust Last Operation works.
        args = (call_context, True)
        kwargs = _operator_kwargs(func, item.get("properties"))
    elif item_type == 'TOOL':
        func = bpy.ops.wm.tool_set_by_id
        args = ()
        kwargs = {"name": item.get("tool", "")}
        if area is not None and area.type in {'VIEW_3D', 'IMAGE_EDITOR', 'NODE_EDITOR',
                                              'SEQUENCE_EDITOR'}:
            kwargs["space_type"] = area.type
    elif item_type == 'MENU':
        func = bpy.ops.wm.call_menu_pie if item.get("pie") else bpy.ops.wm.call_menu
        args = ()
        kwargs = {"name": item.get("menu", "")}
    else:
        return ""

    try:
        with context.temp_override(**override):
            func(*args, **kwargs)
    except (RuntimeError, TypeError) as ex:
        message = str(ex).strip()
        if message.startswith("Error: "):
            message = message[len("Error: "):]
        return "{:s}: {:s}".format(item_name(context, item), message)
    return ""


def _properties_dict(op_props):
    result = {}
    for prop in op_props.bl_rna.properties:
        key = prop.identifier
        if key == "rna_type" or prop.type in {'POINTER', 'COLLECTION'}:
            continue
        if not op_props.is_property_set(key):
            continue
        value = getattr(op_props, key)
        if prop.type == 'ENUM' and prop.is_enum_flag:
            value = sorted(value)
        elif prop.type in {'BOOLEAN', 'INT', 'FLOAT'} and getattr(prop, "is_array", False):
            value = [list(v) if hasattr(v, "__len__") else v for v in value]
        result[key] = value
    return result


def _property_location(context, ptr):
    """``(root, path)`` to find the owner of a property again, or None."""
    id_data = ptr.id_data
    if id_data is not None:
        roots = (
            ("scene", context.scene),
            ("object", context.object),
            ("object_data", context.object.data if context.object else None),
        )
        for root, root_id in roots:
            if root_id is not None and id_data == root_id:
                try:
                    path = "" if ptr == root_id else ptr.path_from_id()
                except ValueError:
                    return None
                return root, path
        return None
    space = context.space_data
    if space is not None and space.type == 'VIEW_3D':
        for path in ("", "overlay", "shading"):
            if ptr == (space.path_resolve(path) if path else space):
                return "view3d", path
    prefs = context.preferences
    for path in ("edit", "view", "inputs", "system", "filepaths"):
        if ptr == getattr(prefs, path):
            return "preferences", path
    return None


def item_from_button(context):
    """A new item for the button the context menu is open for, or None."""
    op_props = getattr(context, "button_operator", None)
    if op_props is not None:
        idname = _python_idname(op_props.bl_rna.identifier)
        if idname == "toolbox.run":
            return None
        if idname == "wm.tool_set_by_id":
            return {"type": 'TOOL', "tool": op_props.name, "name": "", "icon": 'NONE'}
        if idname in {"wm.call_menu", "wm.call_menu_pie"}:
            return {"type": 'MENU', "menu": op_props.name, "name": "", "icon": 'NONE',
                    "pie": idname == "wm.call_menu_pie"}
        area = context.area
        item = {
            "type": 'OPERATOR',
            "operator": idname,
            "name": "",
            "icon": _known_icon(idname),
            "editor": area.type if area and area.type != 'TOOLBOX' else 'VIEW_3D',
        }
        properties = _properties_dict(op_props)
        if properties:
            item["properties"] = properties
        return item

    ptr = getattr(context, "button_pointer", None)
    prop = getattr(context, "button_prop", None)
    if ptr is None or prop is None:
        return None
    location = _property_location(context, ptr)
    if location is None:
        return None
    return {"type": 'PROPERTY', "root": location[0], "path": location[1],
            "property": prop.identifier, "name": "", "icon": 'NONE'}


_default_data = None


def _default():
    global _default_data
    if _default_data is None:
        _default_data = _read(default_path()) or _empty()
    return _default_data


def _known_icon(idname):
    """The icon another button of this operator has in the toolbox."""
    sources = (data(), _default())
    for source in sources:
        for tab in source["tabs"]:
            for sec in tab["sections"]:
                for item in sec["items"]:
                    if item.get("operator") == idname and _icon(item.get("icon")) != 'NONE':
                        return item["icon"]
    return 'NONE'


def _find_property_item(context):
    """``(tab, section, item)`` of the toolbox property the context menu is open for."""
    ptr = getattr(context, "button_pointer", None)
    prop = getattr(context, "button_prop", None)
    if ptr is None or prop is None:
        return None
    for ti, tab in enumerate(tabs()):
        for si, sec in enumerate(tab["sections"]):
            for ii, item in enumerate(sec["items"]):
                if item.get("type") != 'PROPERTY' or item.get("property") != prop.identifier:
                    continue
                if _property_owner(context, item) == ptr:
                    return ti, si, ii
    return None


# -----------------------------------------------------------------------------
# Drawing

def _set_location(props, tab, section=-1, item=-1):
    props.tab = tab
    if hasattr(props, "section"):
        props.section = section
    if hasattr(props, "item"):
        props.item = item


def _draw_item(context, layout, item, location, icons_only):
    item_type = item.get("type", 'OPERATOR')
    icon = _icon(item.get("icon"))
    name = item_name(context, item)
    text = "" if (icons_only and icon != 'NONE') else name

    if item_type == 'PROPERTY':
        owner = _property_owner(context, item)
        if owner is None:
            layout.label(text=name, icon='ERROR')
            return
        if icons_only and icon != 'NONE':
            layout.prop(owner, item["property"], text="", icon=icon, toggle=1)
        elif icon != 'NONE':
            layout.prop(owner, item["property"], text=name, icon=icon)
        else:
            layout.prop(owner, item["property"], text=name)
        return

    depress = item_type == 'TOOL' and _tool_active(context, item)
    props = layout.operator("toolbox.run", text=text, icon=icon, depress=depress)
    _set_location(props, *location)


def _draw_section_edit(layout, tab_index, si, sec):
    row = layout.row(align=True)
    props = row.operator(
        "toolbox.section_style", text="",
        icon='LINENUMBERS_ON' if sec["style"] == 'ICONS' else 'IMGDISPLAY',
    )
    _set_location(props, tab_index, si)
    props = row.operator("toolbox.section_rename", text="", icon='SMALL_CAPS')
    _set_location(props, tab_index, si)
    props = row.operator("toolbox.section_move", text="", icon='TRIA_UP')
    _set_location(props, tab_index, si)
    props.offset = -1
    props = row.operator("toolbox.section_move", text="", icon='TRIA_DOWN')
    _set_location(props, tab_index, si)
    props.offset = 1
    props = row.operator("toolbox.section_remove", text="", icon='X')
    _set_location(props, tab_index, si)


def _draw_separator(layout, location, edit):
    """A line. While editing, a row with the buttons to move and remove it."""
    if not edit:
        layout.separator(type='LINE')
        return
    row = layout.row(align=True)
    row.label(text="Separator", icon='REMOVE')
    props = row.operator("toolbox.item_move", text="", icon='TRIA_UP')
    _set_location(props, *location)
    props.offset = -1
    props = row.operator("toolbox.item_move", text="", icon='TRIA_DOWN')
    _set_location(props, *location)
    props.offset = 1
    props = row.operator("toolbox.item_remove", text="", icon='X')
    _set_location(props, *location)


def _icon_columns(context, scale):
    """The number of buttons of `scale` units that fit a row of the panel."""
    region = context.region
    unit = 20.0 * context.preferences.system.ui_scale
    # The tab column on the right, the panel's margins and the scroll-bar.
    width = region.width - unit * 3.0
    return max(1, int(width // (unit * scale)))


def _draw_section_body(context, layout, tab_index, si, sec, scale, edit):
    items = sec["items"]
    if not items:
        layout.label(text="Right-click any button and choose Add to Toolbox")
        return

    if sec["style"] == 'ICONS':
        # Centered rows of as many square buttons as fit the width, touching like the tool-bar's.
        columns = _icon_columns(context, scale)
        col = row = None
        used = 0
        for ii, item in enumerate(items):
            if item.get("type") == 'SEPARATOR':
                col = row = None
                _draw_separator(layout, (tab_index, si, ii), edit)
                continue
            if col is None:
                col = layout.column(align=True)
                col.scale_x = scale
                col.scale_y = scale
            if row is None or used == columns:
                row = col.row(align=True)
                row.alignment = 'CENTER'
                used = 0
            _draw_item(context, row, item, (tab_index, si, ii), icons_only=True)
            used += 1
        return

    col = layout.column(align=True)
    for ii, item in enumerate(items):
        if item.get("type") == 'SEPARATOR':
            _draw_separator(layout, (tab_index, si, ii), edit)
            col = layout.column(align=True)
            continue
        row = col.row(align=True)
        if item.get("type") != 'PROPERTY' and _icon(item.get("icon")) != 'NONE':
            row.scale_y = scale
        _draw_item(context, row, item, (tab_index, si, ii), icons_only=False)


class ToolboxSectionPanel:
    """The panel of a section, a class of it is made for each section, see
    :func:`register_tabs`. The tab is the panel category."""
    bl_space_type = 'TOOLBOX'
    bl_region_type = 'WINDOW'
    tab_index = 0
    section_index = 0

    def draw_header_preset(self, context):
        sec = get_section(self.tab_index, self.section_index)
        if sec is not None and context.space_data.show_edit:
            _draw_section_edit(self.layout, self.tab_index, self.section_index, sec)

    def draw(self, context):
        sec = get_section(self.tab_index, self.section_index)
        if sec is None:
            return
        space = context.space_data
        _draw_section_body(
            context, self.layout, self.tab_index, self.section_index, sec, space.icon_scale,
            space.show_edit,
        )


class ToolboxTabEndPanel:
    """At the end of each tab: the button to add a section, while editing or when it's empty."""
    bl_space_type = 'TOOLBOX'
    bl_region_type = 'WINDOW'
    bl_options = {'HIDE_HEADER'}
    bl_order = 100000
    tab_index = 0

    @classmethod
    def poll(cls, context):
        tab = get_tab(cls.tab_index)
        return tab is not None and (not tab["sections"] or context.space_data.show_edit)

    def draw(self, _context):
        layout = self.layout
        if not get_tab(self.tab_index)["sections"]:
            layout.label(text="This tab is empty")
        props = layout.operator("toolbox.section_add", text="Add Section", icon='ADD')
        props.tab = self.tab_index


def unregister_tabs():
    for cls in reversed(_tab_panels):
        if cls.is_registered:
            bpy.utils.unregister_class(cls)
    _tab_panels.clear()
    menu = getattr(bpy.types, "UI_MT_button_context_menu", None)
    if menu is not None:
        menu.remove(draw_button_context_menu)


def register_tabs():
    """(Re)register the panels of the tabs and sections, after they changed."""
    unregister_tabs()
    if not supported():
        return
    menu = getattr(bpy.types, "UI_MT_button_context_menu", None)
    if menu is not None:
        menu.append(draw_button_context_menu)
    for ti, tab in enumerate(tabs()):
        classes = []
        for si, sec in enumerate(tab["sections"]):
            classes.append(type(
                "TOOLBOX_PT_tab{:d}_section{:d}".format(ti, si),
                (ToolboxSectionPanel, Panel),
                {"bl_label": sec["name"], "bl_category": tab["name"], "bl_order": si,
                 "tab_index": ti, "section_index": si},
            ))
        classes.append(type(
            "TOOLBOX_PT_tab{:d}_end".format(ti),
            (ToolboxTabEndPanel, Panel),
            {"bl_label": tab["name"], "bl_category": tab["name"], "tab_index": ti},
        ))
        for cls in classes:
            bpy.utils.register_class(cls)
            _tab_panels.append(cls)
    _redraw()


# -----------------------------------------------------------------------------
# Header & Menus

class TOOLBOX_HT_header(Header):
    bl_space_type = 'TOOLBOX'

    def draw(self, context):
        layout = self.layout
        space = context.space_data
        layout.template_header()
        layout.menu("TOOLBOX_MT_context_menu", text="Toolbox")
        layout.separator_spacer()
        layout.prop(space, "show_edit", text="Edit", toggle=True)


class TOOLBOX_MT_context_menu(Menu):
    bl_label = "Toolbox"

    def draw(self, context):
        layout = self.layout
        space = context.space_data
        tab = active_tab_index(context)

        if space is not None and space.type == 'TOOLBOX':
            layout.prop(space, "show_edit", text="Edit Sections")
            layout.prop(space, "icon_scale")
            layout.separator()
        if tab is not None:
            props = layout.operator("toolbox.section_add", text="Add Section...", icon='ADD')
            props.tab = tab
        layout.operator("toolbox.tab_add", text="Add Tab...", icon='ADD')
        if tab is not None:
            props = layout.operator("toolbox.tab_rename", text="Rename Tab...")
            props.tab = tab
            props = layout.operator("toolbox.tab_move", text="Move Tab Up", icon='TRIA_UP')
            props.tab = tab
            props.offset = -1
            props = layout.operator("toolbox.tab_move", text="Move Tab Down", icon='TRIA_DOWN')
            props.tab = tab
            props.offset = 1
            props = layout.operator("toolbox.tab_remove", text="Remove Tab", icon='X')
            props.tab = tab
        layout.separator()
        if missing_default_tabs():
            layout.menu("TOOLBOX_MT_restore_tab")
        layout.operator("toolbox.reset", text="Reset to Default")


def missing_default_tabs():
    """Names of the default tabs that aren't in the toolbox."""
    names = {tab["name"] for tab in tabs()}
    return [tab["name"] for tab in _default()["tabs"] if tab["name"] not in names]


class TOOLBOX_MT_restore_tab(Menu):
    bl_label = "Restore Default Tab"

    def draw(self, _context):
        layout = self.layout
        for name in missing_default_tabs():
            layout.operator("toolbox.tab_restore", text=name).name = name


def _draw_item_edit(layout, location, item):
    props = layout.operator("toolbox.item_rename", text="Rename...", icon='SMALL_CAPS')
    _set_location(props, *location)
    props = layout.operator("toolbox.pick_icon", text="Change Icon...", icon='IMAGE_DATA')
    _set_location(props, *location)
    props = layout.operator("toolbox.item_move", text="Move Up", icon='TRIA_UP')
    _set_location(props, *location)
    props.offset = -1
    props = layout.operator("toolbox.item_move", text="Move Down", icon='TRIA_DOWN')
    _set_location(props, *location)
    props.offset = 1
    layout.operator_menu_enum(
        "toolbox.item_move_to_section", "section_name", text="Move to Section",
    )
    tab, section, index = location
    after = get_item(tab, section, index + 1)
    if after is not None and after.get("type") == 'SEPARATOR':
        props = layout.operator("toolbox.item_remove", text="Remove Separator After")
        _set_location(props, tab, section, index + 1)
    else:
        props = layout.operator("toolbox.item_add_separator", text="Add Separator After")
        _set_location(props, *location)
    props = layout.operator("toolbox.item_remove", text="Remove from Toolbox", icon='X')
    _set_location(props, *location)


def draw_button_context_menu(self, context):
    """Added to the context menu of all buttons."""
    layout = self.layout
    space = context.space_data
    if space is not None and space.type == 'TOOLBOX':
        location = None
        op_props = getattr(context, "button_operator", None)
        if op_props is not None and op_props.bl_rna.identifier == "TOOLBOX_OT_run":
            location = (op_props.tab, op_props.section, op_props.item)
        else:
            location = _find_property_item(context)
        if location is None or get_item(*location) is None:
            return
        layout.separator()
        # The "Move to Section" menu needs to know the item.
        TOOLBOX_OT_item_move_to_section.location = location
        _draw_item_edit(layout, location, get_item(*location))
        return

    item = item_from_button(context)
    if item is None:
        return
    layout.separator()
    props = layout.operator("toolbox.add_item", text="Add to Toolbox...", icon='ADD')
    props.item_json = json.dumps(item)


# -----------------------------------------------------------------------------
# Operators

class _Location:
    tab: IntProperty(name="Tab", options={'HIDDEN', 'SKIP_SAVE'})
    section: IntProperty(name="Section", options={'HIDDEN', 'SKIP_SAVE'})
    item: IntProperty(name="Item", options={'HIDDEN', 'SKIP_SAVE'})


class TOOLBOX_OT_run(_Location, Operator):
    bl_idname = "toolbox.run"
    bl_label = "Run Toolbox Button"
    bl_options = {'INTERNAL'}

    @classmethod
    def description(cls, context, properties):
        item = get_item(properties.tab, properties.section, properties.item)
        return item_description(context, item) if item else ""

    @classmethod
    def get_label(cls, properties):
        """The tool-tip shows the button's own name instead of this operator's.
        (Builds without ``Operator.get_label`` ignore it.)"""
        item = get_item(properties.tab, properties.section, properties.item)
        return item_name(bpy.context, item) if item else ""

    def _run(self, context, call_context):
        item = get_item(self.tab, self.section, self.item)
        if item is None:
            return {'CANCELLED'}
        error = run_item(context, item, call_context)
        if error:
            self.report({'WARNING'}, error)
            return {'CANCELLED'}
        return {'FINISHED'}

    def invoke(self, context, _event):
        return self._run(context, 'INVOKE_DEFAULT')

    def execute(self, context):
        return self._run(context, 'EXEC_DEFAULT')


def _tab_items(_self, _context):
    items = [(str(i), tab["name"], "") for i, tab in enumerate(tabs())]
    _tab_items.cache = items
    return items


def _section_items(self, _context):
    items = []
    tab = get_tab(int(self.tab_choice)) if self.tab_choice else None
    if tab is not None:
        items = [(str(i), sec["name"], "") for i, sec in enumerate(tab["sections"])]
    items.append(('NEW', "New Section", "Add a new section for the button"))
    _section_items.cache = items
    return items


class TOOLBOX_OT_add_item(Operator):
    """Add this button to a section of the Toolbox editor"""
    bl_idname = "toolbox.add_item"
    bl_label = "Add to Toolbox"
    bl_options = {'INTERNAL'}

    item_json: StringProperty(options={'HIDDEN', 'SKIP_SAVE'})
    tab_choice: EnumProperty(name="Tab", items=_tab_items)
    section_choice: EnumProperty(name="Section", items=_section_items)
    new_section: StringProperty(name="Name", default="New Section")
    name: StringProperty(name="Button Name", description="Leave empty to use the usual name")

    def invoke(self, context, _event):
        return context.window_manager.invoke_props_dialog(self, width=320)

    def draw(self, _context):
        layout = self.layout
        layout.use_property_split = True
        layout.use_property_decorate = False
        layout.prop(self, "tab_choice")
        layout.prop(self, "section_choice")
        if self.section_choice == 'NEW':
            layout.prop(self, "new_section")
        layout.prop(self, "name")

    def execute(self, _context):
        try:
            item = json.loads(self.item_json)
        except ValueError:
            return {'CANCELLED'}
        tab = get_tab(int(self.tab_choice)) if self.tab_choice else None
        if tab is None:
            return {'CANCELLED'}
        if self.name.strip():
            item["name"] = self.name.strip()
        if self.section_choice == 'NEW':
            sec = {"name": self.new_section.strip() or "Section", "style": 'LIST', "items": []}
            tab["sections"].append(sec)
        else:
            sec = tab["sections"][int(self.section_choice)]
        sec["items"].append(item)
        save()
        register_tabs()
        return {'FINISHED'}


class TOOLBOX_OT_item_remove(_Location, Operator):
    """Remove this button from the Toolbox"""
    bl_idname = "toolbox.item_remove"
    bl_label = "Remove from Toolbox"
    bl_options = {'INTERNAL'}

    def execute(self, _context):
        sec = get_section(self.tab, self.section)
        if sec is None or not (0 <= self.item < len(sec["items"])):
            return {'CANCELLED'}
        del sec["items"][self.item]
        save()
        return {'FINISHED'}


class TOOLBOX_OT_item_move(_Location, Operator):
    """Move this button in its section"""
    bl_idname = "toolbox.item_move"
    bl_label = "Move Button"
    bl_options = {'INTERNAL'}

    offset: IntProperty(options={'SKIP_SAVE'})

    def execute(self, _context):
        sec = get_section(self.tab, self.section)
        if sec is None or not _move(sec["items"], self.item, self.offset):
            return {'CANCELLED'}
        save()
        return {'FINISHED'}


def _move_section_items(_self, _context):
    location = TOOLBOX_OT_item_move_to_section.location
    tab = get_tab(location[0]) if location else None
    items = []
    if tab is not None:
        items = [(str(i), sec["name"], "") for i, sec in enumerate(tab["sections"])
                 if i != location[1]]
    if not items:
        items = [('NONE', "No Other Sections", "")]
    _move_section_items.cache = items
    return items


class TOOLBOX_OT_item_move_to_section(Operator):
    """Move this button to another section of its tab"""
    bl_idname = "toolbox.item_move_to_section"
    bl_label = "Move to Section"
    bl_options = {'INTERNAL'}

    # The item of the context menu, set when the menu is drawn.
    location = None

    section_name: EnumProperty(name="Section", items=_move_section_items)

    def execute(self, _context):
        location = TOOLBOX_OT_item_move_to_section.location
        if location is None or self.section_name == 'NONE':
            return {'CANCELLED'}
        sec = get_section(location[0], location[1])
        target = get_section(location[0], int(self.section_name))
        if sec is None or target is None or not (0 <= location[2] < len(sec["items"])):
            return {'CANCELLED'}
        target["items"].append(sec["items"].pop(location[2]))
        save()
        return {'FINISHED'}


class TOOLBOX_OT_item_add_separator(_Location, Operator):
    """Add a separator line after this button"""
    bl_idname = "toolbox.item_add_separator"
    bl_label = "Add Separator"
    bl_options = {'INTERNAL'}

    def execute(self, _context):
        sec = get_section(self.tab, self.section)
        if sec is None:
            return {'CANCELLED'}
        sec["items"].insert(self.item + 1, {"type": 'SEPARATOR'})
        save()
        return {'FINISHED'}


class TOOLBOX_OT_item_rename(_Location, Operator):
    """Change the name of this button"""
    bl_idname = "toolbox.item_rename"
    bl_label = "Rename Button"
    bl_options = {'INTERNAL'}

    name: StringProperty(name="Name", description="Leave empty to use the usual name")

    def invoke(self, context, _event):
        item = get_item(self.tab, self.section, self.item)
        if item is None:
            return {'CANCELLED'}
        self.name = item_name(context, item)
        return context.window_manager.invoke_props_dialog(self)

    def execute(self, _context):
        item = get_item(self.tab, self.section, self.item)
        if item is None:
            return {'CANCELLED'}
        item["name"] = self.name.strip()
        save()
        return {'FINISHED'}


class TOOLBOX_OT_item_set_icon(_Location, Operator):
    bl_idname = "toolbox.item_set_icon"
    bl_label = "Set Icon"
    bl_options = {'INTERNAL'}

    icon: StringProperty(options={'SKIP_SAVE'})

    def execute(self, _context):
        item = get_item(self.tab, self.section, self.item)
        if item is None:
            return {'CANCELLED'}
        item["icon"] = self.icon
        save()
        return {'FINISHED'}


class TOOLBOX_OT_pick_icon(_Location, Operator):
    """Pick another icon for this button"""
    bl_idname = "toolbox.pick_icon"
    bl_label = "Change Icon"
    bl_options = {'INTERNAL'}

    filter: StringProperty(
        name="Filter",
        description="Only show the icons with this in their name",
        options={'TEXTEDIT_UPDATE', 'SKIP_SAVE'},
    )

    COLUMNS = 24
    MAX_SHOWN = COLUMNS * 20

    def invoke(self, context, _event):
        return context.window_manager.invoke_popup(self, width=self.COLUMNS * 26)

    def execute(self, _context):
        return {'FINISHED'}

    def _icon_button(self, layout, text, icon):
        props = layout.operator("toolbox.item_set_icon", text=text, icon=icon)
        _set_location(props, self.tab, self.section, self.item)
        props.icon = icon

    def draw(self, _context):
        layout = self.layout
        layout.operator_context = 'EXEC_DEFAULT'
        row = layout.row()
        row.prop(self, "filter", text="", icon='VIEWZOOM')
        self._icon_button(row, "No Icon", 'NONE')

        text = self.filter.strip().upper().replace(" ", "_")
        icons = [name for name in icon_names() if text in name]
        if not icons:
            layout.label(text="No icons match")
            return
        grid = layout.grid_flow(
            row_major=True, columns=self.COLUMNS, even_columns=True, even_rows=True, align=True,
        )
        for name in icons[:self.MAX_SHOWN]:
            self._icon_button(grid, "", name)
        hidden = len(icons) - self.MAX_SHOWN
        if hidden > 0:
            layout.label(
                text="{:d} more, type in the filter to find them".format(hidden), icon='INFO',
            )


class TOOLBOX_OT_section_add(Operator):
    """Add a section to this tab"""
    bl_idname = "toolbox.section_add"
    bl_label = "Add Section"
    bl_options = {'INTERNAL'}

    tab: IntProperty(options={'HIDDEN', 'SKIP_SAVE'})
    name: StringProperty(name="Name", default="New Section")
    style: EnumProperty(name="Style", items=STYLES)

    def invoke(self, context, _event):
        return context.window_manager.invoke_props_dialog(self)

    def execute(self, _context):
        tab = get_tab(self.tab)
        if tab is None:
            return {'CANCELLED'}
        tab["sections"].append({
            "name": self.name.strip() or "Section", "style": self.style, "items": [],
        })
        save()
        register_tabs()
        return {'FINISHED'}


class TOOLBOX_OT_section_remove(_Location, Operator):
    """Remove this section and its buttons"""
    bl_idname = "toolbox.section_remove"
    bl_label = "Remove Section"
    bl_options = {'INTERNAL'}

    def invoke(self, context, event):
        sec = get_section(self.tab, self.section)
        if sec is None:
            return {'CANCELLED'}
        if sec["items"]:
            return context.window_manager.invoke_confirm(
                self, event, title="Remove Section",
                message="Remove {!r} and its buttons?".format(sec["name"]),
                confirm_text="Remove",
            )
        return self.execute(context)

    def execute(self, _context):
        tab = get_tab(self.tab)
        if tab is None or not (0 <= self.section < len(tab["sections"])):
            return {'CANCELLED'}
        del tab["sections"][self.section]
        save()
        register_tabs()
        return {'FINISHED'}


class TOOLBOX_OT_section_rename(_Location, Operator):
    """Rename this section"""
    bl_idname = "toolbox.section_rename"
    bl_label = "Rename Section"
    bl_options = {'INTERNAL'}

    name: StringProperty(name="Name")

    def invoke(self, context, _event):
        sec = get_section(self.tab, self.section)
        if sec is None:
            return {'CANCELLED'}
        self.name = sec["name"]
        return context.window_manager.invoke_props_dialog(self)

    def execute(self, _context):
        sec = get_section(self.tab, self.section)
        if sec is None or not self.name.strip():
            return {'CANCELLED'}
        sec["name"] = self.name.strip()
        save()
        register_tabs()
        return {'FINISHED'}


class TOOLBOX_OT_section_move(_Location, Operator):
    """Move this section up or down"""
    bl_idname = "toolbox.section_move"
    bl_label = "Move Section"
    bl_options = {'INTERNAL'}

    offset: IntProperty(options={'SKIP_SAVE'})

    def execute(self, _context):
        tab = get_tab(self.tab)
        if tab is None or not _move(tab["sections"], self.section, self.offset):
            return {'CANCELLED'}
        save()
        register_tabs()
        return {'FINISHED'}


class TOOLBOX_OT_section_style(_Location, Operator):
    """Show the buttons of this section as a grid of icons, or as a list with their names"""
    bl_idname = "toolbox.section_style"
    bl_label = "Toggle Icons/List"
    bl_options = {'INTERNAL'}

    def execute(self, _context):
        sec = get_section(self.tab, self.section)
        if sec is None:
            return {'CANCELLED'}
        sec["style"] = 'LIST' if sec["style"] == 'ICONS' else 'ICONS'
        save()
        return {'FINISHED'}


class TOOLBOX_OT_tab_add(Operator):
    """Add a tab to the Toolbox"""
    bl_idname = "toolbox.tab_add"
    bl_label = "Add Tab"
    bl_options = {'INTERNAL'}

    name: StringProperty(name="Name", default="New Tab")

    def invoke(self, context, _event):
        return context.window_manager.invoke_props_dialog(self)

    def execute(self, _context):
        tabs().append({"name": _unique_tab_name(self.name), "sections": []})
        save()
        register_tabs()
        return {'FINISHED'}


class TOOLBOX_OT_tab_remove(Operator):
    """Remove this tab and everything in it"""
    bl_idname = "toolbox.tab_remove"
    bl_label = "Remove Tab"
    bl_options = {'INTERNAL'}

    tab: IntProperty(options={'HIDDEN', 'SKIP_SAVE'})

    @classmethod
    def poll(cls, _context):
        return len(tabs()) > 1

    def invoke(self, context, event):
        tab = get_tab(self.tab)
        if tab is None:
            return {'CANCELLED'}
        return context.window_manager.invoke_confirm(
            self, event, title="Remove Tab",
            message="Remove the {!r} tab and everything in it?".format(tab["name"]),
            confirm_text="Remove", icon='WARNING',
        )

    def execute(self, _context):
        if get_tab(self.tab) is None or len(tabs()) < 2:
            return {'CANCELLED'}
        del tabs()[self.tab]
        save()
        register_tabs()
        return {'FINISHED'}


class TOOLBOX_OT_tab_rename(Operator):
    """Rename this tab"""
    bl_idname = "toolbox.tab_rename"
    bl_label = "Rename Tab"
    bl_options = {'INTERNAL'}

    tab: IntProperty(options={'HIDDEN', 'SKIP_SAVE'})
    name: StringProperty(name="Name")

    def invoke(self, context, _event):
        tab = get_tab(self.tab)
        if tab is None:
            return {'CANCELLED'}
        self.name = tab["name"]
        return context.window_manager.invoke_props_dialog(self)

    def execute(self, _context):
        tab = get_tab(self.tab)
        if tab is None or not self.name.strip():
            return {'CANCELLED'}
        tab["name"] = _unique_tab_name(self.name, skip=self.tab)
        save()
        register_tabs()
        return {'FINISHED'}


class TOOLBOX_OT_tab_move(Operator):
    """Move this tab up or down"""
    bl_idname = "toolbox.tab_move"
    bl_label = "Move Tab"
    bl_options = {'INTERNAL'}

    tab: IntProperty(options={'HIDDEN', 'SKIP_SAVE'})
    offset: IntProperty(options={'SKIP_SAVE'})

    def execute(self, _context):
        if not _move(tabs(), self.tab, self.offset):
            return {'CANCELLED'}
        save()
        register_tabs()
        return {'FINISHED'}


class TOOLBOX_OT_tab_restore(Operator):
    """Bring back a default tab that was removed"""
    bl_idname = "toolbox.tab_restore"
    bl_label = "Restore Default Tab"
    bl_options = {'INTERNAL'}

    name: StringProperty(options={'HIDDEN', 'SKIP_SAVE'})

    def execute(self, _context):
        import copy
        default_tabs = _default()["tabs"]
        tab = next((t for t in default_tabs if t["name"] == self.name), None)
        if tab is None or any(t["name"] == self.name for t in tabs()):
            return {'CANCELLED'}
        # Put it after the default tabs that come before it.
        before = {t["name"] for t in default_tabs[:default_tabs.index(tab)]}
        index = 0
        for i, t in enumerate(tabs()):
            if t["name"] in before:
                index = i + 1
        tabs().insert(index, copy.deepcopy(tab))
        save()
        register_tabs()
        return {'FINISHED'}


class TOOLBOX_OT_reset(Operator):
    """Replace everything in the Toolbox with the default tabs and buttons"""
    bl_idname = "toolbox.reset"
    bl_label = "Reset Toolbox to Default"
    bl_options = {'INTERNAL'}

    def invoke(self, context, event):
        return context.window_manager.invoke_confirm(
            self, event, title="Reset Toolbox",
            message="Replace all tabs and buttons with the default ones?",
            confirm_text="Reset",
        )

    def execute(self, _context):
        reset()
        return {'FINISHED'}


def supported():
    """False in builds without the Toolbox editor, which share these scripts."""
    return 'TOOLBOX' in bpy.types.Space.bl_rna.properties["type"].enum_items.keys()


classes = () if not supported() else (
    TOOLBOX_HT_header,
    TOOLBOX_MT_context_menu,
    TOOLBOX_MT_restore_tab,
    TOOLBOX_OT_run,
    TOOLBOX_OT_add_item,
    TOOLBOX_OT_item_remove,
    TOOLBOX_OT_item_move,
    TOOLBOX_OT_item_move_to_section,
    TOOLBOX_OT_item_add_separator,
    TOOLBOX_OT_item_rename,
    TOOLBOX_OT_item_set_icon,
    TOOLBOX_OT_pick_icon,
    TOOLBOX_OT_section_add,
    TOOLBOX_OT_section_remove,
    TOOLBOX_OT_section_rename,
    TOOLBOX_OT_section_move,
    TOOLBOX_OT_section_style,
    TOOLBOX_OT_tab_add,
    TOOLBOX_OT_tab_remove,
    TOOLBOX_OT_tab_rename,
    TOOLBOX_OT_tab_move,
    TOOLBOX_OT_tab_restore,
    TOOLBOX_OT_reset,
)

if __name__ == "__main__":  # only for live edit.
    from bpy.utils import register_class
    for cls in classes:
        register_class(cls)
    register_tabs()
