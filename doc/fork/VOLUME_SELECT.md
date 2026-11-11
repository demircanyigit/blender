# Volume Select

<img src="images/icons/mod_vol_select.png" width="32" height="32" alt="Volume Select">

Writes a vertex group with the points inside a sphere, box or cylinder, with a soft falloff. The
modifiers after it can use the group, so you can limit a deform, a delete or a displace to an area
and move that area around, or animate it.

Add it from **Add Modifier ▸ Edit**. It works on meshes. In the [Modifier List](MODIFIER_LIST.md)'s Add menu it's in the **Select** column.

## Settings

- **Shape**: Shape of the gizmo: Sphere, Box or Cylinder.
- **Center**: Center of the gizmo, in object space.
- **Rotation**: Rotation of the gizmo.
- **Size**: Size of the gizmo on each axis.
- **Falloff**: Distance outside the shape over which the weight fades out.
- **Invert**: Select the points outside the shape.
- **Vertex Group**: Name of the vertex group to write. Other modifiers can use it.

## In the viewport

When Volume Select is the active modifier, a handle moves its center. See
[Modifier gizmos](MODIFIER_GIZMOS.md).

## As a node

Geometry Nodes has a **Volume Select** node that shares its code with this modifier, so a node tree and the modifier give the same result.

## In Python

```python
mod = obj.modifiers.new("Volume Select", 'VOLUME_SELECT')
```

The Python type is `VolumeSelectModifier`. Official Blender doesn't know this modifier, so a file that uses it won't give the same result there.
