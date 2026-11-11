# FFD 2x2x2, 3x3x3, 4x4x4

<img src="images/icons/mod_ffd_2.png" width="32" height="32" alt="FFD 2x2x2, 3x3x3, 4x4x4"> <img src="images/icons/mod_ffd_3.png" width="32" height="32" alt="FFD 2x2x2, 3x3x3, 4x4x4"> <img src="images/icons/mod_ffd_4.png" width="32" height="32" alt="FFD 2x2x2, 3x3x3, 4x4x4">

Bends the shape with a box of control points around it: 2, 3 or 4 points on each side. Move the
points in the viewport and the mesh inside follows smoothly. It's like a Lattice modifier, without a
lattice object to make and keep next to the mesh.

Add it from **Add Modifier ▸ Deform**. It works on meshes, curves, surfaces and text. In the [Modifier List](MODIFIER_LIST.md)'s Add menu it's in the **Deform** column.

## How to use it

Add one of the three, and make it the active modifier. A dot appears on every control point, with
the cage drawn between them. Drag a dot to move that point; the cage stays drawn while you drag.
**Reset Points** puts all points back. See [Modifier gizmos](MODIFIER_GIZMOS.md).

The box fits around the shape by itself. With **Custom Box** you set its corners yourself.

## Settings

- **Custom Box**: Use the box below, instead of fitting the box around the shape.
- **Box Min**: Lowest corner of the box, in object space.
- **Box Max**: Highest corner of the box, in object space.
- **Inside Only**: Only change points inside the box.
- **Vertex Group**: Vertex group that controls how much each point is affected.

## As a node

Geometry Nodes has an **FFD** node that shares its code with these modifiers.

## In Python

```python
mod = obj.modifiers.new("FFD 2x2x2", 'FFD_2')
mod = obj.modifiers.new("FFD 3x3x3", 'FFD_3')
mod = obj.modifiers.new("FFD 4x4x4", 'FFD_4')
```

The Python types are `FFD2Modifier`, `FFD3Modifier` and `FFD4Modifier`. Official Blender doesn't know these modifiers, so a file that uses them won't give the same result there.
