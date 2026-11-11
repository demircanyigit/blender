# SPDX-FileCopyrightText: 2026 Blender Authors
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""Multi-Cut: cut faces point by point.

Click on faces, edges or vertices to place cut points; each point is joined to the one before it,
straight across the face they share or across the faces in between. A cut can start, bend and end
inside faces: an end inside a face stays as an edge there, and a closed shape drawn inside one face
becomes a face of its own (click the first point again to close it). Hold the mouse button and drag
to slide a point along its edge, Shift snaps it in steps. Backspace removes the last point, Enter or
right-click makes the cut, Esc drops it. Ctrl+click on an edge inserts an edge loop through it.
Before the first point, dragging from a face or empty space slices the mesh along the dragged line.

With the tool active, what a click would do is shown under the mouse before clicking.
"""

import bpy
from bpy.props import BoolProperty, FloatProperty
from bpy.types import Operator

TOOL_ID = "builtin.multi_cut"

# Pixels: how close the mouse must be to a vertex or an edge to snap to it.
_VERT_SNAP_PX = 12.0
_EDGE_SNAP_PX = 18.0
# Points this close to an edge end use the vertex instead.
_END_EPSILON = 1e-4
# Pixels: a press on a face or empty space that moves this far is a slice.
_SLICE_PX = 8.0

_COLOR_CUT = (1.0, 0.82, 0.16, 1.0)
_COLOR_POINT = (1.0, 0.59, 0.12, 1.0)
_COLOR_EDGE = (1.0, 0.82, 0.16, 0.6)
_COLOR_LOOP = (1.0, 0.59, 0.12, 0.5)


# -----------------------------------------------------------------------------
# Edge ring (Ctrl+click)

def _edge_ring(edge, vert_a):
    """The ring of edges across quads starting at `edge`.

    Returns `(items, segments)`, `items` is a list of `(edge, vert)` where `vert` is the end of the
    edge that matches `vert_a` (so a factor means the same side on every edge), and `segments` are
    index pairs into `items` for the faces the ring crosses.
    """
    items = [(edge, vert_a)]
    index = {edge: 0}
    segments = []
    for loop_start in edge.link_loops:
        loop = loop_start
        vert_cur = vert_a
        while True:
            face = loop.face
            if len(face.verts) != 4 or face.hide:
                break
            opposite = loop.link_loop_next.link_loop_next
            if loop.vert == vert_cur:
                vert_new = opposite.link_loop_next.vert
            else:
                vert_new = opposite.vert
            edge_new = opposite.edge
            if edge_new in index:
                # Closed ring: add the last face and stop, the other direction walks the same faces.
                segments.append((index[loop.edge], index[edge_new]))
                return items, segments
            index[edge_new] = len(items)
            items.append((edge_new, vert_new))
            segments.append((index[loop.edge], index[edge_new]))
            others = [l for l in edge_new.link_loops if l.face != face]
            if len(others) != 1:
                break
            loop = others[0]
            vert_cur = vert_new
    return items, segments


def _ring_coords(edge, t):
    """Object space segments of the loop through `edge` at `t` (from `edge.verts[0]`)."""
    items, segments = _edge_ring(edge, edge.verts[0])
    cos = [
        e.verts[0].co.lerp(e.verts[1].co, t if a == e.verts[0] else 1.0 - t)
        for e, a in items
    ]
    return [(cos[i], cos[j]) for i, j in segments]


# -----------------------------------------------------------------------------
# Cut points
#
# A point is `('VERT', vert, None)`, `('EDGE', edge, factor from edge.verts[0])` or
# `('FACE', face, object space position)`. Points refer to the mesh as it was when they were
# placed; the mesh only changes when the cut is made.

def _point_faces(point):
    kind, elem, _value = point
    if kind == 'FACE':
        return [elem]
    return [f for f in elem.link_faces if not f.hide]


def _point_co(point):
    kind, elem, value = point
    if kind == 'VERT':
        return elem.co.copy()
    if kind == 'EDGE':
        return elem.verts[0].co.lerp(elem.verts[1].co, value)
    return value.copy()


def _share_face(a, b):
    return bool(set(_point_faces(a)) & set(_point_faces(b)))


def _same_point(a, b):
    if a[0] != b[0] or a[1] != b[1]:
        return False
    if a[0] == 'VERT':
        return True
    return (_point_co(a) - _point_co(b)).length < 1e-9


def _normalize(point):
    """An edge point at an end of its edge is that vertex."""
    kind, elem, value = point
    if kind == 'EDGE':
        if value < _END_EPSILON:
            return ('VERT', elem.verts[0], None)
        if value > 1.0 - _END_EPSILON:
            return ('VERT', elem.verts[1], None)
    return point


# -----------------------------------------------------------------------------
# Picking: what is under the mouse

class _Picker:
    """Ray casts and screen positions for the edit mesh of one object.

    It also keeps the one Python reference to the edit BMesh that every vertex, edge and face
    object taken from it depends on: when the last reference goes away they all stop working.
    """

    def __init__(self, ob):
        import bmesh
        self.ob = ob
        self.mesh = ob.data
        self.bm = bmesh.from_edit_mesh(ob.data)
        self.dirty = True
        self.mesh_version = 0
        self.screen_key = None
        self.screen = None
        self.bvh = None
        self.region = self.rv3d = None
        self.matrix = ob.matrix_world.copy()
        self.xray = False

    def valid_for(self, ob):
        try:
            return ob == self.ob and ob.data == self.mesh and self.bm.is_valid
        except ReferenceError:
            return False

    def update(self):
        """Index tables and the ray-cast tree, after the mesh changed."""
        from mathutils.bvhtree import BVHTree
        bm = self.bm
        bm.verts.index_update()
        bm.edges.index_update()
        bm.faces.index_update()
        bm.verts.ensure_lookup_table()
        bm.edges.ensure_lookup_table()
        bm.faces.ensure_lookup_table()
        self.bvh = BVHTree.FromBMesh(bm)
        self.mesh_version += 1
        self.dirty = False

    def set_view(self, context):
        self.region = context.region
        self.rv3d = context.region_data
        self.matrix = self.ob.matrix_world.copy()
        shading = context.space_data.shading
        self.xray = shading.show_xray_wireframe if shading.type == 'WIREFRAME' else shading.show_xray
        if self.dirty:
            self.update()

    # Screen space.

    def to_screen(self, co_local):
        from bpy_extras.view3d_utils import location_3d_to_region_2d
        return location_3d_to_region_2d(self.region, self.rv3d, self.matrix @ co_local)

    def ray(self, mouse):
        """The view ray through a region position, in object space."""
        from bpy_extras import view3d_utils
        origin = view3d_utils.region_2d_to_origin_3d(self.region, self.rv3d, mouse)
        direction = view3d_utils.region_2d_to_vector_3d(self.region, self.rv3d, mouse)
        inv = self.matrix.inverted_safe()
        return inv @ origin, (inv.to_3x3() @ direction).normalized()

    def hit(self, mouse):
        """The face under the mouse and the hit position (object space), or None."""
        origin, direction = self.ray(mouse)
        bm = self.bm
        # Step past hidden faces along the ray.
        for _ in range(64):
            co, _normal, index, _dist = self.bvh.ray_cast(origin, direction)
            if co is None:
                return None
            face = bm.faces[index]
            if not face.hide:
                return face, co
            origin = co + direction * 1e-5
        return None

    def edge_factor(self, edge, mouse):
        """Factor along the edge (from `edge.verts[0]`) closest to the view ray at `mouse`."""
        from mathutils.geometry import intersect_line_line
        origin, direction = self.ray(mouse)
        a, b = edge.verts[0].co, edge.verts[1].co
        result = intersect_line_line(a, b, origin, origin + direction)
        ab = b - a
        if result is None or ab.length_squared == 0.0:
            return 0.5
        t = (result[0] - a).dot(ab) / ab.length_squared
        return min(max(t, 0.0), 1.0)

    def _screen_cache(self):
        """Region positions of all vertices and the edge vertex indices, as arrays. Only made
        again when the view or the mesh changed."""
        import numpy as np
        key = (tuple(tuple(row) for row in self.rv3d.perspective_matrix), tuple(tuple(row) for row in self.matrix),
               self.mesh_version, self.region.width, self.region.height)
        if self.screen_key == key:
            return self.screen
        bm = self.bm
        co = np.array([v.co for v in bm.verts], dtype=np.float64).reshape(-1, 3)
        hidden_v = np.array([v.hide for v in bm.verts], dtype=bool)
        edge_v = np.array([(e.verts[0].index, e.verts[1].index) for e in bm.edges],
                          dtype=np.int64).reshape(-1, 2)
        hidden_e = np.array([e.hide for e in bm.edges], dtype=bool)
        mat = np.array(self.rv3d.perspective_matrix) @ np.array(self.matrix)
        clip = np.c_[co, np.ones(len(co))] @ mat.T
        w = clip[:, 3]
        ok = (w > 1e-6) & ~hidden_v
        w_safe = np.where(ok, w, 1.0)
        size = np.array([self.region.width, self.region.height], dtype=np.float64)
        screen = (clip[:, :2] / w_safe[:, None] * 0.5 + 0.5) * size
        if len(edge_v):
            edge_ok = ~hidden_e & ok[edge_v[:, 0]] & ok[edge_v[:, 1]]
        else:
            edge_ok = np.zeros(0, dtype=bool)
        self.screen = (screen, ok, edge_v, edge_ok)
        self.screen_key = key
        return self.screen

    def visible(self, co_local):
        """True when no face covers the point (always true in X-ray)."""
        if self.xray:
            return True
        from mathutils import Vector
        pos = self.to_screen(co_local)
        if pos is None:
            return False
        origin, direction = self.ray(pos)
        if not self.rv3d.is_perspective:
            origin = co_local - direction * 1e4
        ray = Vector(co_local) - origin
        dist = ray.length
        if dist == 0.0:
            return True
        hit = self.bvh.ray_cast(origin, ray / dist, dist)
        return hit[0] is None or dist - hit[3] < max(1e-4, dist * 1e-3)

    def nearest_vert(self, mouse):
        import numpy as np
        screen, ok, _edge_v, _edge_ok = self._screen_cache()
        if not len(screen):
            return None
        d = np.linalg.norm(screen - np.array(mouse, dtype=np.float64), axis=1)
        d[~ok] = np.inf
        for i in np.argsort(d)[:8]:
            if d[i] > _VERT_SNAP_PX:
                break
            v = self.bm.verts[int(i)]
            if self.visible(v.co):
                return v
        return None

    def nearest_edge(self, mouse, radius):
        """The closest visible edge within `radius` pixels and the factor along it."""
        import numpy as np
        screen, _ok, edge_v, edge_ok = self._screen_cache()
        if not len(edge_v):
            return None
        m = np.array(mouse, dtype=np.float64)
        p0, p1 = screen[edge_v[:, 0]], screen[edge_v[:, 1]]
        seg = p1 - p0
        length_sq = np.maximum((seg * seg).sum(axis=1), 1e-12)
        t = np.clip(((m - p0) * seg).sum(axis=1) / length_sq, 0.0, 1.0)
        d = np.linalg.norm(p0 + seg * t[:, None] - m, axis=1)
        d[~edge_ok] = np.inf
        for i in np.argsort(d)[:8]:
            if d[i] > radius:
                break
            e = self.bm.edges[int(i)]
            factor = self.edge_factor(e, mouse)
            if self.visible(e.verts[0].co.lerp(e.verts[1].co, factor)):
                return e, factor
        return None

    def nearest_edge_of_face(self, face, mouse):
        from mathutils import Vector
        from mathutils.geometry import intersect_point_line
        m = Vector(mouse)
        best, best_dist = None, float("inf")
        for e in face.edges:
            p0, p1 = self.to_screen(e.verts[0].co), self.to_screen(e.verts[1].co)
            if p0 is None or p1 is None:
                continue
            closest, t = intersect_point_line(m, p0, p1)
            if t < 0.0 or t > 1.0:
                closest = p0 if t < 0.0 else p1
            d = (closest - m).length
            if d < best_dist:
                best, best_dist = e, d
        if best is None:
            return None
        return best, self.edge_factor(best, mouse)

    def pick(self, mouse, ctrl, shift, snap_step):
        """What a click at `mouse` would do: `('LOOP', edge, t)`, a cut point, or None.

        Vertices and edges are found near the mouse even when it is off the mesh, as long as
        they can be seen.
        """
        hit = self.hit(mouse)
        vert = None if ctrl else self.nearest_vert(mouse)
        if vert is not None:
            return ('VERT', vert, None)
        edge = self.nearest_edge(mouse, _EDGE_SNAP_PX)
        if edge is None and ctrl and hit is not None:
            # Ctrl over a face: the loop goes through its closest edge.
            edge = self.nearest_edge_of_face(hit[0], mouse)
        if edge is not None:
            e, t = edge
            if shift:
                t = _snap(t, snap_step)
            if ctrl:
                return ('LOOP', e, min(max(t, 0.01), 0.99))
            return _normalize(('EDGE', e, t))
        if hit is not None and not ctrl:
            return ('FACE', hit[0], hit[1].copy())
        return None

    # Cutting across faces.

    def path(self, a, b):
        """The straight cut (as seen in the view) from point `a` to point `b` across the faces
        between them. Returns `(faces, crossings)`: the faces walked through in order and the edge
        points where the cut goes from one to the next, or None when it can't get there."""
        from mathutils.geometry import intersect_line_line_2d
        pa = self.to_screen(_point_co(a))
        pb = self.to_screen(_point_co(b))
        if pa is None or pb is None:
            return None
        seg_len = (pb - pa).length
        if seg_len < 1e-3:
            return None
        faces_b = set(_point_faces(b))
        best = None
        for face_start in _point_faces(a):
            face = face_start
            entry = a[1] if a[0] == 'EDGE' else None
            last = 0.0
            faces, crossings, seen = [face], [], set()
            reached = False
            for _ in range(10000):
                if face in faces_b:
                    reached = True
                    break
                found = None
                for e in face.edges:
                    if e == entry or e in seen:
                        continue
                    if a[0] == 'VERT' and face == face_start and a[1] in e.verts:
                        continue
                    p0, p1 = self.to_screen(e.verts[0].co), self.to_screen(e.verts[1].co)
                    if p0 is None or p1 is None:
                        continue
                    x = intersect_line_line_2d(pa, pb, p0, p1)
                    if x is None:
                        continue
                    param = (x - pa).length / seg_len
                    if param <= last + 1e-6:
                        continue
                    if found is None or param < found[0]:
                        found = (param, e, x)
                if found is None:
                    break
                param, e, x = found
                t = self.edge_factor(e, x)
                if t <= _END_EPSILON or t >= 1.0 - _END_EPSILON:
                    # Right through a vertex: not handled.
                    break
                crossings.append(('EDGE', e, t))
                seen.add(e)
                last = param
                others = [l.face for l in e.link_loops if l.face != face and not l.face.hide]
                if len(others) != 1:
                    break
                face = others[0]
                entry = e
                faces.append(face)
            if reached and (best is None or len(crossings) < len(best[1])):
                best = (faces, crossings)
        return best


def _bridge_islands(bm, face, edges):
    """Closed shapes in `edges` that touch no side of `face` get two edges to its sides, so the
    face can be split around them: the shape becomes a face, and the rest two faces. Lines that
    touch nothing stay as they are. New edges are added to `edges`."""
    sides = set(face.verts)
    links = {}
    for e in edges:
        a, b = e.verts
        links.setdefault(a, []).append(b)
        links.setdefault(b, []).append(a)
    seen = set()
    for start in list(links):
        if start in seen:
            continue
        island, todo = [], [start]
        seen.add(start)
        while todo:
            v = todo.pop()
            island.append(v)
            for w in links[v]:
                if w not in seen:
                    seen.add(w)
                    todo.append(w)
        if any(v in sides for v in island):
            continue
        if len(island) < 3 or any(len(links[v]) != 2 for v in island):
            continue
        a, side_a = min(((v, s) for v in island for s in sides), key=lambda p: (p[0].co - p[1].co).length)
        b = max(island, key=lambda v: (v.co - a.co).length)
        others = [s for s in sides if s != side_a]
        if not others:
            continue
        side_b = min(others, key=lambda s: (s.co - b.co).length)
        for v, s in ((a, side_a), (b, side_b)):
            if bm.edges.get((v, s)) is None:
                edges.append(bm.edges.new((v, s)))


def _snap(t, snap_step):
    step = snap_step / 100.0
    return min(max(round(t / step) * step, 0.0), 1.0)


# The picker is shared by the hover preview and the cut, so both hold the same edit BMesh.
_picker = None


def _picker_get(context):
    global _picker
    ob = context.edit_object
    if _picker is None or not _picker.valid_for(ob):
        _picker = _Picker(ob)
    _picker.set_view(context)
    return _picker


def _picker_dirty(_scene, depsgraph):
    if _picker is None:
        return
    for update in depsgraph.updates:
        if update.is_updated_geometry or isinstance(update.id, bpy.types.Mesh):
            _picker.dirty = True
            return


# -----------------------------------------------------------------------------
# Drawing

# World space positions to draw, and labels (world position, text). No mesh access while drawing.
_draw_data = {"cut": [], "points": [], "edge": [], "loop": [], "labels": [], "slice": [], "modal": False}
_HANDLES_KEY = "_mesh_multi_cut_handles"


def _draw_clear():
    for key in ("cut", "points", "edge", "loop", "labels", "slice"):
        _draw_data[key] = []


def _tool_active(context):
    try:
        tool = context.workspace.tools.from_space_view3d_mode(context.mode, create=False)
    except (AttributeError, TypeError):
        return False
    return tool is not None and tool.idname == TOOL_ID


def _draw_view():
    import gpu
    from gpu_extras.batch import batch_for_shader
    context = bpy.context
    if not (_draw_data["modal"] or (context.mode == 'EDIT_MESH' and _tool_active(context))):
        return
    region = context.region
    gpu.state.blend_set('ALPHA')
    gpu.state.depth_test_set('NONE')
    line = gpu.shader.from_builtin('POLYLINE_UNIFORM_COLOR')
    line.uniform_float("viewportSize", (region.width, region.height))
    for coords, color, width in (
            (_draw_data["edge"], _COLOR_EDGE, 4.0),
            (_draw_data["loop"], _COLOR_LOOP, 2.0),
            (_draw_data["cut"], _COLOR_CUT, 2.0),
    ):
        if coords:
            line.uniform_float("color", color)
            line.uniform_float("lineWidth", width)
            batch_for_shader(line, 'LINES', {"pos": coords}).draw(line)
    if _draw_data["points"]:
        point = gpu.shader.from_builtin('POINT_UNIFORM_COLOR')
        gpu.state.point_size_set(8.0)
        point.uniform_float("color", _COLOR_POINT)
        batch_for_shader(point, 'POINTS', {"pos": _draw_data["points"]}).draw(point)
        gpu.state.point_size_set(1.0)
    gpu.state.blend_set('NONE')


def _draw_labels():
    import blf
    from bpy_extras.view3d_utils import location_3d_to_region_2d
    context = bpy.context
    if not (_draw_data["modal"] or (context.mode == 'EDIT_MESH' and _tool_active(context))):
        return
    scale = context.preferences.system.ui_scale
    font = 0
    blf.size(font, 12.0 * scale)
    blf.color(font, *_COLOR_CUT)
    for co, text in _draw_data["labels"]:
        pos = location_3d_to_region_2d(context.region, context.region_data, co)
        if pos is None:
            continue
        blf.position(font, pos.x + 8.0 * scale, pos.y + 8.0 * scale, 0.0)
        blf.draw(font, text)
    if _draw_data["slice"]:
        import gpu
        from gpu_extras.batch import batch_for_shader
        gpu.state.blend_set('ALPHA')
        line = gpu.shader.from_builtin('POLYLINE_UNIFORM_COLOR')
        line.uniform_float("viewportSize", (context.region.width, context.region.height))
        line.uniform_float("color", _COLOR_CUT)
        line.uniform_float("lineWidth", 2.0)
        batch_for_shader(line, 'LINES', {"pos": _draw_data["slice"]}).draw(line)
        gpu.state.blend_set('NONE')


def _handlers_ensure():
    """Add the draw callbacks and the mesh change callback once. After a script reload the old
    ones are replaced, so they don't keep drawing old data."""
    ns = bpy.app.driver_namespace
    handles = ns.get(_HANDLES_KEY)
    if handles is not None and handles[0] is _draw_view:
        return
    if handles is not None:
        for handle, kind in handles[1]:
            try:
                bpy.types.SpaceView3D.draw_handler_remove(handle, 'WINDOW')
            except ValueError:
                pass
    updates = bpy.app.handlers.depsgraph_update_post
    for fn in list(updates):
        if getattr(fn, "__name__", "") == "_picker_dirty":
            updates.remove(fn)
    updates.append(_picker_dirty)
    ns[_HANDLES_KEY] = (_draw_view, [
        (bpy.types.SpaceView3D.draw_handler_add(_draw_view, (), 'WINDOW', 'POST_VIEW'), 'POST_VIEW'),
        (bpy.types.SpaceView3D.draw_handler_add(_draw_labels, (), 'WINDOW', 'POST_PIXEL'), 'POST_PIXEL'),
    ])


def _percent(t):
    return "{:d}%".format(round(t * 100.0))


def _set_hover_draw(picker, hover, ctrl_loop=True):
    """What the mouse is over, before any point is placed."""
    mat = picker.matrix
    _draw_clear()
    if hover is None:
        return
    kind, elem, value = hover
    if kind == 'LOOP':
        if ctrl_loop:
            for a, b in _ring_coords(elem, value):
                _draw_data["loop"] += [mat @ a, mat @ b]
            co = mat @ elem.verts[0].co.lerp(elem.verts[1].co, value)
            _draw_data["points"].append(co)
            _draw_data["labels"].append((co, _percent(value)))
        return
    co = mat @ _point_co(hover)
    _draw_data["points"].append(co)
    if kind == 'EDGE':
        _draw_data["edge"] = [mat @ elem.verts[0].co, mat @ elem.verts[1].co]
        _draw_data["labels"].append((co, _percent(value)))


# -----------------------------------------------------------------------------
# Operators

def _view3d_poll(context):
    ob = context.edit_object
    return (
        context.mode == 'EDIT_MESH' and ob is not None and ob.type == 'MESH' and
        context.area is not None and context.area.type == 'VIEW_3D' and
        context.region is not None and context.region.type == 'WINDOW' and context.region_data is not None
    )


def _tool_snap_step(context):
    try:
        tool = context.workspace.tools.from_space_view3d_mode(context.mode, create=False)
        return tool.operator_properties("mesh.multi_cut").snap_step
    except (AttributeError, TypeError, RuntimeError):
        return 25.0


class MESH_OT_multi_cut_hover(Operator):
    """Show what a Multi-Cut click would do under the mouse"""
    bl_idname = "mesh.multi_cut_hover"
    bl_label = "Multi-Cut Preview"
    bl_options = {'INTERNAL'}

    @classmethod
    def poll(cls, context):
        return _view3d_poll(context)

    def invoke(self, context, event):
        if _draw_data["modal"]:
            return {'PASS_THROUGH'}
        # Key events: the modifier state is the one after this press or release.
        ctrl, shift = event.ctrl, event.shift
        if event.type in {'LEFT_CTRL', 'RIGHT_CTRL'}:
            ctrl = event.value == 'PRESS'
        if event.type in {'LEFT_SHIFT', 'RIGHT_SHIFT'}:
            shift = event.value == 'PRESS'
        _handlers_ensure()
        try:
            picker = _picker_get(context)
            hover = picker.pick((event.mouse_region_x, event.mouse_region_y), ctrl, shift,
                                _tool_snap_step(context))
            _set_hover_draw(picker, hover)
        except (ReferenceError, IndexError, ValueError):
            _draw_clear()
        context.area.tag_redraw()
        return {'PASS_THROUGH'}


class MESH_OT_multi_cut(Operator):
    """Cut faces point by point: click on faces, edges and vertices, Enter to cut, Ctrl+click for an edge loop"""
    bl_idname = "mesh.multi_cut"
    bl_label = "Multi-Cut"
    bl_options = {'UNDO'}

    snap_step: FloatProperty(
        name="Snap Step",
        description="Step along the edge used while Shift is held",
        subtype='PERCENTAGE',
        default=25.0,
        min=1.0,
        max=50.0,
    )
    wait_for_input: BoolProperty(
        name="Wait for Input",
        description="Wait for the first click instead of using the click that started the cut",
        default=True,
        options={'HIDDEN', 'SKIP_SAVE'},
    )

    @classmethod
    def poll(cls, context):
        return _view3d_poll(context)

    # Points: a list of chains, each a list of points. A point that can't be reached from the
    # one before it starts a new chain.

    def _last(self):
        return self.chains[-1][-1] if self.chains and self.chains[-1] else None

    def _reachable(self, last, point):
        if _share_face(last, point):
            return True
        return self.picker.path(last, point) is not None

    def _add_point(self, point):
        """Place a point; returns true when it was placed."""
        last = self._last()
        if last is not None and _same_point(last, point):
            return False
        if last is None or self._reachable(last, point):
            if not self.chains:
                self.chains.append([])
            self.chains[-1].append(point)
        else:
            # Not reachable: start a new cut here.
            self.chains.append([point])
        return True

    def _snap_placed(self, mouse, hover):
        """A point placed inside a face, when the mouse is close to it, so a cut can come back to
        it, for example to close a shape."""
        if hover is not None and hover[0] in {'VERT', 'LOOP'}:
            return hover
        from mathutils import Vector
        last = self._last()
        m = Vector(mouse)
        best, best_dist = None, _VERT_SNAP_PX
        for chain in self.chains:
            for point in chain:
                if point[0] != 'FACE' or point is last:
                    continue
                pos = self.picker.to_screen(_point_co(point))
                if pos is None:
                    continue
                d = (pos - m).length
                if d < best_dist:
                    best, best_dist = point, d
        return best or hover

    def _remove_point(self):
        if self.chains and self.chains[-1]:
            self.chains[-1].pop()
        while self.chains and not self.chains[-1]:
            self.chains.pop()

    # Drawing.

    def _segment(self, a, b):
        """Object space positions from point `a` to `b`, through the edges a cut crosses."""
        cos = [_point_co(a)]
        if not _share_face(a, b):
            path = self.picker.path(a, b)
            if path is None:
                return None
            cos += [_point_co(c) for c in path[1]]
        cos.append(_point_co(b))
        return cos

    def _update_draw(self):
        picker = self.picker
        mat = picker.matrix
        _draw_clear()
        for chain in self.chains:
            for point in chain:
                _draw_data["points"].append(mat @ _point_co(point))
            for a, b in zip(chain, chain[1:]):
                cos = self._segment(a, b) or []
                for p, q in zip(cos, cos[1:]):
                    _draw_data["cut"] += [mat @ p, mat @ q]
                for co in cos[1:-1]:
                    _draw_data["points"].append(mat @ co)
        last = self._last()
        if last is not None and last[0] == 'EDGE':
            _draw_data["labels"].append((mat @ _point_co(last), _percent(last[2])))
        if self.dragging:
            return
        hover = self.hover
        if hover is None or hover[0] == 'LOOP':
            if hover is not None and last is None:
                _set_hover_draw(picker, hover)
            return
        if last is None:
            _set_hover_draw(picker, hover)
            return
        co = mat @ _point_co(hover)
        _draw_data["points"].append(co)
        if hover[0] == 'EDGE':
            _draw_data["edge"] = [mat @ hover[1].verts[0].co, mat @ hover[1].verts[1].co]
            _draw_data["labels"].append((co, _percent(hover[2])))
        if _same_point(last, hover):
            return
        cos = self._segment(last, hover)
        if cos is None:
            # Can't get there: this point starts a new cut.
            return
        for p, q in zip(cos, cos[1:]):
            _draw_data["cut"] += [mat @ p, mat @ q]
        for c in cos[1:-1]:
            _draw_data["points"].append(mat @ c)

    def _update_hover(self, context, event, ctrl=None):
        mouse = (event.mouse_region_x, event.mouse_region_y)
        ctrl = event.ctrl if ctrl is None else ctrl
        if event.type in {'LEFT_CTRL', 'RIGHT_CTRL'}:
            ctrl = event.value == 'PRESS'
        shift = event.shift
        if event.type in {'LEFT_SHIFT', 'RIGHT_SHIFT'}:
            shift = event.value == 'PRESS'
        # Ctrl only makes a loop before the first point.
        self.hover = self._snap_placed(
            mouse, self.picker.pick(mouse, ctrl and self._last() is None, shift, self.snap_step))
        self._update_draw()
        context.area.tag_redraw()

    def _drag(self, context, event):
        """Slide the last point along its edge."""
        chain = self.chains[-1]
        edge = self.drag_edge
        t = self.picker.edge_factor(edge, (event.mouse_region_x, event.mouse_region_y))
        if event.shift:
            t = _snap(t, self.snap_step)
        new = _normalize(('EDGE', edge, t))
        if len(chain) > 1 and not self._reachable(chain[-2], new):
            return
        chain[-1] = new
        self._update_draw()
        context.area.tag_redraw()

    # Changing the mesh.

    def _mesh_changed(self):
        import bmesh
        bmesh.update_edit_mesh(self.picker.ob.data, loop_triangles=True, destructive=True)
        self.picker.update()
        self.changed = True

    def _insert_loop(self, edge, t):
        """An edge loop through `edge`; returns true when the mesh changed."""
        import bmesh
        items, segments = _edge_ring(edge, edge.verts[0])
        if not segments:
            # No quad to cut through.
            return False
        verts = []
        for e, a in items:
            b = e.other_vert(a)
            co = a.co.lerp(b.co, t)
            _e, v = bmesh.utils.edge_split(e, a, t)
            v.co = co
            verts.append(v)
        bmesh.ops.connect_verts(self.picker.bm, verts=verts)
        for v in verts:
            v.select_set(True)
        self._mesh_changed()
        return True

    def _cut(self):
        """Make the cut through all placed points; returns true when the mesh changed."""
        import bmesh
        from bmesh.geometry import intersect_face_point
        bm = self.picker.bm

        # Edge points by their edge's original vertices, since the edges get split along the way.
        splits = {}

        def edge_vert(ends, t):
            va, vb = ends
            chain = splits.setdefault(ends, [(0.0, va), (1.0, vb)])
            for tt, v in chain:
                if abs(tt - t) < 1e-6:
                    return v
            for k in range(len(chain) - 1):
                (ta, ca), (tb, cb) = chain[k], chain[k + 1]
                if ta < t < tb:
                    e = bm.edges.get((ca, cb))
                    if e is None:
                        return None
                    u = (t - ta) / (tb - ta)
                    co = ca.co.lerp(cb.co, u)
                    _e, v = bmesh.utils.edge_split(e, ca, u)
                    v.co = co
                    chain.insert(k + 1, (t, v))
                    return v
            return None

        def split_live(edge, t):
            a = edge.verts[0]
            co = a.co.lerp(edge.verts[1].co, t)
            _e, v = bmesh.utils.edge_split(edge, a, t)
            v.co = co
            return v

        # Everything resolved before the mesh changes.
        resolved = []
        for chain in self.chains:
            items = []
            for point in chain:
                kind, elem, value = point
                if kind == 'VERT':
                    items.append(('V', elem, None, point))
                elif kind == 'EDGE':
                    items.append(('E', (elem.verts[0], elem.verts[1]), value, point))
                else:
                    items.append(('F', elem, value.copy(), point))
            resolved.append(items)

        # The new edges inside each face. Every face is split once, at the end, by all of them.
        nets = {}

        def add_edge(face, va, vb):
            if va == vb:
                return
            edge = bm.edges.get((va, vb))
            if edge is not None and face in edge.link_faces:
                # Along the side of the face: nothing to cut.
                return
            if edge is None:
                edge = bm.edges.new((va, vb))
            net = nets.setdefault(face, [])
            if edge not in net:
                net.append(edge)

        # Points inside faces become new vertices; a point used again (closing a shape) is the
        # same vertex.
        face_verts = {}
        changed = False
        for items in resolved:
            if len(items) < 2:
                if items and items[0][0] == 'E':
                    edge_vert(items[0][1], items[0][2])
                    changed = True
                continue
            points = []  # (vertex, point for finding the way across faces)
            for kind, elem, value, point in items:
                if kind == 'V':
                    points.append((elem, ('VERT', elem, None)))
                elif kind == 'E':
                    v = edge_vert(elem, value)
                    changed = True
                    points.append((v, ('VERT', v, None) if v is not None else None))
                else:
                    v = face_verts.get(id(point))
                    if v is None:
                        v = bm.verts.new(value)
                        face_verts[id(point)] = v
                    points.append((v, ('FACE', elem, value)))
            for (va, pa), (vb, pb) in zip(points, points[1:]):
                if va is None or vb is None or va == vb or not va.is_valid or not vb.is_valid:
                    continue
                if not pa[1].is_valid or not pb[1].is_valid:
                    continue
                shared = [f for f in set(_point_faces(pa)) & set(_point_faces(pb)) if not f.hide]
                if shared:
                    if len(shared) > 1:
                        probe = (va.co + vb.co) / 2.0
                        shared.sort(key=lambda f: not intersect_face_point(f, probe))
                    add_edge(shared[0], va, vb)
                    continue
                path = self.picker.path(pa, pb)
                if path is None:
                    continue
                faces, crossings = path
                chain_verts = [va] + [split_live(e, t) for _kind, e, t in crossings] + [vb]
                for face, a, b in zip(faces, chain_verts, chain_verts[1:]):
                    add_edge(face, a, b)

        for face, edges in nets.items():
            if not face.is_valid:
                continue
            edges = [e for e in edges if e.is_valid]
            if not edges:
                continue
            _bridge_islands(bm, face, edges)
            face.normal_update()
            try:
                bmesh.utils.face_split_edgenet(face, edges)
            except ValueError:
                pass
            changed = True
        if changed:
            self._mesh_changed()
        return changed

    # Operator.

    def _status(self, context):
        context.workspace.status_text_set(
            "Click: cut point on a face, edge or vertex  |  Drag: slide on edge  |  Shift: snap  |  Backspace: remove point  |  "
            "Enter / Right-click: cut  |  Ctrl+click: edge loop  |  Esc: cancel")

    def _end(self, context):
        _draw_data["modal"] = False
        _draw_clear()
        context.window.cursor_modal_restore()
        context.workspace.status_text_set(None)
        context.area.tag_redraw()
        self.hover = None
        self.chains = []

    def _slice(self, a, b):
        """Cut every shown face along the plane through the view rays at region positions `a`
        and `b`. Returns true when the mesh changed."""
        import bmesh
        picker = self.picker
        origin_a, dir_a = picker.ray(a)
        origin_b, dir_b = picker.ray(b)
        normal = dir_a.cross(origin_b + dir_b - origin_a)
        if normal.length < 1e-12:
            return False
        normal.normalize()
        bm = picker.bm
        faces = [f for f in bm.faces if not f.hide]
        if not faces:
            return False
        geom = list({v for f in faces for v in f.verts}) + list({e for f in faces for e in f.edges}) + faces
        result = bmesh.ops.bisect_plane(bm, geom=geom, dist=1e-6, plane_co=origin_a, plane_no=normal)
        cut = [e for e in result["geom_cut"] if isinstance(e, bmesh.types.BMEdge)]
        if not cut:
            return False
        for e in cut:
            e.select_set(True)
        self._mesh_changed()
        return True

    def _slice_draw(self, context):
        a, b = self.slice_start, self.slice_end
        _draw_clear()
        if b is not None:
            _draw_data["slice"] = [(a[0], a[1], 0.0), (b[0], b[1], 0.0)]
        context.area.tag_redraw()

    def _press(self, context, event):
        """A left click: a loop, a point, or the start of a slice. Returns false when nothing was
        placed."""
        self._update_hover(context, event)
        hover = self.hover
        if self._last() is None and (hover is None or hover[0] == 'FACE'):
            # On a face or empty space with no points: a drag from here slices. A click on a face
            # without dragging places a point there, when the button is released.
            self.slice_start = (event.mouse_region_x, event.mouse_region_y)
            self.slice_end = None
            self.press_point = hover
            return False
        if hover is None:
            return False
        if hover[0] == 'LOOP':
            self._insert_loop(hover[1], hover[2])
            self._update_hover(context, event)
            return True
        if not self._add_point(hover):
            return False
        if hover[0] == 'EDGE':
            self.dragging = True
            self.drag_edge = hover[1]
        self._update_draw()
        return True

    def invoke(self, context, event):
        _handlers_ensure()
        self.picker = _picker_get(context)
        self.chains = []
        self.hover = None
        self.changed = False
        self.dragging = False
        self.drag_edge = None
        self.slice_start = self.slice_end = None
        self.press_point = None
        # Started by a click with the tool: a click that places nothing ends it.
        self.from_click = event.type == 'LEFTMOUSE' and not self.wait_for_input
        _draw_data["modal"] = True
        # The knife cursor also when started from a menu, like the tool shows it.
        context.window.cursor_modal_set('KNIFE')

        if self.from_click:
            self._press(context, event)
            if self.changed and not self.chains:
                # A Ctrl+click loop: done.
                self._end(context)
                return {'FINISHED'}
            if not self.chains and self.slice_start is None:
                self._end(context)
                return {'CANCELLED'}
        else:
            self._update_hover(context, event)
        self._status(context)
        context.window_manager.modal_handler_add(self)
        return {'RUNNING_MODAL'}

    def modal(self, context, event):
        if not self.picker.valid_for(context.edit_object):
            self._end(context)
            return {'CANCELLED'}
        # Let the view be moved while cutting.
        if event.type in {
                'MIDDLEMOUSE', 'WHEELUPMOUSE', 'WHEELDOWNMOUSE', 'TRACKPADPAN', 'TRACKPADZOOM',
                'MOUSEROTATE', 'MOUSESMARTZOOM', 'NDOF_MOTION',
        } or (event.type.startswith('NUMPAD_') and event.type != 'NUMPAD_ENTER'):
            return {'PASS_THROUGH'}

        if event.type in {'MOUSEMOVE', 'INBETWEEN_MOUSEMOVE', 'LEFT_CTRL', 'RIGHT_CTRL',
                          'LEFT_SHIFT', 'RIGHT_SHIFT'}:
            self.picker.set_view(context)
            if self.slice_start is not None:
                a, m = self.slice_start, (event.mouse_region_x, event.mouse_region_y)
                scale = context.preferences.system.ui_scale
                if self.slice_end is not None or ((m[0] - a[0]) ** 2 + (m[1] - a[1]) ** 2) ** 0.5 > _SLICE_PX * scale:
                    self.slice_end = m
                    self._slice_draw(context)
            elif self.dragging:
                self._drag(context, event)
            else:
                self._update_hover(context, event)
            return {'RUNNING_MODAL'}

        if event.type == 'LEFTMOUSE':
            if event.value == 'RELEASE' and self.slice_start is not None:
                a, b = self.slice_start, self.slice_end
                self.slice_start = self.slice_end = None
                if b is not None:
                    self.picker.set_view(context)
                    self._slice(a, b)
                    changed = self.changed
                    self._end(context)
                    return {'FINISHED'} if changed else {'CANCELLED'}
                point, self.press_point = self.press_point, None
                if point is not None and point[0] == 'FACE':
                    # A click on a face: the first point of the cut is there.
                    self._add_point(point)
                    self._update_hover(context, event)
                    return {'RUNNING_MODAL'}
                if self.from_click and not self.chains:
                    # A click on a face or empty space that didn't drag: nothing to do.
                    changed = self.changed
                    self._end(context)
                    return {'FINISHED'} if changed else {'CANCELLED'}
                self._update_hover(context, event)
            elif event.value == 'RELEASE':
                self.dragging = False
                self._update_hover(context, event)
            elif event.value == 'PRESS':
                self.picker.set_view(context)
                self._press(context, event)
                context.area.tag_redraw()
            return {'RUNNING_MODAL'}

        if event.value != 'PRESS':
            return {'RUNNING_MODAL'}

        if event.type == 'BACK_SPACE':
            self.dragging = False
            self._remove_point()
            self._update_hover(context, event)
            return {'RUNNING_MODAL'}

        if self.slice_start is not None and event.type in {'ESC', 'RIGHTMOUSE'}:
            # Drop the slice.
            self.slice_start = self.slice_end = None
            self.press_point = None
            _draw_clear()
            context.area.tag_redraw()
            return {'RUNNING_MODAL'}

        if event.type in {'RET', 'NUMPAD_ENTER', 'SPACE', 'RIGHTMOUSE'}:
            self.picker.set_view(context)
            self._cut()
            changed = self.changed
            self._end(context)
            return {'FINISHED'} if changed else {'CANCELLED'}

        if event.type == 'ESC':
            changed = self.changed
            self._end(context)
            # Loops made with Ctrl+click stay (one undo step).
            return {'FINISHED'} if changed else {'CANCELLED'}

        return {'RUNNING_MODAL'}


classes = (
    MESH_OT_multi_cut_hover,
    MESH_OT_multi_cut,
)
