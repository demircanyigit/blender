# Cap Holes

<img src="images/icons/mod_cap_holes.png" width="32" height="32" alt="Cap Holes">

Closes the holes of the mesh with new faces, like **Face ▸ Fill** on every hole at once. Use it to
close a mesh before a boolean, a solidify or a fracture.

Add it from **Add Modifier ▸ Generate**. It works on meshes. In the [Modifier List](MODIFIER_LIST.md)'s Add menu it's in the **Edit** column.

## Settings

- **Max Sides**: Only fill holes with at most this many sides. Zero fills all holes.
- **Triangulate**: Split the new faces into triangles.

## As a node

Geometry Nodes has a **Cap Holes** node that shares its code with this modifier, so a node tree and the modifier give the same result.

## In Python

```python
mod = obj.modifiers.new("Cap Holes", 'CAP_HOLES')
```

The Python type is `CapHolesModifier`. Official Blender doesn't know this modifier, so a file that uses it won't give the same result there.
