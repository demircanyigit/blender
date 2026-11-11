# UVW Map

<img src="images/icons/mod_uvw_map.png" width="32" height="32" alt="UVW Map">

Projects a UV map from a planar, cylindrical, spherical or box gizmo, which you can move, turn and
size. Use it for quick mapping of simple shapes, or to keep the mapping of a mesh you're still
changing.

Add it from **Add Modifier ▸ Edit**. It works on meshes. In the [Modifier List](MODIFIER_LIST.md)'s Add menu it's in the **UV** column.

## Settings

- **Mapping**: Shape of the projection.
  - **Planar**: Project along the Z axis of the gizmo.
  - **Cylindrical**: Wrap around the Z axis of the gizmo.
  - **Spherical**: Wrap around a sphere.
  - **Box**: Project each face from the side of the box it faces.
  - **Face**: Give each face the whole texture.
- **Center**: Center of the gizmo, in object space.
- **Rotation**: Rotation of the gizmo.
- **Size**: Size of the gizmo on each axis.
- **Tile**: Number of times the texture repeats on U and V.
- **Offset**: Move the UV map on U and V.
- **Cap**: Map the faces at the ends of a cylinder with a planar projection.
- **Flip U**: Mirror the map on U.
- **Flip V**: Mirror the map on V.
- **UV Map**: Name of the UV map to write. It is added when missing.

## In the viewport

When UVW Map is the active modifier, a handle moves the center of the gizmo. See
[Modifier gizmos](MODIFIER_GIZMOS.md).

## As a node

Geometry Nodes has a **UVW Map** node that shares its code with this modifier, so a node tree and the modifier give the same result.

## In Python

```python
mod = obj.modifiers.new("UVW Map", 'UVW_MAP')
```

The Python type is `UVWMapModifier`. Official Blender doesn't know this modifier, so a file that uses it won't give the same result there.
