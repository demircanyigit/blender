# Unwrap

<img src="images/icons/mod_uv_unwrap.png" width="32" height="32" alt="Unwrap">

Unwraps the mesh from its seams, like **UV ▸ Unwrap** in Edit Mode, then packs the islands into the
UV square. As a modifier the UVs follow the stack: when a modifier above it changes the mesh, the
UVs are made again.

The seams can come from the mesh itself, or from a modifier above it. For example, put
[Mark by Angle](MARK_BY_ANGLE.md) with **Seams** on above Unwrap, and the mesh is cut wherever its
faces meet at a steep angle.

Add it from **Add Modifier ▸ Edit**. It works on meshes. In the [Modifier List](MODIFIER_LIST.md)'s
Add menu it's in the **UV** column.

## Settings

- **UV Map**: UV map to write. It's added when missing. Empty uses the active map, or makes one
  called UVMap.
- **Method**:
  - **Angle Based**: keeps the angles of the faces. Good for most meshes.
  - **Conformal**: faster, but less even. Works well for simple shapes.
  - **Minimum Stretch**: keeps both the areas and the angles as even as it can.
- **Iterations** (Minimum Stretch): more steps give less stretch, but take longer.
- **No Flip** (Minimum Stretch): don't let faces turn over in the UVs.
- **Fill Holes**: fills holes for the unwrap, so the islands overlap less.
- **Margin**: space between the islands.

The islands are always packed into the 0 to 1 square, with rotation. Put
[Pack UVs](UV_PACK.md) after it to pack them another way, or [UV Scale](UV_SCALE.md) to set a
texel density.

A mesh without seams is one island. A closed mesh without seams can't be flattened well, so mark
some first.

## Good to know

- Unwrapping runs every time the mesh changes, so on dense meshes it can slow down editing. Turn
  the modifier off in Edit Mode if that gets in the way.
- If an island can't be unwrapped, the modifier shows how many failed.
- Pins from the UV Editor aren't used.

## In Python

```python
mod = obj.modifiers.new("Unwrap", 'UV_UNWRAP')
```

The Python type is `UVUnwrapModifier`. Official Blender doesn't know this modifier, so a file
that uses it won't give the same result there.
