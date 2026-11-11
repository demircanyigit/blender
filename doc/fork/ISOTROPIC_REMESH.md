# Isotropic Remesh

<img src="images/icons/mod_isotropic_remesh.png" width="32" height="32" alt="Isotropic Remesh">

Rebuilds the mesh out of even, near-equilateral triangles that follow its surface. Use it to clean
up scans, booleans and sculpts before smoothing, simulating or retopologizing, or to get triangles
that are all about the same size.

With **Adaptive**, curved areas get smaller triangles and flat areas bigger ones, so detail stays
where it's needed.

Add it from **Add Modifier ▸ Generate**. It works on meshes. In the [Modifier List](MODIFIER_LIST.md)'s Add menu it's in the **Topology** column.

## Settings

- **Mode**: How to choose the length of the edges.
  - **Uniform**: Give all edges the same length.
  - **Adaptive**: Use shorter edges where the surface is more curved.
- **Edge Length**: Length of the triangle edges in the result. Zero uses the average edge length of the input.
- **Min Edge Length**: Length of the edges in the most curved areas.
- **Max Edge Length**: Length of the edges in flat areas.
- **Tolerance**: Allowed distance between the edges and the curved surface. Smaller values use more triangles on curved areas.
- **Iterations**: Number of split, collapse, flip and relax passes to run.
- **Preserve Boundary**: Keep open boundary edges in place.
- **Preserve Sharp Edges**: Keep edges whose faces meet at more than the sharp angle.
- **Keep Marked Sharp**: Keep the edges marked sharp, whatever their angle.
- **Sharp Angle**: Edges whose faces meet at more than this angle are kept.
- **Smooth Shading**: Output faces with smooth shading rather than flat shaded.

## As a node

Geometry Nodes has a **Isotropic Remesh** node too, described in [Modeling nodes in detail](../../README.md#isotropic-remesh).

## In Python

```python
mod = obj.modifiers.new("Isotropic Remesh", 'ISOTROPIC_REMESH')
```

The Python type is `IsotropicRemeshModifier`. Official Blender doesn't know this modifier, so a file that uses it won't give the same result there.
