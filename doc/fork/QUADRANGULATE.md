# Quadrangulate

<img src="images/icons/mod_quadrangulate.png" width="32" height="32" alt="Quadrangulate">

Joins pairs of triangles into quads, the same as **Face ▸ Triangles to Quads** in Edit Mode. Use it
after imports, booleans or a [Decimate](DECIMATE.md) that leave triangles, so subdivision and
modeling work better.

Add it from **Add Modifier ▸ Generate**. It works on meshes. In the [Modifier List](MODIFIER_LIST.md)'s Add menu it's in the **Topology** column.

## Settings

- **Max Face Angle**: Largest angle between two triangles that can be joined.
- **Max Shape Angle**: Largest difference from a square corner of the joined quad.
- **Topology Influence**: How much to prioritize regular grids of quads as well as quads that touch existing quads.
- **Compare UVs**: Don't join triangles across a UV seam.
- **Compare Color Attributes**: Don't join triangles with different colors along their edge.
- **Compare Seam**: Don't join triangles across a seam.
- **Compare Sharp**: Don't join triangles across a sharp edge.
- **Compare Materials**: Don't join triangles with different materials.
- **Vertex Group**: Only join triangles whose vertices are all in this group.

## As a node

Geometry Nodes has a **Quadrangulate** node that shares its code with this modifier, so a node tree and the modifier give the same result.

## In Python

```python
mod = obj.modifiers.new("Quadrangulate", 'QUADRANGULATE')
```

The Python type is `QuadrangulateModifier`. Official Blender doesn't know this modifier, so a file that uses it won't give the same result there.
