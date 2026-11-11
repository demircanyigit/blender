# Mark by Angle

<img src="images/icons/mod_mark_by_angle.png" width="32" height="32" alt="Mark by Angle">

Marks sharp edges, seams, creases or bevel weights where faces meet at a steep angle, on the mesh
as it is at that point in the stack. A Bevel, Subdivision Surface or UV modifier after it can then
use the marks, even though the edges were made by modifiers below.

Add it from **Add Modifier ▸ Normals**. It works on meshes. In the [Modifier List](MODIFIER_LIST.md)'s Add menu it's in the **Normals** column.

## Settings

- **Angle**: Edges where the faces meet at a steeper angle are marked.
- **Edges**: Which steep edges to mark.
  - **Both**: Mark convex and concave edges.
  - **Convex**: Mark only edges that stick out.
  - **Concave**: Mark only edges that sink in.
- **Sharp**: Mark sharp edges.
- **Seam**: Mark UV seams.
- **Crease**: Set the edge crease.
- **Bevel Weight**: Set the edge bevel weight.
- **Value**: Crease and bevel weight of the marked edges.
- **Open Edges**: Also mark edges with only one face.
- **Clear Others**: Clear the marks of the other edges, instead of keeping them.
- **Flat Faces**: Shade the faces around marked edges flat.
- **Vertex Group**: Only change edges whose points are both in this group.

## As a node

Geometry Nodes has a **Mark by Angle** node that shares its code with this modifier, so a node tree and the modifier give the same result.

## In Python

```python
mod = obj.modifiers.new("Mark by Angle", 'MARK_BY_ANGLE')
```

The Python type is `MarkByAngleModifier`. Official Blender doesn't know this modifier, so a file that uses it won't give the same result there.
