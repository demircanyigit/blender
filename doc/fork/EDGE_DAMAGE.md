# Edge Damage

A modifier (`EDGE_DAMAGE`) under Add Modifier > Generate that wears and chips the edges and corners
of closed meshes. It replaces the old Stone Damage modifier.

## How it works

1. **Remesh.** A dense copy of the mesh is made: Isotropic (even triangles), Voxel (a new closed
   surface), Divide (the faces cut by a grid) or None. **Resolution** is the edge length, voxel
   size or grid size.
2. **Smooth.** The copy is smoothed. This pulls it in most at convex corners and edges and barely
   moves flat areas. **Smooth Iterations** and **Smooth Factor** set how far the edges are worn.
3. **Push.** The copy is moved back out along its normals by **Push**, plus **Noise**. Flat areas
   end up outside the original; worn edges stay inside it.
4. **Intersect.** Only what is inside both the original and the copy is kept. The flat faces of
   the original stay exactly as they were, and the edges are bitten away where the copy is inside.

## Settings

- **Solver**: Float, Exact or Manifold, for the intersection. Manifold is fast and needs closed
  meshes; Exact is slow and handles any shape.
- **Per Island**: each connected piece gets its own pattern, and **Variation** changes the
  strength from piece to piece.
- **Noise**: Perlin for soft breaks or Cells for scalloped chips, with Amount, Size, Detail and
  Roughness.
- **Vertex Group**: damage is only allowed in the group.
- **Output**: a material index and smooth shading for the damaged faces, a point attribute that
  is 1 on them, and a red preview color.
