# Beautify Faces

<img src="images/icons/mod_beautify.png" width="32" height="32" alt="Beautify Faces">

Flips the edges between triangles so the triangles are more even, like **Face ▸ Beautify Faces** in
Edit Mode. Long, thin triangles shade badly and deform badly; this makes them rounder without moving
any points.

Add it from **Add Modifier ▸ Generate**. It works on meshes. In the [Modifier List](MODIFIER_LIST.md)'s Add menu it's in the **Topology** column.

## Settings

- **Method**: What makes a triangle even.
  - **Area**: Even out the size of the triangles.
  - **Angle**: Even out the angles of the triangles.
- **Triangulate**: Split the other faces into triangles first, so they can be beautified too.
- **Vertex Group**: Only change faces whose points are all in this group.

## As a node

Geometry Nodes has a **Beautify Faces** node that shares its code with this modifier, so a node tree and the modifier give the same result.

## In Python

```python
mod = obj.modifiers.new("Beautify Faces", 'BEAUTIFY_FACES')
```

The Python type is `BeautifyFacesModifier`. Official Blender doesn't know this modifier, so a file that uses it won't give the same result there.
