# Pack UVs

<img src="images/icons/mod_uv_pack.png" width="32" height="32" alt="Pack UVs">

Packs the islands of an existing UV map into the 0 to 1 square, like **UV ▸ Pack Islands** in the
UV Editor. As a modifier the packing follows the stack, so it stays right when a modifier above
it changes the UVs.

Islands are the parts of the UV map that are connected. Faces that share an edge in the mesh but
not in the UVs are separate islands.

Add it from **Add Modifier ▸ Edit**. It works on meshes. In the [Modifier List](MODIFIER_LIST.md)'s
Add menu it's in the **UV** column.

## Settings

- **UV Map**: an existing UV map. Empty uses the active map. Without a UV map the modifier shows
  "Missing UV map".
- **Shape**: the shape of the islands used for packing.
  - **Bounding Box**: the boxes around the islands. Fastest.
  - **Convex Hull**: the outlines without the dents. This is the default.
  - **Exact Shape**: the exact outlines, so islands fit into each other's gaps. Slowest.
- **Rotate**: rotate the islands to fit them better.
- **Margin**: space between the islands.

## With other modifiers

- After [Unwrap](UV_UNWRAP.md), [Smart UV Project](SMART_UV_PROJECT.md) or [UVW Map](UVW_MAP.md),
  to pack the islands another way.
- Before [UV Scale](UV_SCALE.md), when you want the islands packed first and scaled after.

## In Python

```python
mod = obj.modifiers.new("Pack UVs", 'UV_PACK')
```

The Python type is `UVPackModifier`. Official Blender doesn't know this modifier, so a file that
uses it won't give the same result there.
