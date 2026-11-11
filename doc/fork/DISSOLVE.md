# Dissolve

<img src="images/icons/mod_dissolve.png" width="32" height="32" alt="Dissolve">

Removes points, edges or faces and merges the faces around them, without leaving holes, like the
Dissolve tools in Edit Mode. **Limited** removes the edges and points where the surface stays nearly
flat, which is a quick way to clean up triangulated or over-detailed flat areas.

Add it from **Add Modifier ▸ Generate**. It works on meshes. In the [Modifier List](MODIFIER_LIST.md)'s Add menu it's in the **Topology** column.

## Settings

- **Mode**: What to remove.
  - **Vertices**: Remove the points, and merge the faces around them.
  - **Edges**: Remove the edges, and merge the faces on both sides.
  - **Faces**: Merge faces that touch each other into one.
  - **Limited**: Remove the edges and points where the surface stays nearly flat.
- **Angle**: Only dissolve where the surface bends less than this.
- **Remove Inline Points**: Also remove points left between only two edges.
- **Face Split**: Split off the corners of faces to keep the surrounding shape.
- **Tear Boundary**: Split off corners on open boundaries instead of merging the faces.
- **All Boundaries**: Also dissolve points on open boundaries, whatever their angle.
- **Materials**: Don't merge faces with different materials.
- **Seams**: Don't dissolve seams.
- **Sharp Edges**: Don't dissolve sharp edges.
- **UVs**: Don't dissolve edges that split UV islands.
- **Vertex Group**: Only dissolve the points in this group, and the edges and faces made of them.

## As a node

Geometry Nodes has a **Dissolve** node too, described in [Modeling nodes in detail](../../README.md#dissolve).

## In Python

```python
mod = obj.modifiers.new("Dissolve", 'DISSOLVE')
```

The Python type is `DissolveModifier`. Official Blender doesn't know this modifier, so a file that uses it won't give the same result there.
