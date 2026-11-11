# SPDX-FileCopyrightText: 2026 Blender Authors
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""blender -b --factory-startup --python-exit-code 1 --python tests/python/bl_uv_scale.py"""

import os
import tempfile
import unittest

import bpy


def make_mesh(joined=False):
    mesh = bpy.data.meshes.new('UV test')
    if joined:
        mesh.from_pydata([(0, 0, 0), (1, 0, 0), (2, 0, 0),
                          (0, 1, 0), (1, 1, 0), (2, 1, 0)], [],
                         [(0, 1, 4, 3), (1, 2, 5, 4)])
        coords = [(0, 0), (1, 0), (1, 1), (0, 1),
                  (1, 0), (2, 0), (2, 1), (1, 1)]
    else:
        mesh.from_pydata([(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0),
                          (3, 0, 0), (5, 0, 0), (5, 2, 0), (3, 2, 0)], [],
                         [(0, 1, 2, 3), (4, 5, 6, 7)])
        coords = [(0, 0), (1, 0), (1, 1), (0, 1),
                  (2, 0), (3, 0), (3, 1), (2, 1)]
    layer = mesh.uv_layers.new(name='UVMap')
    for item, uv in zip(layer.data, coords):
        item.uv = uv
    obj = bpy.data.objects.new('UV test', mesh)
    bpy.context.collection.objects.link(obj)
    return obj


def evaluated_uvs(obj, name='UVMap'):
    graph = bpy.context.evaluated_depsgraph_get()
    graph.update()
    evaluated = obj.evaluated_get(graph)
    mesh = evaluated.to_mesh()
    try:
        return [tuple(item.uv) for item in mesh.uv_layers[name].data]
    finally:
        evaluated.to_mesh_clear()


def area(coords):
    return abs(sum(a[0] * b[1] - b[0] * a[1]
                   for a, b in zip(coords, coords[1:] + coords[:1]))) * 0.5


def center(coords):
    return tuple((min(p[i] for p in coords) + max(p[i] for p in coords)) * .5
                 for i in range(2))


def make_node(obj):
    group = bpy.data.node_groups.new('UV Scale test', 'GeometryNodeTree')
    group.interface.new_socket('Geometry', in_out='INPUT', socket_type='NodeSocketGeometry')
    group.interface.new_socket('Geometry', in_out='OUTPUT', socket_type='NodeSocketGeometry')
    source = group.nodes.new('NodeGroupInput')
    target = group.nodes.new('NodeGroupOutput')
    node = group.nodes.new('GeometryNodeUVScale')
    group.links.new(source.outputs[0], node.inputs['Mesh'])
    group.links.new(node.outputs['Mesh'], target.inputs[0])
    obj.modifiers.new('Nodes', 'NODES').node_group = group
    return node


class UVScaleTests(unittest.TestCase):
    def setUp(self):
        bpy.ops.wm.read_factory_settings(use_empty=True)
        self.obj = make_mesh()
        self.mod = self.obj.modifiers.new('UV Scale', 'UV_SCALE')

    def assertUVs(self, actual, expected):
        self.assertEqual(len(actual), len(expected))
        for p, q in zip(actual, expected):
            for a, b in zip(p, q):
                self.assertAlmostEqual(a, b, places=5)

    def test_individual_scaling_and_source_preservation(self):
        original = [tuple(item.uv) for item in self.obj.data.uv_layers.active.data]
        self.mod.scale = (2, .5)
        result = evaluated_uvs(self.obj)
        for start in (0, 4):
            pivot = center(original[start:start + 4])
            expected = [(pivot[0] + (u - pivot[0]) * 2,
                         pivot[1] + (v - pivot[1]) * .5)
                        for u, v in original[start:start + 4]]
            self.assertUVs(result[start:start + 4], expected)
        self.assertUVs([tuple(item.uv) for item in self.obj.data.uv_layers.active.data], original)
        self.assertUVs(evaluated_uvs(self.obj), result)  # Evaluating twice must not compound scale.

    def test_global_and_custom_pivots(self):
        self.mod.use_individual_islands = False
        self.mod.scale = (2, 2)
        self.assertUVs(evaluated_uvs(self.obj)[:1], [(-1.5, -.5)])
        self.mod.pivot = 'ORIGIN'
        self.assertUVs(evaluated_uvs(self.obj)[4:5], [(4, 0)])
        self.mod.pivot = 'CUSTOM'
        self.mod.center = (1, 1)
        self.assertUVs(evaluated_uvs(self.obj)[:1], [(-1, -1)])

    def test_uv_continuity_not_face_or_seam_flags(self):
        obj = make_mesh(joined=True)
        mod = obj.modifiers.new('UV Scale', 'UV_SCALE')
        mod.scale = (2, 2)
        for edge in obj.data.edges:
            edge.use_seam = True  # UV continuity, rather than seam markings, defines the island.
        result = evaluated_uvs(obj)
        self.assertUVs(result[:1], [(-1, -.5)])
        self.assertUVs(result[1:2], result[4:5])
        for item in obj.data.uv_layers.active.data[4:]:
            item.uv.x += 2
        obj.data.update()
        self.assertUVs(evaluated_uvs(obj)[:1], [(-.5, -.5)])

    def test_density_matches_different_objects_and_island_areas(self):
        self.mod.mode = 'DENSITY'
        self.mod.density = 512
        result = evaluated_uvs(self.obj)
        self.assertAlmostEqual(area(result[:4]), .25, places=5)
        self.assertAlmostEqual(area(result[4:]), 1, places=5)
        self.assertEqual(center(result[:4]), (.5, .5))
        self.assertEqual(center(result[4:]), (2.5, .5))
        other = make_mesh()
        other.scale = (3, 2, 4)
        other_mod = other.modifiers.new('UV Scale', 'UV_SCALE')
        other_mod.mode = 'DENSITY'
        other_mod.density = 512
        result = evaluated_uvs(other)
        self.assertAlmostEqual(area(result[:4]), .25 * 6, places=5)
        self.assertAlmostEqual(area(result[4:]), 6, places=5)
        # Object transform updates must invalidate the modifier evaluation.
        other.scale = (2, 2, 2)
        self.assertAlmostEqual(area(evaluated_uvs(other)[:4]), 1, places=5)

    def test_rectangular_texture_scene_units_and_global_density(self):
        self.mod.mode = 'DENSITY'
        self.mod.density = 512
        self.mod.resolution = (1024, 2048)
        result = evaluated_uvs(self.obj)
        self.assertAlmostEqual(area(result[:4]), .125, places=5)
        bpy.context.scene.unit_settings.system = 'METRIC'
        bpy.context.scene.unit_settings.scale_length = .01
        self.assertAlmostEqual(area(evaluated_uvs(self.obj)[:4]), .125 * .0001, places=8)
        bpy.context.scene.unit_settings.scale_length = 1
        self.mod.use_individual_islands = False
        result = evaluated_uvs(self.obj)
        # Global mode targets the aggregate density while keeping island proportions.
        self.assertAlmostEqual(area(result[:4]), .3125, places=5)
        self.assertAlmostEqual(area(result[4:]), .3125, places=5)

    def test_mirrored_degenerate_and_named_maps(self):
        self.mod.mode = 'DENSITY'
        self.mod.density = 512
        layer = self.obj.data.uv_layers.active
        for item in layer.data[:4]:
            item.uv.x = -item.uv.x
        for item in layer.data[4:]:
            item.uv = (3, 2)
        self.obj.data.update()
        result = evaluated_uvs(self.obj)
        self.assertAlmostEqual(area(result[:4]), .25, places=5)
        self.assertUVs(result[4:], [(3, 2)] * 4)
        secondary = self.obj.data.uv_layers.new(name='Secondary')
        for item in secondary.data:
            item.uv = (.25, .75)
        self.mod.mode = 'SCALE'
        self.mod.pivot = 'ORIGIN'
        self.mod.scale = (2, 2)
        self.mod.uv_map = 'Secondary'
        self.assertUVs(evaluated_uvs(self.obj, 'Secondary'), [(.5, 1.5)] * 8)
        self.assertUVs(evaluated_uvs(self.obj),
                       [tuple(item.uv) for item in self.obj.data.uv_layers['UVMap'].data])
        self.mod.uv_map = 'Does not exist'
        self.assertEqual(len(evaluated_uvs(self.obj)), 8)

    def test_node_parity_and_dependencies(self):
        node_obj = make_mesh()
        node = make_node(node_obj)
        self.mod.scale = (1.7, .6)
        node.inputs['Scale'].default_value = (1.7, .6)
        self.assertUVs(evaluated_uvs(node_obj), evaluated_uvs(self.obj))
        self.mod.use_individual_islands = False
        node.inputs['Individual Islands'].default_value = False
        self.mod.pivot = 'CUSTOM'
        self.mod.center = (.2, .8)
        node.inputs['Pivot'].default_value = 'Custom'
        node.inputs['Pivot Position'].default_value = (.2, .8)
        self.assertUVs(evaluated_uvs(node_obj), evaluated_uvs(self.obj))
        self.mod.use_individual_islands = True
        node.inputs['Individual Islands'].default_value = True
        self.mod.pivot = 'CENTER'
        node.inputs['Pivot'].default_value = 'Bounds Center'
        self.mod.uv_map = 'UVMap'
        node.inputs['UV Map'].default_value = 'UVMap'
        self.mod.mode = 'DENSITY'
        node.inputs['Mode'].default_value = 'Texel Density'
        self.mod.density = 300
        node.inputs['Density'].default_value = 300
        self.mod.resolution = (2048, 1024)
        node.inputs['Texture Width'].default_value = 2048
        for scale in [(1, 1, 1), (3, 2, 1), (-2, 3, 1)]:
            self.obj.scale = node_obj.scale = scale
            self.assertUVs(evaluated_uvs(node_obj), evaluated_uvs(self.obj))
        bpy.context.scene.unit_settings.scale_length = .1
        self.assertUVs(evaluated_uvs(node_obj), evaluated_uvs(self.obj))

    def test_missing_uv_and_roundtrip(self):
        self.obj.data.uv_layers.remove(self.obj.data.uv_layers.active)
        # No UV map is created, and the mesh is still available.
        graph = bpy.context.evaluated_depsgraph_get()
        self.assertEqual(len(self.obj.evaluated_get(graph).data.vertices), 8)
        self.assertEqual(len(self.obj.evaluated_get(graph).data.uv_layers), 0)
        self.obj.data.uv_layers.new(name='UVMap')
        self.mod.scale = (1.25, 2.5)
        self.mod.center = (.2, .7)
        self.mod.pivot = 'CUSTOM'
        make_node(self.obj)
        name = self.obj.name
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, 'uv_scale.blend')
            bpy.ops.wm.save_as_mainfile(filepath=path)
            bpy.ops.wm.open_mainfile(filepath=path)
        obj = bpy.data.objects[name]
        self.assertEqual(obj.modifiers[0].type, 'UV_SCALE')
        self.assertEqual(tuple(obj.modifiers[0].scale), (1.25, 2.5))
        self.assertEqual(obj.modifiers[0].pivot, 'CUSTOM')
        self.assertTrue(any(n.bl_idname == 'GeometryNodeUVScale'
                            for n in obj.modifiers[1].node_group.nodes))


if __name__ == '__main__':
    result = unittest.TextTestRunner(verbosity=2).run(
        unittest.defaultTestLoader.loadTestsFromTestCase(UVScaleTests))
    if not result.wasSuccessful():
        raise SystemExit(1)
