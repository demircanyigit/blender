# Normal Smooth

<img src="images/icons/mod_normal_smooth.png" width="32" height="32" alt="Normal Smooth">

Smooths the shading normals over a few steps, without moving the mesh. Cleans up noisy shading on
scans, remeshes and boolean results, while the shape stays the same.

Add it from **Add Modifier ▸ Normals**. It works on meshes. In the
[Modifier List](MODIFIER_LIST.md)'s Add menu it's in the **Normals** column.

## Settings

- **Factor**: how much each step smooths. 1 takes the average of the neighbors fully.
- **Repeat**: the number of steps. More steps smooth over a wider area. The default is 5.
- **Keep Sharp Edges**: leave the normals at sharp edges and flat faces as they are. Points at
  edges with more than two faces are left as they are too. On by default.
- **Vertex Group**: only change the points in this group, by their weight. **Invert** swaps the
  weights.

## Good to know

- Flat faces stay flat. Shade the mesh smooth first to smooth all of it.
- It starts from the normals the mesh already has, so it can come after
  [Normal Transfer](NORMAL_TRANSFER.md) or Weighted Normal to soften their result.

## In Python

```python
mod = obj.modifiers.new("Normal Smooth", 'NORMAL_SMOOTH')
mod.factor = 0.5
mod.iterations = 10
```

The Python type is `NormalSmoothModifier`. Official Blender doesn't know this modifier, so a file
that uses it won't give the same result there.
