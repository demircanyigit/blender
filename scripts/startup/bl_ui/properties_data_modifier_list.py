# SPDX-FileCopyrightText: 2026 Blender Authors
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""The list view of the Modifiers tab: favourites, a modifier menu that adds after the active
modifier, the stack as a list and the settings of the active modifier only."""

import json
import os

import bpy
from bpy.types import Menu, Operator, UIList
from bpy.props import BoolProperty, EnumProperty, IntProperty, StringProperty

from bl_ui.properties_data_modifier import (
    ModifierAddMenu,
    OBJECT_MT_modifier_add_edit,
    OBJECT_MT_modifier_add_generate,
    OBJECT_MT_modifier_add_deform,
    OBJECT_MT_modifier_add_normals,
    OBJECT_MT_modifier_add_physics,
    OBJECT_MT_modifier_add_color,
)

LIST_ID = "MODIFIER_UL_modifier_list"
DEFAULT_FAVORITES = ['SUBSURF', 'MIRROR', 'BEVEL', 'SOLIDIFY', 'ARRAY', 'BOOLEAN']

# Copied modifier: (object name, modifier name).
_clipboard = None


# -----------------------------------------------------------------------------
# Helpers

def _favorites_path():
    return os.path.join(bpy.utils.user_resource('CONFIG'), "modifier_favorites.json")


_favorites_cache = None
_columns_cache = None
DEFAULT_COLUMNS = 3


def _favorites_load():
    global _favorites_cache, _columns_cache
    _favorites_cache = list(DEFAULT_FAVORITES)
    _columns_cache = DEFAULT_COLUMNS
    try:
        with open(_favorites_path(), encoding="utf-8") as fh:
            data = json.load(fh)
    except (OSError, ValueError):
        return
    # Older files are only the list of favourites.
    if isinstance(data, dict):
        columns = data.get("columns")
        if isinstance(columns, int) and 1 <= columns <= 3:
            _columns_cache = columns
        data = data.get("favorites", _favorites_cache)
    if isinstance(data, list):
        _favorites_cache = [t for t in data if isinstance(t, str)]


def _favorites_save():
    try:
        os.makedirs(os.path.dirname(_favorites_path()), exist_ok=True)
        with open(_favorites_path(), "w", encoding="utf-8") as fh:
            json.dump({"favorites": _favorites_cache, "columns": _columns_cache}, fh, indent=1)
    except OSError:
        pass


def favorites_get():
    if _favorites_cache is None:
        _favorites_load()
    return [t for t in _favorites_cache if t in ModifierAddMenu.MODIFIER_TYPES_TO_LABELS]


def favorites_set(types):
    global _favorites_cache
    if _favorites_cache is None:
        _favorites_load()
    _favorites_cache = list(types)
    _favorites_save()


def favorites_columns_get():
    if _columns_cache is None:
        _favorites_load()
    return _columns_cache


def favorites_columns_set(columns):
    global _columns_cache
    if _favorites_cache is None:
        _favorites_load()
    _columns_cache = max(1, min(3, columns))
    _favorites_save()


def _type_fits_object(mod_type, ob):
    is_gp = mod_type.startswith('GREASE_PENCIL') or mod_type == 'LINEART'
    return is_gp == (ob.type == 'GREASEPENCIL')


def _icon(mod_type):
    return ModifierAddMenu.MODIFIER_TYPES_TO_ICONS.get(mod_type, 'MODIFIER')


def _active(context):
    ob = context.object
    if ob is None:
        return None, None
    return ob, ob.modifiers.active


def _last_on_top(context):
    space = context.space_data
    return bool(getattr(space, "modifier_list_last_on_top", False))


def _view3d_spaces(context):
    for area in context.screen.areas:
        if area.type == 'VIEW_3D':
            yield area.spaces.active


def _object_mode_only(cls, context):
    ob = context.object
    if ob is None or ob.mode != 'OBJECT':
        cls.poll_message_set("Only available in Object Mode")
        return False
    return True


def _move_to(ob, md, index):
    current = list(ob.modifiers).index(md)
    index = max(0, min(index, len(ob.modifiers) - 1))
    if current != index:
        ob.modifiers.move(current, index)


# -----------------------------------------------------------------------------
# Operators

class OBJECT_OT_modifier_list_add(Operator):
    """Add a modifier after the active one"""
    bl_idname = "object.modifier_list_add"
    bl_label = "Add Modifier"
    bl_options = {'REGISTER', 'UNDO', 'INTERNAL'}

    type: StringProperty(options={'SKIP_SAVE'})

    @classmethod
    def description(cls, _context, props):
        return ModifierAddMenu.MODIFIER_TYPES_TO_LABELS.get(props.type, "")

    def execute(self, context):
        ob, active = _active(context)
        if ob is None:
            return {'CANCELLED'}
        index = list(ob.modifiers).index(active) if active else -1
        try:
            result = bpy.ops.object.modifier_add(type=self.type)
        except (RuntimeError, TypeError) as ex:
            self.report({'ERROR'}, str(ex))
            return {'CANCELLED'}
        if 'FINISHED' not in result:
            return {'CANCELLED'}
        new = ob.modifiers.active
        if active is not None and new is not None and new != active:
            _move_to(ob, new, index + 1)
        return {'FINISHED'}


class OBJECT_OT_modifier_list_collapse_to(Operator):
    """Apply every modifier up to and including the active one"""
    bl_idname = "object.modifier_list_collapse_to"
    bl_label = "Collapse To"
    bl_options = {'REGISTER', 'UNDO'}

    @classmethod
    def poll(cls, context):
        ob, md = _active(context)
        return md is not None and _object_mode_only(cls, context)

    def execute(self, context):
        ob, md = _active(context)
        names = []
        for it in ob.modifiers:
            names.append(it.name)
            if it == md:
                break
        for name in names:
            try:
                bpy.ops.object.modifier_apply(modifier=name)
            except RuntimeError as ex:
                self.report({'ERROR'}, str(ex))
                return {'CANCELLED'}
        return {'FINISHED'}


class OBJECT_OT_modifier_list_apply_all(Operator):
    """Apply every modifier of the stack"""
    bl_idname = "object.modifier_list_apply_all"
    bl_label = "Apply All"
    bl_options = {'REGISTER', 'UNDO'}

    @classmethod
    def poll(cls, context):
        ob = context.object
        return ob is not None and len(ob.modifiers) > 0 and _object_mode_only(cls, context)

    def execute(self, context):
        ob = context.object
        for name in [md.name for md in ob.modifiers]:
            try:
                bpy.ops.object.modifier_apply(modifier=name)
            except RuntimeError as ex:
                self.report({'WARNING'}, str(ex))
        return {'FINISHED'}


class OBJECT_OT_modifier_list_delete_all(Operator):
    """Remove every modifier of the stack"""
    bl_idname = "object.modifier_list_delete_all"
    bl_label = "Delete All"
    bl_options = {'REGISTER', 'UNDO'}

    @classmethod
    def poll(cls, context):
        ob = context.object
        return ob is not None and len(ob.modifiers) > 0

    def invoke(self, context, event):
        return context.window_manager.invoke_confirm(self, event)

    def execute(self, context):
        context.object.modifiers.clear()
        return {'FINISHED'}


class OBJECT_OT_modifier_list_enable_all(Operator):
    """Show or hide every modifier in the viewport"""
    bl_idname = "object.modifier_list_enable_all"
    bl_label = "Enable All"
    bl_options = {'REGISTER', 'UNDO'}

    enable: BoolProperty(default=True)

    @classmethod
    def description(cls, _context, props):
        return "Show every modifier in the viewport" if props.enable else \
            "Hide every modifier in the viewport"

    @classmethod
    def poll(cls, context):
        ob = context.object
        return ob is not None and len(ob.modifiers) > 0

    def execute(self, context):
        for md in context.object.modifiers:
            md.show_viewport = self.enable
        return {'FINISHED'}


class OBJECT_OT_modifier_list_toggle(Operator):
    """Show or hide the active modifier in the viewport"""
    bl_idname = "object.modifier_list_toggle"
    bl_label = "Enable/Disable"
    bl_options = {'REGISTER', 'UNDO'}

    @classmethod
    def poll(cls, context):
        return _active(context)[1] is not None

    def execute(self, context):
        md = _active(context)[1]
        md.show_viewport = not md.show_viewport
        return {'FINISHED'}


class OBJECT_OT_modifier_list_move(Operator):
    """Move the active modifier up or down the list"""
    bl_idname = "object.modifier_list_move"
    bl_label = "Move Modifier"
    bl_options = {'REGISTER', 'UNDO', 'INTERNAL'}

    direction: EnumProperty(items=(('UP', "Up", ""), ('DOWN', "Down", "")))

    @classmethod
    def description(cls, _context, props):
        return "Move the active modifier up the list" if props.direction == 'UP' else \
            "Move the active modifier down the list"

    @classmethod
    def poll(cls, context):
        return _active(context)[1] is not None

    def execute(self, context):
        ob, md = _active(context)
        # With the last modifier on top, up in the list is later in the stack.
        earlier = (self.direction == 'UP') != _last_on_top(context)
        index = list(ob.modifiers).index(md)
        _move_to(ob, md, index - 1 if earlier else index + 1)
        return {'FINISHED'}


class OBJECT_OT_modifier_list_copy(Operator):
    """Copy the active modifier"""
    bl_idname = "object.modifier_list_copy"
    bl_label = "Copy"

    @classmethod
    def poll(cls, context):
        return _active(context)[1] is not None

    def execute(self, context):
        global _clipboard
        ob, md = _active(context)
        _clipboard = (ob.name, md.name)
        return {'FINISHED'}


class OBJECT_OT_modifier_list_paste(Operator):
    """Paste the copied modifier after the active one"""
    bl_idname = "object.modifier_list_paste"
    bl_label = "Paste"
    bl_options = {'REGISTER', 'UNDO'}

    @classmethod
    def poll(cls, context):
        if _clipboard is None or context.object is None:
            return False
        src = bpy.data.objects.get(_clipboard[0])
        return src is not None and src.modifiers.get(_clipboard[1]) is not None

    def execute(self, context):
        ob, active = _active(context)
        src = bpy.data.objects[_clipboard[0]]
        index = list(ob.modifiers).index(active) if active else len(ob.modifiers) - 1
        count = len(ob.modifiers)
        if src == ob:
            with context.temp_override(object=ob, active_object=ob):
                bpy.ops.object.modifier_copy(modifier=_clipboard[1])
        else:
            with context.temp_override(
                    object=src,
                    active_object=src,
                    selected_objects=[src, ob],
                    selected_editable_objects=[src, ob],
            ):
                bpy.ops.object.modifier_copy_to_selected(modifier=_clipboard[1])
        if len(ob.modifiers) == count:
            self.report({'WARNING'}, "The modifier could not be pasted")
            return {'CANCELLED'}
        if src == ob:
            new = ob.modifiers.active
        else:
            new = ob.modifiers[-1]
        _move_to(ob, new, index + 1)
        ob.modifiers.active = new
        return {'FINISHED'}


class OBJECT_OT_modifier_list_rename(Operator):
    """Rename the active modifier"""
    bl_idname = "object.modifier_list_rename"
    bl_label = "Rename"
    bl_options = {'REGISTER', 'UNDO'}

    name: StringProperty(name="Name")

    @classmethod
    def poll(cls, context):
        return _active(context)[1] is not None

    def invoke(self, context, _event):
        self.name = _active(context)[1].name
        return context.window_manager.invoke_props_dialog(self)

    def execute(self, context):
        _active(context)[1].name = self.name
        return {'FINISHED'}


class OBJECT_OT_modifier_list_copy_stack(Operator):
    """Copy every modifier of the active object to the selected objects"""
    bl_idname = "object.modifier_list_copy_stack"
    bl_label = "Copy Stack to Selected"
    bl_options = {'REGISTER', 'UNDO'}

    @classmethod
    def poll(cls, context):
        ob = context.object
        return ob is not None and len(context.selected_editable_objects) > 1

    def execute(self, _context):
        return bpy.ops.object.make_links_data(type='MODIFIERS')


class OBJECT_OT_modifier_list_order(Operator):
    """Choose which end of the stack is shown at the top of the list"""
    bl_idname = "object.modifier_list_order"
    bl_label = "List Order"
    bl_options = {'INTERNAL'}

    last_on_top: BoolProperty()

    @classmethod
    def description(cls, _context, props):
        return "Show the last modifier at the top of the list" if props.last_on_top else \
            "Show the first modifier at the top of the list"

    def execute(self, context):
        space = context.space_data
        if space and space.type == 'PROPERTIES':
            space.modifier_list_last_on_top = self.last_on_top
        return {'FINISHED'}


class OBJECT_OT_modifier_list_gizmos(Operator):
    """Show or hide the modifier gizmos in the 3D Viewports"""
    bl_idname = "object.modifier_list_gizmos"
    bl_label = "Modifier Gizmos"
    bl_options = {'INTERNAL'}

    def execute(self, context):
        spaces = list(_view3d_spaces(context))
        if not spaces:
            return {'CANCELLED'}
        show = not spaces[0].show_gizmo_modifier
        for space in spaces:
            space.show_gizmo_modifier = show
        for area in context.screen.areas:
            if area.type == 'VIEW_3D':
                area.tag_redraw()
        return {'FINISHED'}


class OBJECT_OT_modifier_list_edit_base(Operator):
    """Toggle Edit Mode on the base object"""
    bl_idname = "object.modifier_list_edit_base"
    bl_label = "Edit Base"
    bl_options = {'REGISTER', 'UNDO', 'INTERNAL'}

    @classmethod
    def poll(cls, context):
        ob = context.object
        return ob is not None and ob.type in {
            'MESH', 'CURVE', 'SURFACE', 'FONT', 'LATTICE', 'ARMATURE', 'META', 'CURVES',
            'POINTCLOUD', 'GREASEPENCIL',
        }

    def execute(self, context):
        ob = context.object
        if context.view_layer.objects.active != ob:
            context.view_layer.objects.active = ob
        return bpy.ops.object.mode_set(mode='OBJECT' if ob.mode == 'EDIT' else 'EDIT')


class OBJECT_OT_modifier_list_favorite_toggle(Operator):
    """Add or remove this modifier from the favourites"""
    bl_idname = "object.modifier_list_favorite_toggle"
    bl_label = "Favourite"
    bl_options = {'INTERNAL'}

    type: StringProperty()

    def execute(self, _context):
        favorites = favorites_get()
        if self.type in favorites:
            favorites.remove(self.type)
        else:
            favorites.append(self.type)
        favorites_set(favorites)
        return {'FINISHED'}


class OBJECT_OT_modifier_list_favorite_move(Operator):
    """Move this favourite up or down"""
    bl_idname = "object.modifier_list_favorite_move"
    bl_label = "Move Favourite"
    bl_options = {'INTERNAL'}

    type: StringProperty()
    direction: EnumProperty(items=(('UP', "Up", ""), ('DOWN', "Down", ""), ('TOP', "Top", ""),
                                   ('BOTTOM', "Bottom", "")))

    def execute(self, _context):
        favorites = favorites_get()
        if self.type not in favorites:
            return {'CANCELLED'}
        i = favorites.index(self.type)
        favorites.pop(i)
        j = {'UP': i - 1, 'DOWN': i + 1, 'TOP': 0, 'BOTTOM': len(favorites)}[self.direction]
        favorites.insert(max(0, min(len(favorites), j)), self.type)
        favorites_set(favorites)
        return {'FINISHED'}


class OBJECT_OT_modifier_list_favorites_columns(Operator):
    """Number of columns of the favourite buttons"""
    bl_idname = "object.modifier_list_favorites_columns"
    bl_label = "Columns"
    bl_options = {'INTERNAL'}

    columns: IntProperty(min=1, max=3, default=3)

    def execute(self, _context):
        favorites_columns_set(self.columns)
        return {'FINISHED'}


class OBJECT_OT_modifier_list_favorites(Operator):
    """Choose the modifiers shown as buttons above the list, their order and their columns"""
    bl_idname = "object.modifier_list_favorites"
    bl_label = "Favourites"
    bl_options = {'INTERNAL'}

    search: StringProperty(
        name="Search",
        description="Show only the modifiers whose name has this text",
        options={'TEXTEDIT_UPDATE', 'SKIP_SAVE'},
    )

    def invoke(self, context, _event):
        return context.window_manager.invoke_props_dialog(self, width=760, confirm_text="Done")

    def draw(self, context):
        layout = self.layout
        ob = context.object
        labels = ModifierAddMenu.MODIFIER_TYPES_TO_LABELS
        favorites = favorites_get()
        columns = favorites_columns_get()

        split = layout.split(factor=0.36)

        # The favourites in their order, with buttons to move them.
        left = split.column()
        row = left.row(align=True)
        row.label(text="Columns")
        for n in (1, 2, 3):
            row.operator(OBJECT_OT_modifier_list_favorites_columns.bl_idname, text=str(n),
                         depress=n == columns).columns = n
        box = left.box()
        col = box.column(align=True)
        if not favorites:
            col.label(text="Click a modifier to add it")
        for i, mod_type in enumerate(favorites):
            row = col.row(align=True)
            row.label(text=labels[mod_type], icon=_icon(mod_type))
            sub = row.row(align=True)
            sub.enabled = i > 0
            op = sub.operator(OBJECT_OT_modifier_list_favorite_move.bl_idname, text="", icon='TRIA_UP')
            op.type, op.direction = mod_type, 'UP'
            sub = row.row(align=True)
            sub.enabled = i < len(favorites) - 1
            op = sub.operator(OBJECT_OT_modifier_list_favorite_move.bl_idname, text="", icon='TRIA_DOWN')
            op.type, op.direction = mod_type, 'DOWN'
            row.operator(OBJECT_OT_modifier_list_favorite_toggle.bl_idname, text="",
                         icon='X').type = mod_type
        left.operator(OBJECT_OT_modifier_list_favorites_reset.bl_idname, icon='LOOP_BACK')

        # Every modifier, to pick from.
        right = split.column()
        right.prop(self, "search", text="", icon='VIEWZOOM')
        search = self.search.strip().lower()
        types = [t for t in labels if (ob is None or _type_fits_object(t, ob)) and
                 (not search or search in labels[t].lower())]
        types.sort(key=lambda t: labels[t])
        if not types:
            right.label(text="No modifier has that name")
        grid = right.grid_flow(row_major=True, columns=3, even_columns=True, align=True)
        for mod_type in types:
            grid.operator(
                OBJECT_OT_modifier_list_favorite_toggle.bl_idname,
                text=labels[mod_type],
                icon=_icon(mod_type),
                depress=mod_type in favorites,
            ).type = mod_type

    def execute(self, _context):
        return {'FINISHED'}


class OBJECT_OT_modifier_list_favorites_reset(Operator):
    """Go back to the default favourites"""
    bl_idname = "object.modifier_list_favorites_reset"
    bl_label = "Reset to Default"
    bl_options = {'INTERNAL'}

    def execute(self, _context):
        favorites_set(DEFAULT_FAVORITES)
        return {'FINISHED'}


# -----------------------------------------------------------------------------
# Menus

class ModifierListAddMenu(ModifierAddMenu):
    """The category menus of the list view add after the active modifier."""

    @classmethod
    def operator_modifier_add(cls, layout, mod_type, text=None, no_icon=False):
        label = text if text else cls.MODIFIER_TYPES_TO_LABELS[mod_type]
        layout.operator(
            OBJECT_OT_modifier_list_add.bl_idname,
            text=label,
            text_ctxt=cls.MODIFIER_TYPES_I18N_CONTEXT,
            icon='NONE' if no_icon else cls.MODIFIER_TYPES_TO_ICONS[mod_type],
        ).type = mod_type


class OBJECT_MT_modifier_list_edit(ModifierListAddMenu, Menu):
    bl_label = OBJECT_MT_modifier_add_edit.bl_label
    bl_options = {'SEARCH_ON_KEY_PRESS'}
    draw = OBJECT_MT_modifier_add_edit.draw


class OBJECT_MT_modifier_list_generate(ModifierListAddMenu, Menu):
    bl_label = OBJECT_MT_modifier_add_generate.bl_label
    bl_options = {'SEARCH_ON_KEY_PRESS'}
    draw = OBJECT_MT_modifier_add_generate.draw


class OBJECT_MT_modifier_list_deform(ModifierListAddMenu, Menu):
    bl_label = OBJECT_MT_modifier_add_deform.bl_label
    bl_options = {'SEARCH_ON_KEY_PRESS'}
    draw = OBJECT_MT_modifier_add_deform.draw


class OBJECT_MT_modifier_list_normals(ModifierListAddMenu, Menu):
    bl_label = OBJECT_MT_modifier_add_normals.bl_label
    bl_options = {'SEARCH_ON_KEY_PRESS'}
    draw = OBJECT_MT_modifier_add_normals.draw


class OBJECT_MT_modifier_list_physics(ModifierListAddMenu, Menu):
    bl_label = OBJECT_MT_modifier_add_physics.bl_label
    bl_options = {'SEARCH_ON_KEY_PRESS'}
    draw = OBJECT_MT_modifier_add_physics.draw


class OBJECT_MT_modifier_list_color(ModifierListAddMenu, Menu):
    bl_label = OBJECT_MT_modifier_add_color.bl_label
    bl_options = {'SEARCH_ON_KEY_PRESS'}
    draw = OBJECT_MT_modifier_add_color.draw


# Mesh modifiers of the list view's menu, in columns. Each column is sorted by label.
MESH_CATEGORIES_TOP = (
    ("Model", (
        'BEVEL', 'BOOLEAN', 'CAP_HOLES', 'COLLAPSE', 'DICE', 'DRAPE', 'FACE_EXTRUDE', 'INSET', 'MASONRY', 'MIRROR', 'PANELIZE', 'POKE',
        'SCREW', 'SHATTER', 'SKIN', 'SLICE', 'SOLIDIFY', 'SYMMETRIZE', 'VOLUME_TO_MESH', 'VORONOI_FRACTURE', 'WELD',
        'WIREFRAME',
    )),
    ("Topology", (
        'SUBSURF', 'MULTIRES', 'DECIMATE', 'DISSOLVE', 'REMESH', 'ISOTROPIC_REMESH', 'QUADRIFLOW_REMESH',
        'TRIANGULATE', 'QUADRANGULATE', 'BEAUTIFY_FACES', 'ROTATE_EDGES', 'EDGE_SPLIT',
    )),
    ("Elements", (
        'ARRAY', 'RADIAL_ARRAY', 'BUILD', 'MASK', 'DELETE_MESH', 'EXPLODED_VIEW',
        'RANDOMIZE_ELEMENTS', 'ELEMENT_ORDER', 'ELEMENT_PIVOT',
    )),
    ("Deform", (
        'ARMATURE', 'CURVE', 'HOOK', 'LATTICE', 'MESH_DEFORM', 'SURFACE_DEFORM', 'SHRINKWRAP',
        'LAPLACIANDEFORM', 'FFD_2', 'FFD_3', 'FFD_4', 'SHAPE_KEY_INBETWEEN',
    )),
    ("Shape", (
        'BEND', 'TWIST', 'TAPER', 'STRETCH', 'SIMPLE_DEFORM', 'CAST', 'DISPLACE', 'NOISE', 'PUSH',
        'MELT', 'WARP', 'WAVE',
    )),
)
MESH_CATEGORIES_BOTTOM = (
    ("Smooth", ('SMOOTH', 'CORRECTIVE_SMOOTH', 'LAPLACIANSMOOTH', 'RELAX')),
    ("Normals", ('NORMAL_EDIT', 'WEIGHTED_NORMAL', 'MARK_BY_ANGLE')),
    ("UV & Data", (
        'UV_PROJECT', 'UVW_MAP', 'UV_WARP', 'SMART_UV_PROJECT', 'DATA_TRANSFER', 'DATA_CHANNEL',
        'MATERIAL_BY_ELEMENT', 'TENSION', 'MESH_CACHE', 'MESH_SEQUENCE_CACHE',
        'VERTEX_WEIGHT_EDIT', 'VERTEX_WEIGHT_MIX', 'VERTEX_WEIGHT_PROXIMITY',
    )),
    ("Select", ('EDIT_POLY', 'POLY_SELECT', 'POLY_EDIT', 'VOLUME_SELECT')),
    ("Physics", (
        'CLOTH', 'COLLISION', 'DYNAMIC_PAINT', 'EXPLODE', 'FLUID', 'OCEAN', 'PARTICLE_INSTANCE',
        'PARTICLE_SYSTEM', 'SOFT_BODY',
    )),
)


class OBJECT_MT_modifier_list_add(ModifierListAddMenu, Menu):
    bl_label = "Modifier List"
    bl_options = {'SEARCH_ON_KEY_PRESS'}

    @classmethod
    def draw_category(cls, layout, title, types):
        types = [t for t in types if t in cls.MODIFIER_TYPES_TO_LABELS]
        if not types:
            return
        col = layout.column()
        col.label(text=title)
        col.separator(type='LINE')
        for mod_type in sorted(types, key=lambda t: cls.MODIFIER_TYPES_TO_LABELS[t]):
            cls.operator_modifier_add(col, mod_type)

    def draw_mesh(self, layout):
        """All mesh modifiers at once, one row of columns with short groups stacked."""
        groups = dict(MESH_CATEGORIES_TOP + MESH_CATEGORIES_BOTTOM)
        columns = (
            ("Model",),
            ("Topology", "Smooth"),
            ("Elements", "Select", "Normals"),
            ("Deform",),
            ("Shape",),
            ("UV & Data",),
            ("Physics", "Geometry Nodes"),
        )
        row = layout.row()
        for titles in columns:
            col = row.column()
            for k, title in enumerate(titles):
                if k:
                    col.separator(factor=1.5)
                if title == "Geometry Nodes":
                    col.label(text=title)
                    col.separator(type='LINE')
                    self.operator_modifier_add(col, 'NODES')
                    col.menu_contents("OBJECT_MT_modifier_add_root_catalogs")
                else:
                    self.draw_category(col, title, groups[title])

    def draw(self, context):
        layout = self.layout
        ob = context.object
        if not ob:
            return
        ob_type = ob.type

        if layout.operator_context == 'EXEC_REGION_WIN':
            layout.operator_context = 'INVOKE_REGION_WIN'
            layout.operator(
                "WM_OT_search_single_menu",
                text="Search...",
                icon='VIEWZOOM',
            ).menu_idname = "OBJECT_MT_modifier_list_add"
            layout.separator()

        layout.operator_context = 'INVOKE_REGION_WIN'

        if ob_type == 'MESH':
            self.draw_mesh(layout)
            return

        if ob_type in {'CURVE', 'CURVES', 'FONT', 'SURFACE', 'LATTICE', 'GREASEPENCIL', 'POINTCLOUD'}:
            layout.menu("OBJECT_MT_modifier_list_edit")
        if ob_type in {'CURVE', 'CURVES', 'FONT', 'SURFACE', 'VOLUME', 'GREASEPENCIL'}:
            layout.menu("OBJECT_MT_modifier_list_generate")
        if ob_type in {'CURVE', 'CURVES', 'FONT', 'SURFACE', 'LATTICE', 'VOLUME', 'GREASEPENCIL'}:
            layout.menu("OBJECT_MT_modifier_list_deform")
        if ob_type in {'CURVE', 'FONT', 'SURFACE', 'LATTICE'}:
            layout.menu("OBJECT_MT_modifier_list_physics")
        if ob_type in {'GREASEPENCIL'}:
            layout.menu("OBJECT_MT_modifier_list_color")

        if ob_type in {'CURVE', 'CURVES', 'FONT', 'VOLUME', 'POINTCLOUD', 'GREASEPENCIL'}:
            layout.menu_contents("OBJECT_MT_modifier_add_root_catalogs")
            layout.separator()
            self.operator_modifier_add(layout, 'NODES')


class OBJECT_MT_modifier_list_options(Menu):
    bl_label = "Modifier List Options"

    def draw(self, context):
        layout = self.layout
        last_on_top = _last_on_top(context)
        layout.operator(
            OBJECT_OT_modifier_list_order.bl_idname,
            text="First on Top",
            icon='MODLIST_FIRST_ON_TOP',
            depress=not last_on_top,
        ).last_on_top = False
        layout.operator(
            OBJECT_OT_modifier_list_order.bl_idname,
            text="Last on Top",
            icon='MODLIST_LAST_ON_TOP',
            depress=last_on_top,
        ).last_on_top = True
        layout.separator()
        layout.operator(OBJECT_OT_modifier_list_enable_all.bl_idname, text="Enable All",
                        icon='MODLIST_ENABLE_ALL').enable = True
        layout.operator(OBJECT_OT_modifier_list_enable_all.bl_idname, text="Disable All",
                        icon='MODLIST_DISABLE_ALL').enable = False
        layout.operator(OBJECT_OT_modifier_list_apply_all.bl_idname, icon='MODLIST_APPLY_ALL')
        layout.operator(OBJECT_OT_modifier_list_delete_all.bl_idname, icon='MODLIST_DELETE_ALL')
        layout.separator()
        layout.operator(OBJECT_OT_modifier_list_copy_stack.bl_idname, icon='MODLIST_COPY_STACK')
        layout.separator()
        layout.operator(OBJECT_OT_modifier_list_favorites.bl_idname, text="Favourites...",
                        icon='MODLIST_FAVORITES')


def draw_item_context_menu(self, context):
    ui_list = getattr(context, "ui_list", None)
    if ui_list is None or ui_list.bl_idname != LIST_ID:
        return
    layout = self.layout
    layout.operator_context = 'INVOKE_DEFAULT'
    ob, md = _active(context)
    layout.operator(OBJECT_OT_modifier_list_collapse_to.bl_idname, icon='MODLIST_COLLAPSE_TO')
    if md is not None:
        layout.operator("object.modifier_apply", text="Apply", icon='MODLIST_APPLY').modifier = md.name
    layout.separator()
    if md is not None:
        layout.operator("object.modifier_copy", text="Duplicate", icon='DUPLICATE').modifier = md.name
    layout.operator(OBJECT_OT_modifier_list_copy.bl_idname, icon='MODLIST_COPY')
    layout.operator(OBJECT_OT_modifier_list_paste.bl_idname, icon='MODLIST_PASTE')
    if md is not None:
        layout.operator(
            "object.modifier_copy_to_selected",
            text="Copy to Selected",
            icon='MODLIST_COPY_TO_SELECTED',
        ).modifier = md.name
    layout.separator()
    if md is not None:
        layout.operator(
            OBJECT_OT_modifier_list_toggle.bl_idname,
            text="Disable" if md.show_viewport else "Enable",
            icon='RESTRICT_VIEW_ON' if md.show_viewport else 'RESTRICT_VIEW_OFF',
        )
    layout.operator(OBJECT_OT_modifier_list_rename.bl_idname, icon='MODLIST_RENAME')
    if md is not None:
        layout.operator("object.modifier_remove", text="Delete", icon='X').modifier = md.name


# -----------------------------------------------------------------------------
# List

def _off_icon(name, fallback):
    """The off icon of a switch, or the plain one with a build that doesn't have it yet."""
    icons = bpy.types.UILayout.bl_rna.functions["prop"].parameters["icon"].enum_items
    return name if name in icons else fallback


class MODIFIER_UL_modifier_list(UIList):
    def draw_item(self, _context, layout, _data, item, _icon_value, _active_data, _active_propname, _index):
        md = item
        ob = md.id_data
        row = layout.row(align=True)
        # A modifier that is off in the viewport and in renders looks faded, like a hidden object.
        name = row.row(align=True)
        name.active = md.show_viewport or md.show_render
        name.prop(md, "name", text="", emboss=False, icon=_icon(md.type))

        def toggle(prop, on, **kw):
            # Each switch that is off is faded, so it's clear at a glance which ones are on.
            sub = row.row(align=True)
            sub.active = on
            sub.prop(md, prop, text="", emboss=False, **kw)

        if ob.type == 'MESH':
            # Same rule as the panel header: only modifiers that work in edit mode show these.
            if md.type not in {
                    'COLLISION', 'SURFACE', 'CLOTH', 'SOFT_BODY', 'FLUID', 'DYNAMIC_PAINT',
                    'PARTICLE_SYSTEM', 'OCEAN', 'EXPLODE'}:
                # Their own off icons, like the eye and the camera: a fade alone is hidden by the
                # highlight under the mouse, so the click would only show once the mouse leaves.
                toggle("show_on_cage", md.show_on_cage and md.show_in_editmode and md.show_viewport,
                       icon='MESH_DATA' if md.show_on_cage else _off_icon('MODLIST_ON_CAGE_OFF', 'MESH_DATA'))
                toggle("show_in_editmode", md.show_in_editmode and md.show_viewport,
                       icon='EDITMODE_HLT' if md.show_in_editmode else
                       _off_icon('MODLIST_EDITMODE_OFF', 'EDITMODE_HLT'))
        toggle("show_viewport", md.show_viewport)
        toggle("show_render", md.show_render)

    def filter_items(self, context, data, propname):
        items = getattr(data, propname)
        flags = [self.bitflag_filter_item] * len(items)
        if self.filter_name:
            flags = UIList.filter_items_by_name(
                self.filter_name, self.bitflag_filter_item, items, "name", reverse=False)
        order = []
        if _last_on_top(context):
            count = len(items)
            order = [count - 1 - i for i in range(count)]
        return flags, order


# -----------------------------------------------------------------------------
# Drawing (called from DATA_PT_modifiers)

def _draw_base_row(layout, ob):
    row = layout.row(align=True)
    is_edit = ob.mode == 'EDIT'
    text = ob.data.name if ob.data else ob.name
    icon_value = layout.icon(ob.data) if ob.data else layout.icon(ob)
    row.operator(
        OBJECT_OT_modifier_list_edit_base.bl_idname,
        text=text,
        icon_value=icon_value,
        depress=is_edit,
    )


def draw_header(layout, context, space):
    row = layout.row(align=True)
    if not space.use_modifier_list:
        # The list view has its Add Modifier button under the favourites.
        row.operator("wm.call_menu", text="Add Modifier", icon='ADD').name = "OBJECT_MT_modifier_add"
        row.separator()
    toggle = row.row(align=True)
    toggle.prop(space, "use_modifier_list", text="", icon='MODLIST_VIEW_STACK', invert_checkbox=True)
    toggle.prop(space, "use_modifier_list", text="", icon='MODLIST_VIEW_LIST')
    if space.use_modifier_list:
        row.separator()
        row.operator("buttons.toggle_pin", text="",
                     icon='PINNED' if space.use_pin_id else 'UNPINNED')
        row.menu("OBJECT_MT_modifier_list_options", text="", icon='COLLAPSEMENU')


def draw_list(layout, context, ob):
    favorites = [t for t in favorites_get() if _type_fits_object(t, ob)]
    col = layout.column(align=True)
    if favorites:
        grid = col.grid_flow(row_major=True, columns=favorites_columns_get(), even_columns=True,
                             align=True)
        for mod_type in favorites:
            grid.operator(
                OBJECT_OT_modifier_list_add.bl_idname,
                text=ModifierAddMenu.MODIFIER_TYPES_TO_LABELS[mod_type],
                icon=_icon(mod_type),
            ).type = mod_type
        col.separator(factor=0.5)
    # Every modifier: the menu, a search that adds the one picked, and the favourites editor.
    row = col.row(align=True)
    row.scale_y = 1.3
    row.menu("OBJECT_MT_modifier_list_add", text="Add Modifier", icon='ADD')
    sub = row.row(align=True)
    sub.operator_context = 'INVOKE_REGION_WIN'
    sub.operator("wm.search_single_menu", text="", icon='VIEWZOOM').menu_idname = (
        "OBJECT_MT_modifier_list_add")
    row.operator(OBJECT_OT_modifier_list_favorites.bl_idname, text="", icon='MODLIST_FAVORITES')
    layout.separator(factor=0.5)

    last_on_top = _last_on_top(context)
    col = layout.column(align=True)
    if not last_on_top:
        _draw_base_row(col, ob)
    col.template_list(
        LIST_ID, "", ob, "modifiers", ob.modifiers, "active_index",
        rows=6, sort_lock=True,
    )
    if last_on_top:
        _draw_base_row(col, ob)

    md = ob.modifiers.active
    _draw_active_bar(col, context, ob, md)
    layout.separator(factor=0.5)


def _draw_active_bar(layout, context, ob, md):
    """Two rows under the list, every button the same width so the rows span the list: the
    actions for the active modifier, then the list buttons. The buttons are always there, and
    the ones that can't be used now are greyed out, so nothing moves around."""
    name = md.name if md else ""
    editable = md is not None and ob.library is None and not (md.type == 'EDIT_POLY' and md.is_editing)
    spaces = list(_view3d_spaces(context))

    # Icon-only buttons in a row keep their small size, in grid cells they take the cell's width.
    grid = layout.grid_flow(row_major=True, columns=5, even_columns=True, even_rows=True, align=True)
    grid.scale_y = 1.15

    def cell(enabled=True):
        sub = grid.column(align=True)
        sub.enabled = enabled
        return sub

    cell(editable).operator("object.modifier_apply", text="", icon='MODLIST_APPLY').modifier = name
    cell(editable).operator("object.modifier_copy", text="", icon='DUPLICATE').modifier = name
    cell(editable).operator("object.modifier_copy_to_selected", text="",
                            icon='MODLIST_COPY_TO_SELECTED').modifier = name
    if md is not None:
        cell().prop(md, "use_pin_to_last", text="", icon='PINNED' if md.use_pin_to_last else 'UNPINNED')
    else:
        cell(False).label(text="", icon='UNPINNED')
    cell().prop(ob, "show_modifier_end_result", text="", icon='MODLIST_END_RESULT')
    cell().operator(
        OBJECT_OT_modifier_list_gizmos.bl_idname,
        text="",
        icon='GIZMO',
        depress=bool(spaces) and spaces[0].show_gizmo_modifier,
    )
    cell(md is not None).operator(OBJECT_OT_modifier_list_collapse_to.bl_idname, text="",
                                  icon='MODLIST_COLLAPSE_TO')
    cell(md is not None).operator(OBJECT_OT_modifier_list_move.bl_idname, text="",
                                  icon='TRIA_UP').direction = 'UP'
    cell(md is not None).operator(OBJECT_OT_modifier_list_move.bl_idname, text="",
                                  icon='TRIA_DOWN').direction = 'DOWN'
    cell(md is not None).operator("object.modifier_remove", text="", icon='X').modifier = name


classes = (
    OBJECT_OT_modifier_list_add,
    OBJECT_OT_modifier_list_collapse_to,
    OBJECT_OT_modifier_list_apply_all,
    OBJECT_OT_modifier_list_delete_all,
    OBJECT_OT_modifier_list_enable_all,
    OBJECT_OT_modifier_list_toggle,
    OBJECT_OT_modifier_list_move,
    OBJECT_OT_modifier_list_copy,
    OBJECT_OT_modifier_list_paste,
    OBJECT_OT_modifier_list_rename,
    OBJECT_OT_modifier_list_copy_stack,
    OBJECT_OT_modifier_list_order,
    OBJECT_OT_modifier_list_gizmos,
    OBJECT_OT_modifier_list_edit_base,
    OBJECT_OT_modifier_list_favorite_toggle,
    OBJECT_OT_modifier_list_favorite_move,
    OBJECT_OT_modifier_list_favorites_columns,
    OBJECT_OT_modifier_list_favorites,
    OBJECT_OT_modifier_list_favorites_reset,
    OBJECT_MT_modifier_list_edit,
    OBJECT_MT_modifier_list_generate,
    OBJECT_MT_modifier_list_deform,
    OBJECT_MT_modifier_list_normals,
    OBJECT_MT_modifier_list_physics,
    OBJECT_MT_modifier_list_color,
    OBJECT_MT_modifier_list_add,
    OBJECT_MT_modifier_list_options,
    MODIFIER_UL_modifier_list,
)


def register_menus():
    bpy.types.UI_MT_list_item_context_menu.append(draw_item_context_menu)


def unregister_menus():
    try:
        bpy.types.UI_MT_list_item_context_menu.remove(draw_item_context_menu)
    except (AttributeError, ValueError):
        pass
