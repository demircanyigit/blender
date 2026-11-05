# SPDX-FileCopyrightText: 2026 Blender Authors
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""Viewport gizmos for the active modifier of the active object: a handle to move the center of
Bend, Twist, Taper, Stretch, UVW Map, Volume Select and Radial Array (and the plane of Slice), a
dial for the angle of Bend, Twist and Radial Array, and arrows for the limits and the slice
distance. FFD modifiers get a handle on each control point and a cage between them."""

from bpy.types import Gizmo, GizmoGroup
from mathutils import Euler, Matrix, Vector

AXIS_DEFORM_TYPES = {'BEND', 'TWIST', 'TAPER', 'STRETCH'}
GIZMO_TYPES = AXIS_DEFORM_TYPES | {'UVW_MAP', 'VOLUME_SELECT', 'RADIAL_ARRAY', 'SLICE'}
AXIS_INDEX = {'X': 0, 'Y': 1, 'Z': 2}


def _gizmos_shown(context):
    space = context.space_data
    return getattr(space, "show_gizmo_modifier", True)


def _active_modifier(context):
    ob = context.object
    if ob is None or ob.mode not in {'OBJECT', 'EDIT'}:
        return None
    if not _gizmos_shown(context):
        return None
    md = ob.modifiers.active
    if md is None or md.type not in GIZMO_TYPES or not md.show_viewport:
        return None
    return md


def _frame(md):
    """The gizmo frame in object space, with its Z axis along the axis of the modifier."""
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


class VIEW3D_GGT_modifier_gizmos(GizmoGroup):
    bl_idname = "VIEW3D_GGT_modifier_gizmos"
    bl_label = "Modifier Gizmos"
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'WINDOW'
    bl_options = {'3D', 'PERSISTENT'}

    @classmethod
    def poll(cls, context):
        return _active_modifier(context) is not None

    def setup(self, context):
        self.move = self.gizmos.new("GIZMO_GT_move_3d")
        self.move.draw_options = {'ALIGN_VIEW', 'FILL_SELECT'}
        self.move.scale_basis = 0.15
        self.move.color = 0.95, 0.75, 0.2
        self.move.color_highlight = 1.0, 0.9, 0.4

        self.dial = self.gizmos.new("GIZMO_GT_dial_3d")
        self.dial.draw_options = {'ANGLE_VALUE'}
        self.dial.scale_basis = 0.8
        self.dial.color = 0.3, 0.6, 1.0
        self.dial.color_highlight = 0.5, 0.8, 1.0

        self.upper = self.gizmos.new("GIZMO_GT_arrow_3d")
        self.lower = self.gizmos.new("GIZMO_GT_arrow_3d")
        self.distance = self.gizmos.new("GIZMO_GT_arrow_3d")
        for gz in (self.upper, self.lower, self.distance):
            gz.color = 0.9, 0.3, 0.3
            gz.color_highlight = 1.0, 0.5, 0.5
            gz.scale_basis = 0.6

        self.refresh(context)

    def refresh(self, context):
        md = _active_modifier(context)
        all_gizmos = (self.move, self.dial, self.upper, self.lower, self.distance)
        if md is None:
            for gz in all_gizmos:
                gz.hide = True
            return
        for gz in all_gizmos:
            gz.hide = True

        self.move.target_set_prop("offset", md, "plane_location" if md.type == 'SLICE' else "center")
        self.move.hide = False

        if md.type in {'BEND', 'TWIST', 'RADIAL_ARRAY'}:
            self.dial.target_set_prop("offset", md, "angle")
            self.dial.hide = False
        if md.type in AXIS_DEFORM_TYPES and md.use_limits:
            self.upper.target_set_prop("offset", md, "upper_limit")
            self.lower.target_set_prop("offset", md, "lower_limit")
            self.upper.hide = False
            self.lower.hide = False
        if md.type == 'SLICE':
            self.distance.target_set_prop("offset", md, "distance")
            self.distance.hide = False

    def draw_prepare(self, context):
        md = _active_modifier(context)
        if md is None:
            return
        ob_matrix = context.object.matrix_world
        self.move.matrix_basis = ob_matrix.copy()
        frame = ob_matrix @ _frame(md)
        for gz in (self.dial, self.upper, self.lower, self.distance):
            gz.matrix_basis = frame.normalized()


FFD_TYPES = {'FFD_2': 2, 'FFD_3': 3, 'FFD_4': 4}


def _active_ffd(context):
    ob = context.object
    if ob is None or ob.mode not in {'OBJECT', 'EDIT'}:
        return None
    if not _gizmos_shown(context):
        return None
    md = ob.modifiers.active
    if md is None or md.type not in FFD_TYPES or not md.show_viewport:
        return None
    return md


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


class VIEW3D_GT_ffd_cage(Gizmo):
    """Lines between the control points of an FFD modifier."""
    bl_idname = "VIEW3D_GT_ffd_cage"

    __slots__ = (
        "shape",
        "key",
    )

    def setup(self):
        self.shape = None
        self.key = None

    def update_shape(self, md, res):
        points = [_ffd_point(md, i, res) for i in range(res ** 3)]
        key = tuple(tuple(p) for p in points)
        if key == self.key:
            return
        self.key = key
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
        self.shape = self.new_custom_shape('LINES', [tuple(p) for p in lines])

    def draw(self, _context):
        if self.shape is not None:
            self.draw_custom_shape(self.shape)

    def test_select(self, _context, _location):
        return -1


class VIEW3D_GGT_modifier_ffd(GizmoGroup):
    bl_idname = "VIEW3D_GGT_modifier_ffd"
    bl_label = "FFD Modifier Gizmos"
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'WINDOW'
    bl_options = {'3D', 'PERSISTENT'}

    @classmethod
    def poll(cls, context):
        return _active_ffd(context) is not None

    def setup(self, context):
        self.cage = self.gizmos.new(VIEW3D_GT_ffd_cage.bl_idname)
        self.cage.color = 0.95, 0.6, 0.15
        self.cage.alpha = 0.8
        self.cage.line_width = 1.0
        self.points = []
        for index in range(64):
            gz = self.gizmos.new("GIZMO_GT_move_3d")
            gz.draw_options = {'ALIGN_VIEW', 'FILL_SELECT'}
            gz.scale_basis = 0.08
            gz.color = 0.95, 0.6, 0.15
            gz.color_highlight = 1.0, 0.85, 0.4
            gz.target_set_handler("offset", get=self._getter(index), set=self._setter(index))
            self.points.append(gz)

    @staticmethod
    def _getter(index):
        import bpy

        def get():
            md = _active_ffd(bpy.context)
            if md is None:
                return (0.0, 0.0, 0.0)
            return tuple(md.control_points[index])
        return get

    @staticmethod
    def _setter(index):
        import bpy

        def set(value):
            md = _active_ffd(bpy.context)
            if md is not None:
                md.control_points[index] = value
        return set

    def draw_prepare(self, context):
        md = _active_ffd(context)
        if md is None:
            return
        res = FFD_TYPES[md.type]
        ob_matrix = context.object.matrix_world
        self.cage.matrix_basis = ob_matrix.copy()
        self.cage.update_shape(md, res)
        count = res ** 3
        for index, gz in enumerate(self.points):
            gz.hide = index >= count
            if not gz.hide:
                gz.matrix_basis = ob_matrix @ Matrix.Translation(_ffd_rest_point(md, index, res))


classes = (
    VIEW3D_GGT_modifier_gizmos,
    VIEW3D_GT_ffd_cage,
    VIEW3D_GGT_modifier_ffd,
)


if __name__ == "__main__":  # only for live edit.
    from bpy.utils import register_class
    for cls in classes:
        register_class(cls)
