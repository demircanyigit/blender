# SPDX-FileCopyrightText: 2026 Blender Authors
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""Viewport gizmos for the active modifier of the active object: a handle to move the center of
Bend, Twist, Taper, Stretch, UVW Map, Volume Select and Radial Array (and the plane of Slice), a
dial for the angle of Bend, Twist and Radial Array, and markers for the limits and the slice
distance. FFD modifiers get a handle on each control point and a cage between them."""

import bpy
from bpy.types import Gizmo, GizmoGroup
from mathutils import Euler, Matrix, Vector

AXIS_DEFORM_TYPES = {'BEND', 'TWIST', 'TAPER', 'STRETCH'}
GIZMO_TYPES = AXIS_DEFORM_TYPES | {'UVW_MAP', 'VOLUME_SELECT', 'RADIAL_ARRAY', 'SLICE'}
AXIS_INDEX = {'X': 0, 'Y': 1, 'Z': 2}

COLOR_MOVE = (0.95, 0.75, 0.2)
COLOR_MOVE_HI = (1.0, 0.9, 0.45)
COLOR_DIAL = (0.3, 0.6, 1.0)
COLOR_DIAL_HI = (0.55, 0.8, 1.0)
COLOR_LIMIT = (0.95, 0.35, 0.3)
COLOR_LIMIT_HI = (1.0, 0.6, 0.55)


def _gizmos_shown(context):
    space = context.space_data
    return getattr(space, "show_gizmo_modifier", True)


def _active_modifier(context, types):
    ob = context.object
    if ob is None or ob.mode not in {'OBJECT', 'EDIT'}:
        return None
    if not _gizmos_shown(context):
        return None
    md = ob.modifiers.active
    if md is None or md.type not in types or not md.show_viewport:
        return None
    return md


def _frame(md):
    """The modifier's frame in object space, with its Z axis along the axis of the modifier."""
    if md.type == 'SLICE':
        rotation = Euler(md.plane_rotation).to_matrix()
        return Matrix.Translation(md.plane_location) @ rotation.to_4x4()
    if md.type == 'RADIAL_ARRAY':
        rotation = Matrix.Identity(3)
        axis = AXIS_INDEX[md.axis]
    else:
        rotation = Euler(md.rotation).to_matrix()
        axis = AXIS_INDEX[md.deform_axis] if md.type in AXIS_DEFORM_TYPES else 2
    x = rotation.col[(axis + 1) % 3]
    y = rotation.col[(axis + 2) % 3]
    z = rotation.col[axis]
    basis = Matrix((x, y, z)).transposed().to_4x4()
    return Matrix.Translation(md.center) @ basis


def _world_frame(ob, frame):
    """The frame in world space with unit axes, and how long one object unit along its Z is."""
    world = ob.matrix_world @ frame
    z_length = world.col[2].xyz.length
    return world.normalized(), (z_length if z_length > 1e-8 else 1.0)


def _dial_frame(md, frame):
    """The dial turns around its Z axis. Twist and Radial Array turn around the modifier axis;
    Bend turns in the plane of the axis and the bend direction."""
    if md.type != 'BEND':
        return frame
    direction = Matrix.Rotation(getattr(md, "direction", 0.0), 4, 'Z')
    bent = frame @ direction
    x, y, z = bent.col[0].xyz, bent.col[1].xyz, bent.col[2].xyz
    basis = Matrix((z, x, y)).transposed().to_4x4()
    basis.translation = bent.translation
    return basis


class VIEW3D_GT_modifier_lines(Gizmo):
    """Lines drawn in object space, not scaled with the view and never picked."""
    bl_idname = "VIEW3D_GT_modifier_lines"

    __slots__ = (
        "shape",
        "key",
    )

    def setup(self):
        self.shape = None
        self.key = None
        self.use_draw_scale = False
        self.use_draw_modal = True

    def set_lines(self, points):
        key = tuple(tuple(p) for p in points)
        if key == self.key:
            return
        self.key = key
        self.shape = self.new_custom_shape('LINES', key) if key else None

    def draw(self, _context):
        if self.shape is not None:
            self.draw_custom_shape(self.shape)

    def test_select(self, _context, _location):
        return -1


def _limit_marker(gizmos):
    gz = gizmos.new("GIZMO_GT_arrow_3d")
    # Only the head, placed right at the limit.
    gz.draw_style = 'BOX'
    gz.draw_options = set()
    gz.length = 0.0
    gz.scale_basis = 0.5
    gz.color = COLOR_LIMIT
    gz.color_highlight = COLOR_LIMIT_HI
    return gz


class VIEW3D_GGT_modifier_gizmos(GizmoGroup):
    bl_idname = "VIEW3D_GGT_modifier_gizmos"
    bl_label = "Modifier Gizmos"
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'WINDOW'
    bl_options = {'3D', 'PERSISTENT'}

    @classmethod
    def poll(cls, context):
        return _active_modifier(context, GIZMO_TYPES) is not None

    def setup(self, context):
        self.move = self.gizmos.new("GIZMO_GT_move_3d")
        self.move.draw_options = {'ALIGN_VIEW', 'FILL_SELECT'}
        self.move.scale_basis = 0.15
        self.move.color = COLOR_MOVE
        self.move.color_highlight = COLOR_MOVE_HI

        self.dial = self.gizmos.new("GIZMO_GT_dial_3d")
        self.dial.draw_options = {'ANGLE_VALUE'}
        self.dial.scale_basis = 0.8
        self.dial.line_width = 2.0
        self.dial.color = COLOR_DIAL
        self.dial.color_highlight = COLOR_DIAL_HI

        self.axis_line = self.gizmos.new(VIEW3D_GT_modifier_lines.bl_idname)
        self.axis_line.color = COLOR_LIMIT
        self.axis_line.alpha = 0.6
        self.axis_line.line_width = 1.0

        # The limits and the slice distance are object space lengths along the frame's Z axis,
        # the markers work in world units, so the handlers convert between the two.
        self.upper = _limit_marker(self.gizmos)
        self.upper.target_set_handler("offset", get=self._get_upper, set=self._set_upper)
        self.lower = _limit_marker(self.gizmos)
        self.lower.target_set_handler("offset", get=self._get_lower, set=self._set_lower)
        self.distance = _limit_marker(self.gizmos)
        self.distance.target_set_handler("offset", get=self._get_distance, set=self._set_distance)

        self.refresh(context)

    @staticmethod
    def _scale():
        md = _active_modifier(bpy.context, GIZMO_TYPES)
        if md is None:
            return None, 1.0
        _world, z_length = _world_frame(bpy.context.object, _frame(md))
        return md, z_length

    def _get_upper(self):
        md, s = self._scale()
        return md.upper_limit * s if md is not None else 0.0

    def _set_upper(self, value):
        md, s = self._scale()
        if md is not None:
            md.upper_limit = value / s

    def _get_lower(self):
        md, s = self._scale()
        # The lower marker points the other way, so its offset grows away from the center too.
        return -md.lower_limit * s if md is not None else 0.0

    def _set_lower(self, value):
        md, s = self._scale()
        if md is not None:
            md.lower_limit = -value / s

    def _get_distance(self):
        md, s = self._scale()
        return md.distance * s if md is not None else 0.0

    def _set_distance(self, value):
        md, s = self._scale()
        if md is not None:
            md.distance = value / s

    def refresh(self, context):
        md = _active_modifier(context, GIZMO_TYPES)
        for gz in (self.move, self.dial, self.axis_line, self.upper, self.lower, self.distance):
            gz.hide = True
        if md is None:
            return

        self.move.target_set_prop("offset", md, "plane_location" if md.type == 'SLICE' else "center")
        self.move.hide = False

        if md.type in {'BEND', 'TWIST', 'RADIAL_ARRAY'}:
            self.dial.target_set_prop("offset", md, "angle")
            self.dial.hide = False
        if md.type in AXIS_DEFORM_TYPES and md.use_limits:
            self.upper.hide = False
            self.lower.hide = False
            self.axis_line.hide = False
        if md.type == 'SLICE':
            self.distance.hide = False

    def draw_prepare(self, context):
        md = _active_modifier(context, GIZMO_TYPES)
        if md is None:
            return
        ob = context.object
        frame = _frame(md)
        world, _z_length = _world_frame(ob, frame)

        self.move.matrix_basis = ob.matrix_world.copy()
        self.dial.matrix_basis = _world_frame(ob, _dial_frame(md, frame))[0]

        self.upper.matrix_basis = world
        self.lower.matrix_basis = world @ Matrix.Rotation(3.141592653589793, 4, 'X')
        self.distance.matrix_basis = world

        if not self.axis_line.hide:
            self.axis_line.matrix_basis = ob.matrix_world @ frame
            self.axis_line.set_lines([
                (0.0, 0.0, md.lower_limit),
                (0.0, 0.0, md.upper_limit),
            ])


FFD_TYPES = {'FFD_2': 2, 'FFD_3': 3, 'FFD_4': 4}


def _ffd_rest_point(md, index, res):
    x = index % res
    y = (index // res) % res
    z = index // (res * res)
    low = Vector(md.bounds_min)
    size = Vector(md.bounds_max) - low
    step = res - 1
    return low + Vector((size.x * x / step, size.y * y / step, size.z * z / step))


def _ffd_point(md, index, res):
    return _ffd_rest_point(md, index, res) + Vector(md.control_points[index])


def _ffd_cage_lines(md, res):
    points = [_ffd_point(md, i, res) for i in range(res ** 3)]
    lines = []
    for z in range(res):
        for y in range(res):
            for x in range(res):
                i = (z * res + y) * res + x
                if x + 1 < res:
                    lines += (points[i], points[i + 1])
                if y + 1 < res:
                    lines += (points[i], points[i + res])
                if z + 1 < res:
                    lines += (points[i], points[i + res * res])
    return lines


class VIEW3D_GGT_modifier_ffd(GizmoGroup):
    bl_idname = "VIEW3D_GGT_modifier_ffd"
    bl_label = "FFD Modifier Gizmos"
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'WINDOW'
    bl_options = {'3D', 'PERSISTENT'}

    @classmethod
    def poll(cls, context):
        return _active_modifier(context, FFD_TYPES) is not None

    def setup(self, context):
        # The cage stays drawn while a point is dragged, so the shape can be seen changing.
        self.cage = self.gizmos.new(VIEW3D_GT_modifier_lines.bl_idname)
        self.cage.color = 0.95, 0.6, 0.15
        self.cage.alpha = 0.7
        self.cage.line_width = 1.0
        self.points = []
        for index in range(64):
            gz = self.gizmos.new("GIZMO_GT_move_3d")
            gz.draw_options = {'ALIGN_VIEW', 'FILL'}
            gz.scale_basis = 0.06
            gz.color = 0.95, 0.6, 0.15
            gz.alpha = 0.9
            gz.color_highlight = 1.0, 0.9, 0.5
            gz.alpha_highlight = 1.0
            gz.target_set_handler("offset", get=self._getter(index), set=self._setter(index))
            self.points.append(gz)

    @staticmethod
    def _getter(index):
        def get():
            md = _active_modifier(bpy.context, FFD_TYPES)
            if md is None:
                return (0.0, 0.0, 0.0)
            return tuple(md.control_points[index])
        return get

    @staticmethod
    def _setter(index):
        def set(value):
            md = _active_modifier(bpy.context, FFD_TYPES)
            if md is not None:
                md.control_points[index] = value
        return set

    def draw_prepare(self, context):
        md = _active_modifier(context, FFD_TYPES)
        if md is None:
            return
        res = FFD_TYPES[md.type]
        ob_matrix = context.object.matrix_world
        self.cage.matrix_basis = ob_matrix.copy()
        self.cage.set_lines(_ffd_cage_lines(md, res))
        count = res ** 3
        for index, gz in enumerate(self.points):
            gz.hide = index >= count
            if not gz.hide:
                gz.matrix_basis = ob_matrix @ Matrix.Translation(_ffd_rest_point(md, index, res))


classes = (
    VIEW3D_GT_modifier_lines,
    VIEW3D_GGT_modifier_gizmos,
    VIEW3D_GGT_modifier_ffd,
)


if __name__ == "__main__":  # only for live edit.
    from bpy.utils import register_class
    for cls in classes:
        register_class(cls)
