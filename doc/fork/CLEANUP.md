# Cleanup

A modifier (`CLEANUP`) under Add Modifier > Generate, and a **Cleanup** node under Mesh ▸
Operations. It runs the steps of the Clean Up menu, and a few more, in one go. Each step has its
own checkbox. They always run in this order:

1. **Delete Loose**: points with no edges (**Vertices**), edges with no faces (**Edges**), and
   faces that touch no other face (**Faces**).
2. **Duplicate Faces**: deletes faces that use the same points as another face.
3. **Interior Faces**: deletes faces inside the mesh, whose edges all have more than two faces.
4. **Degenerate Dissolve**: removes edges shorter than **Distance**, and faces with no area.
5. **Extra Vertices**: removes points in the middle of straight lines, between only two edges,
   where the line bends less than **Angle**.
6. **Limited Dissolve**: merges faces where the surface bends less than **Angle**, with **All
   Boundaries** and **Delimit** (Normal, Material, Seam, Sharp, UV).
7. **Small Pieces**: deletes loose parts with fewer faces than **Min Faces**, or smaller than
   **Min Size** (0 turns size off).
8. **Fill Holes**: fills holes with at most **Max Sides** sides (0 fills every hole).
9. **Make Planar**: flattens faces, by **Factor** for **Iterations** passes.
10. **Split Non-Planar**: splits faces that bend more than **Angle**.
11. **Split Concave**: splits faces that are not convex.
12. **Recalculate Normals**: makes all faces point outward.

Delete Loose (Vertices and Edges), Duplicate Faces and Degenerate Dissolve are on by default.

There is no Merge by Distance step. Put a **Weld** modifier above Cleanup for that.

## Limiting it

The modifier's **Vertex Group** (or the node's **Selection**) limits the cleanup to those points,
and the edges and faces made of them. A loose part is deleted by Small Pieces only if all of its
points are in the group.

## Counts

The modifier panel shows the number of vertices, edges and faces after the cleanup, and how
many were added or removed, for example `Faces: 1280 (-24)`.
