# Slice

<img src="images/icons/mod_slice.png" width="32" height="32" alt="Slice">

Cuts the mesh with a plane and removes one side, keeps both sides with the cut added, and can fill
the hole the cut leaves. Use it for cutaways, to cut an object to the floor, or to animate a section
moving through it.

Add it from **Add Modifier ▸ Generate**. It works on meshes. In the [Modifier List](MODIFIER_LIST.md)'s Add menu it's in the **Edit** column.

## Settings

- **Keep**: Which part of the mesh to keep.
  - **Above**: Keep the part on the side the plane faces.
  - **Below**: Keep the other part.
  - **Both**: Keep everything, and only add the cut to the surface.
- **Plane Location**: A point on the plane, in object space.
- **Plane Rotation**: Rotation of the plane. Its normal is the rotated Z axis.
- **Distance**: Move the plane along its normal.
- **Fill Cut**: Close the holes left by the cut.

## In the viewport

When Slice is the active modifier, a handle moves the plane and a marker sets its **Distance**.
See [Modifier gizmos](MODIFIER_GIZMOS.md).

## In Python

```python
mod = obj.modifiers.new("Slice", 'SLICE')
```

The Python type is `SliceModifier`. Official Blender doesn't know this modifier, so a file that uses it won't give the same result there.
