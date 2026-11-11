# Poke

<img src="images/icons/mod_poke.png" width="32" height="32" alt="Poke">

Splits each face into a fan of triangles around a new point in its middle, like **Face ▸ Poke Faces**
in Edit Mode. With an **Offset**, the new points move out or in, for spikes or dents.

Add it from **Add Modifier ▸ Generate**. It works on meshes. In the [Modifier List](MODIFIER_LIST.md)'s Add menu it's in the **Edit** column.

## Settings

- **Center**: Where the new point goes.
  - **Weighted Mean**: Average of the points, weighted by edge length.
  - **Mean**: Average of the points.
  - **Bounds**: Center of the bounding box.
- **Offset**: Move the new point along the normal.
- **Relative**: Scale the offset by the size of each face.
- **Vertex Group**: Only poke faces whose points are all in this group.

## As a node

Geometry Nodes has a **Poke** node that shares its code with this modifier, so a node tree and the modifier give the same result.

## In Python

```python
mod = obj.modifiers.new("Poke", 'POKE')
```

The Python type is `PokeModifier`. Official Blender doesn't know this modifier, so a file that uses it won't give the same result there.
