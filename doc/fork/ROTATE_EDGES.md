# Rotate Edges

<img src="images/icons/mod_rotate_edges.png" width="32" height="32" alt="Rotate Edges">

Turns the edges between faces so they connect the other corners, like **Edge ▸ Rotate Edge** in Edit
Mode, on all edges or the edges in a vertex group. Useful to change the direction of triangles
without editing the mesh.

Add it from **Add Modifier ▸ Generate**. It works on meshes. In the [Modifier List](MODIFIER_LIST.md)'s Add menu it's in the **Topology** column.

## Settings

- **Steps**: How many times to rotate the edges.
- **Counter Clockwise**: Rotate the other way.
- **Vertex Group**: Only rotate edges between faces whose points are all in this group.

## As a node

Geometry Nodes has a **Rotate Edges** node that shares its code with this modifier, so a node tree and the modifier give the same result.

## In Python

```python
mod = obj.modifiers.new("Rotate Edges", 'ROTATE_EDGES')
```

The Python type is `RotateEdgesModifier`. Official Blender doesn't know this modifier, so a file that uses it won't give the same result there.
