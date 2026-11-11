# SPDX-FileCopyrightText: 2026 Blender Authors
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""Run with blender -b --factory-startup --python-exit-code 1 --python this_file."""

import math
import unittest

import bpy
import bmesh


def wall(path, name='Wall', closed=False):
    vertices = [(x, y, z) for z in (0, 1.2) for x, y in path]
    n = len(path)
    faces = [(i, (i + 1) % n, (i + 1) % n + n, i + n)
             for i in range(n if closed else n - 1)]
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(vertices, [], faces)
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    return obj


def rounded(segments):
    return [(-1.4, 0)] + [(-.4 + .4 * math.cos(-math.pi / 2 + i * math.pi / 2 / segments),
                           .4 + .4 * math.sin(-math.pi / 2 + i * math.pi / 2 / segments))
                          for i in range(segments + 1)] + [(0, 1.4)]


def add_modifier(obj, continuous):
    mod = obj.modifiers.new('Masonry', 'MASONRY')
    mod.length = .32
    mod.height = .13
    mod.depth = .045
    mod.mortar = .01
    mod.use_backing = False
    assert not mod.use_continuous_walls, 'Existing files must retain per-face behavior by default'
    mod.use_continuous_walls = continuous
    return mod


def result(obj):
    graph = bpy.context.evaluated_depsgraph_get()
    graph.update()
    ev = obj.evaluated_get(graph)
    mesh = ev.to_mesh()
    try:
        bm = bmesh.new()
        bm.from_mesh(mesh)
        assert all(e.is_manifold for e in bm.edges), 'Every generated brick must be closed'
        visited = set()
        islands = []
        for vert in bm.verts:
            if vert in visited:
                continue
            component = [vert]
            visited.add(vert)
            for v in component:
                for edge in v.link_edges:
                    other = edge.other_vert(v)
                    if other not in visited:
                        visited.add(other)
                        component.append(other)
            islands.append(component)
        coords = sorted(tuple(round(c, 5) for c in v.co) for v in bm.verts)
        assert all(math.isfinite(c) for p in coords for c in p)
        count = len(islands)
        bm.free()
        return count, coords
    finally:
        ev.to_mesh_clear()


class ContinuousMasonryTests(unittest.TestCase):
    def setUp(self):
        bpy.ops.wm.read_factory_settings(use_empty=True)

    def test_flat_subdivisions_do_not_create_more_bricks(self):
        counts = []
        for segments in (1, 4, 24):
            obj = wall([(-1.4 + 2.8 * i / segments, 0) for i in range(segments + 1)])
            mod = add_modifier(obj, True)
            count, _ = result(obj)
            counts.append(count)
            if segments == 24:
                mod.use_continuous_walls = False
                self.assertGreater(result(obj)[0], count * 2)
        self.assertEqual(counts, [counts[0]] * 3)

    def test_bevel_segment_count_does_not_fragment_bricks(self):
        counts = []
        for segments in (4, 8, 24):
            obj = wall(rounded(segments))
            mod = add_modifier(obj, True)
            count, _ = result(obj)
            counts.append(count)
            mod.use_continuous_walls = False
            legacy_count, _ = result(obj)
            self.assertLess(count, legacy_count)
            print('Bevel segments:', segments, 'continuous:', count, 'per-face:', legacy_count)
        self.assertLessEqual(max(counts) - min(counts), 2)

    def test_sharp_edge_and_right_angle_break_the_layout(self):
        obj = wall([(-1.4, 0), (0, 0), (1.4, 0)])
        add_modifier(obj, True)
        joined_count = result(obj)[0]
        sharp = obj.data.attributes.new('sharp_edge', 'BOOLEAN', 'EDGE')
        for edge in obj.data.edges:
            a, b = [obj.data.vertices[v].co for v in edge.vertices]
            if abs(a.x) < 1e-6 and abs(b.x) < 1e-6:
                sharp.data[edge.index].value = True
        obj.data.update()
        self.assertGreater(result(obj)[0], joined_count)
        corner = wall([(-1.4, 0), (0, 0), (0, 1.4)])
        mod = add_modifier(corner, True)
        continuous = result(corner)
        mod.use_continuous_walls = False
        self.assertEqual(result(corner), continuous)

    def test_closed_round_wall_has_one_layout_seam(self):
        obj = wall([(math.cos(i * math.tau / 32), math.sin(i * math.tau / 32))
                    for i in range(32)], closed=True)
        mod = add_modifier(obj, True)
        continuous = result(obj)[0]
        mod.use_continuous_walls = False
        legacy = result(obj)[0]
        print('Round wall continuous:', continuous, 'per-face:', legacy)
        self.assertLess(continuous, legacy * .8)

    def test_modifier_node_match_and_preserve_source(self):
        obj = wall(rounded(12))
        mod = add_modifier(obj, True)
        mod.random_depth = .2
        mod.tilt = .02
        mod.angle = .1
        expected = result(obj)
        self.assertEqual(len(obj.data.polygons), 14)
        obj.modifiers.remove(mod)
        group = bpy.data.node_groups.new('Masonry', 'GeometryNodeTree')
        group.interface.new_socket('Geometry', in_out='INPUT', socket_type='NodeSocketGeometry')
        group.interface.new_socket('Geometry', in_out='OUTPUT', socket_type='NodeSocketGeometry')
        source, target = group.nodes.new('NodeGroupInput'), group.nodes.new('NodeGroupOutput')
        node = group.nodes.new('GeometryNodeMasonryMesh')
        for key, value in {'Length': .32, 'Height': .13, 'Depth': .045, 'Mortar': .01,
                           'Backing': False, 'Continuous Walls': True,
                           'Random Depth': .2, 'Tilt': .02, 'Angle': .1}.items():
            node.inputs[key].default_value = value
        group.links.new(source.outputs[0], node.inputs['Mesh'])
        group.links.new(node.outputs['Mesh'], target.inputs[0])
        obj.modifiers.new('Nodes', 'NODES').node_group = group
        self.assertEqual(result(obj), expected)

    def test_wall_with_opening_keeps_safe_per_face_behavior(self):
        mesh = bpy.data.meshes.new('Wall with opening')
        vertices = [(x * .8, 0, z * .4) for z in range(4) for x in range(4)]
        faces = []
        for z in range(3):
            for x in range(3):
                if (x, z) == (1, 1):
                    continue
                a = z * 4 + x
                faces.append((a, a + 1, a + 5, a + 4))
        mesh.from_pydata(vertices, [], faces)
        obj = bpy.data.objects.new('Wall with opening', mesh)
        bpy.context.collection.objects.link(obj)
        mod = add_modifier(obj, True)
        continuous = result(obj)
        mod.use_continuous_walls = False
        self.assertEqual(result(obj), continuous)


if __name__ == '__main__':
    outcome = unittest.TextTestRunner(verbosity=2).run(
        unittest.defaultTestLoader.loadTestsFromTestCase(ContinuousMasonryTests))
    if not outcome.wasSuccessful():
        raise SystemExit(1)
