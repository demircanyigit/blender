# Inset

<img src="images/icons/mod_inset.png" width="32" height="32" alt="Inset">

Insets faces like the Inset Faces tool in Edit Mode: each face on its own, or connected faces as one
region. The new faces can be marked with attributes, so the modifiers after it can work on only the
inset faces or only the rims.

Add it from **Add Modifier ▸ Generate**. It works on meshes. In the [Modifier List](MODIFIER_LIST.md)'s Add menu it's in the **Edit** column.

## Settings

- **Individual**: Inset each face on its own.
- **Thickness**: How far the edges move in.
- **Depth**: How far the inner faces move out.
- **Material Offset**: Added to the material index of the new faces.
- **Even**: Keep the thickness even at sharp corners.
- **Relative**: Scale the thickness by the size of the faces.
- **Boundary**: Inset open edges too.
- **Edge Rail**: Slide along the edges.
- **Outset**: Grow outward instead of inward.
- **Top**: Face attribute to mark the inset faces. Empty to skip.
- **Side**: Face attribute to mark the new rim faces. Empty to skip.
- **Vertex Group**: Only inset faces whose points are all in this group.

## As a node

Geometry Nodes has a **Inset** node that shares its code with this modifier, so a node tree and the modifier give the same result.

## In Python

```python
mod = obj.modifiers.new("Inset", 'INSET')
```

The Python type is `InsetModifier`. Official Blender doesn't know this modifier, so a file that uses it won't give the same result there.
