# SPDX-FileCopyrightText: 2026 Blender Authors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run with Blender --background --factory-startup --python-exit-code 1 --python this_file."""
import math
import os
import tempfile
import time
import unittest
import bpy

BACKENDS = ('BLENDER', 'PMP', 'MESHOPT', 'AGGRESSIVE', 'CGAL', 'WILD')

def sphere():
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=3)
    obj = bpy.context.object
    for face in obj.data.polygons:
        face.use_smooth = True
    return obj

def modifier(obj, backend):
    mod = obj.modifiers.new('Decimate Test', 'DECIMATE')
    mod.backend = backend
    mod.ratio = .4
    for name in ('boundary', 'uv', 'materials', 'sharp'):
        setattr(mod, 'use_preserve_' + name, False)
    return mod

def evaluate(obj):
    bpy.context.view_layer.update()
    evaluated = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
    result = bpy.data.meshes.new_from_object(evaluated)
    return result

class DecimateBackends(unittest.TestCase):
    def setUp(self):
        bpy.ops.wm.read_factory_settings(use_empty=True)

    def test_all_algorithms_reduce_and_are_repeatable(self):
        for backend in BACKENDS:
            with self.subTest(backend=backend):
                obj = sphere()
                original = len(obj.data.polygons)
                mod = modifier(obj, backend)
                start = time.perf_counter()
                result = evaluate(obj)
                print('DECIMATE', backend, original, '->', len(result.polygons),
                      'seconds', round(time.perf_counter() - start, 4), flush=True)
                self.assertGreater(len(result.polygons), 0)
                self.assertLess(len(result.polygons), original * .7)
                self.assertEqual(len(obj.data.polygons), original)
                for vertex in result.vertices:
                    self.assertTrue(all(math.isfinite(x) for x in vertex.co))
                expected = [(tuple(v.co)) for v in result.vertices]
                mod.show_viewport = False
                bpy.context.view_layer.update()
                mod.show_viewport = True
                again = evaluate(obj)
                self.assertEqual(expected, [tuple(v.co) for v in again.vertices])
                self.assertEqual([tuple(f.vertices) for f in result.polygons],
                                 [tuple(f.vertices) for f in again.polygons])
                bpy.data.objects.remove(obj, do_unlink=True)

    def test_triangle_target_and_noop(self):
        for backend in BACKENDS:
            with self.subTest(backend=backend):
                obj = sphere()
                mod = modifier(obj, backend)
                mod.ratio = 1
                self.assertEqual(len(evaluate(obj).polygons), len(obj.data.polygons))
                mod.target_mode = 'FACE_COUNT'
                mod.target_face_count = 100
                count = len(evaluate(obj).polygons)
                self.assertLessEqual(count, 102)
                self.assertGreater(count, 0)
                mod.target_face_count = 0
                # Original BMesh collapse can retain a two-triangle minimum.
                self.assertLessEqual(len(evaluate(obj).polygons), 2 if backend == 'BLENDER' else 0)
                bpy.data.objects.remove(obj, do_unlink=True)

    def test_group_protects_unselected_vertices(self):
        for backend in BACKENDS[1:]:
            with self.subTest(backend=backend):
                obj = sphere()
                group = obj.vertex_groups.new(name='Only Top')
                selected = [v.index for v in obj.data.vertices if v.co.z > .2]
                group.add(selected, 1, 'REPLACE')
                protected = [tuple(v.co) for v in obj.data.vertices if v.co.z <= .2]
                mod = modifier(obj, backend)
                mod.vertex_group = group.name
                output = {tuple(v.co) for v in evaluate(obj).vertices}
                self.assertTrue(set(protected).issubset(output))
                bpy.data.objects.remove(obj, do_unlink=True)

    def test_attributes_and_uv_charts(self):
        for backend in BACKENDS[1:]:
            with self.subTest(backend=backend):
                obj = sphere()
                mesh = obj.data
                for name, shift in [('UVMap', 0), ('Second', 20)]:
                    uv = mesh.uv_layers.get(name) or mesh.uv_layers.new(name=name)
                    for face in mesh.polygons:
                        offset = (10 if face.center.z > 0 else 0) + shift
                        for corner in face.loop_indices:
                            co = mesh.vertices[mesh.loops[corner].vertex_index].co
                            uv.data[corner].uv = (co.x * .1 + offset, co.y * .1)
                attr = mesh.color_attributes.new(name='Color', type='FLOAT_COLOR', domain='CORNER')
                for item in attr.data:
                    item.color = (.2, .4, .6, 1)
                mat = bpy.data.materials.new('Material')
                mesh.materials.append(mat)
                mod = modifier(obj, backend)
                mod.use_preserve_uv = True
                result = evaluate(obj)
                self.assertIn('Color', result.color_attributes)
                self.assertIn('Second', result.uv_layers)
                for layer in result.uv_layers:
                    for face in result.polygons:
                        xs = [layer.data[i].uv.x for i in face.loop_indices]
                        self.assertLess(max(xs) - min(xs), 1)
                self.assertEqual(len(result.materials), 1)
                bpy.data.objects.remove(obj, do_unlink=True)

    def test_empty_and_nonmanifold(self):
        for backend in BACKENDS[1:]:
            mesh = bpy.data.meshes.new('Nonmanifold')
            mesh.from_pydata([(0,0,0),(1,0,0),(0,1,0),(0,-1,0),(0,0,1)], [],
                             [(0,1,2),(1,0,3),(0,1,4)])
            obj = bpy.data.objects.new('Nonmanifold', mesh)
            bpy.context.collection.objects.link(obj)
            modifier(obj, backend)
            result = evaluate(obj)
            self.assertTrue(all(all(math.isfinite(c) for c in v.co) for v in result.vertices))
            bpy.data.objects.remove(obj, do_unlink=True)
        mesh = bpy.data.meshes.new('Empty')
        obj = bpy.data.objects.new('Empty', mesh)
        bpy.context.collection.objects.link(obj)
        for backend in BACKENDS:
            mod = modifier(obj, backend)
            self.assertEqual(len(evaluate(obj).polygons), 0)
            obj.modifiers.remove(mod)

    def test_wild_virtual_pairs(self):
        obj = sphere()
        original = obj.data
        points = [tuple(v.co) for v in original.vertices]
        count = len(points)
        points += [(x + 2.01, y, z) for x, y, z in points]
        faces = [tuple(p.vertices) for p in original.polygons]
        faces += [tuple(v + count for v in f) for f in faces]
        mesh = bpy.data.meshes.new('Two components')
        mesh.from_pydata(points, [], faces)
        obj.data = mesh
        mod = modifier(obj, 'WILD')
        mod.virtual_distance = .05
        result = evaluate(obj)
        self.assertGreater(len(result.polygons), 0)
        self.assertLess(len(result.polygons), len(faces))
        self.assertTrue(all(all(math.isfinite(c) for c in v.co) for v in result.vertices))

    def test_save_reload(self):
        obj = sphere()
        mod = modifier(obj, 'WILD')
        mod.target_mode = 'FACE_COUNT'
        mod.target_face_count = 234
        mod.virtual_distance = .01
        mod.uv_map = 'Second'
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, 'decimate.blend')
            bpy.ops.wm.save_as_mainfile(filepath=path)
            bpy.ops.wm.open_mainfile(filepath=path)
            mod = bpy.context.object.modifiers[0]
            self.assertEqual(mod.backend, 'WILD')
            self.assertEqual(mod.target_face_count, 234)
            self.assertEqual(mod.uv_map, 'Second')
            self.assertAlmostEqual(mod.virtual_distance, .01)

    def test_legacy_file_defaults(self):
        path = os.environ.get('DECIMATE_LEGACY_FILE')
        if not path:
            self.skipTest('DECIMATE_LEGACY_FILE not supplied')
        bpy.ops.wm.open_mainfile(filepath=path)
        mod = bpy.context.object.modifiers[0]
        self.assertEqual(mod.backend, 'BLENDER')
        self.assertEqual(mod.target_mode, 'RATIO')
        self.assertAlmostEqual(mod.ratio, .375)
        self.assertTrue(mod.use_symmetry)
        self.assertEqual(mod.symmetry_axis, 'Y')
        self.assertTrue(mod.use_preserve_boundary)
        self.assertTrue(mod.use_preserve_uv)
        self.assertAlmostEqual(mod.max_aspect_ratio, 10)

if __name__ == '__main__':
    result = unittest.TextTestRunner(verbosity=2).run(
        unittest.defaultTestLoader.loadTestsFromTestCase(DecimateBackends))
    if not result.wasSuccessful():
        raise SystemExit(1)
