# SPDX-FileCopyrightText: 2026 Blender Authors
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""Run with Blender --background --factory-startup --python-exit-code 1 --python this_file."""

import math

import bpy


def snapshot(obj):
    evaluated = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
    mesh = evaluated.to_mesh()
    try:
        assert mesh is not None
        points = [tuple(v.co) for v in mesh.vertices]
        assert all(math.isfinite(c) for point in points for c in point)
        return points, [tuple(p.vertices) for p in mesh.polygons]
    finally:
        evaluated.to_mesh_clear()


def node_modifier(obj, node_id):
    tree = bpy.data.node_groups.new(node_id, 'GeometryNodeTree')
    tree.interface.new_socket('Geometry', in_out='INPUT', socket_type='NodeSocketGeometry')
    tree.interface.new_socket('Geometry', in_out='OUTPUT', socket_type='NodeSocketGeometry')
    source = tree.nodes.new('NodeGroupInput')
    target = tree.nodes.new('NodeGroupOutput')
    node = tree.nodes.new(node_id)
    tree.links.new(source.outputs[0], node.inputs['Mesh'])
    tree.links.new(node.outputs['Mesh'], target.inputs[0])
    modifier = obj.modifiers.new('Nodes', 'NODES')
    modifier.node_group = tree
    return modifier, tree


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    for modifier_type, node_id in (
            ('DICE', 'GeometryNodeDiceMesh'),
            ('DISSOLVE', 'GeometryNodeDissolveMesh'),
            ('MASONRY', 'GeometryNodeMasonryMesh')):
        bpy.ops.mesh.primitive_cube_add()
        obj = bpy.context.object
        modifier = obj.modifiers.new(modifier_type, modifier_type)
        result = snapshot(obj)
        assert result[0] and result[1], modifier_type
        if modifier_type == 'DICE':
            assert len(result[1]) == 48, '2x2x2 dice should create eight closed cubes'
        if modifier_type == 'MASONRY':
            for item in modifier.bl_rna.properties['pattern'].enum_items:
                modifier.pattern = item.identifier
                assert snapshot(obj)[1], item.identifier
            modifier.pattern = 'RUNNING'
            result = snapshot(obj)
        obj.modifiers.remove(modifier)
        modifier, tree = node_modifier(obj, node_id)
        if modifier_type == 'DISSOLVE':
            # The modifier starts in Limited mode; the node starts in Edges mode.
            node = next(n for n in tree.nodes if n.bl_idname == node_id)
            node.inputs['Mode'].default_value = 'Limited'
        node_result = snapshot(obj)
        assert len(result[0]) == len(node_result[0]), modifier_type
        assert result[1] == node_result[1], modifier_type
        assert all(abs(a - b) < 1e-5
                   for p, q in zip(result[0], node_result[0])
                   for a, b in zip(p, q)), modifier_type
        bpy.data.objects.remove(obj, do_unlink=True)
        bpy.data.node_groups.remove(tree)
        print('PASS modifier/node parity:', modifier_type)

    bpy.ops.mesh.primitive_cube_add()
    obj = bpy.context.object
    modifier = obj.modifiers.new('Edit Poly', 'EDIT_POLY')
    bpy.ops.object.edit_poly_start(modifier=modifier.name)
    assert modifier.is_editing and obj.mode == 'EDIT'
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.mesh.extrude_region_move(TRANSFORM_OT_translate={'value': (0, 0, 1)})
    bpy.ops.object.edit_poly_finish(modifier=modifier.name)
    assert not modifier.is_editing and obj.mode == 'OBJECT'
    assert len(obj.data.vertices) == 8, 'Original mesh must remain intact'
    assert len(snapshot(obj)[0]) == 16, 'Edited mesh must be used by the stack'
    print('PASS Edit Poly editing lifecycle')


if __name__ == '__main__':
    main()
