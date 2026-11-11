# Delete Mesh

<img src="images/icons/mod_delete_mesh.png" width="32" height="32" alt="Delete Mesh">

Deletes the points, edges or faces picked by a vertex group or an attribute. Together with
[Volume Select](VOLUME_SELECT.md), [Poly Select](POLY_SELECT.md) or
[Data Channel](DATA_CHANNEL.md) it can cut away parts of the mesh that change with the stack.

Add it from **Add Modifier ▸ Generate**. It works on meshes. In the [Modifier List](MODIFIER_LIST.md)'s Add menu it's in the **Elements** column.

## Settings

- **Domain**: What to delete.
  - **Points**: Delete the selected points and the edges and faces that use them.
  - **Edges**: Delete the selected edges and the faces that use them.
  - **Faces**: Delete the selected faces, and the edges and points only they use.
  - **Only Faces**: Delete the selected faces and keep all edges and points.
- **Selection**: Vertex group or attribute that selects what to delete.
- **Threshold**: Elements whose value is above this are selected.
- **Invert**: Delete what is not selected.

## In Python

```python
mod = obj.modifiers.new("Delete Mesh", 'DELETE_MESH')
```

The Python type is `DeleteMeshModifier`. Official Blender doesn't know this modifier, so a file that uses it won't give the same result there.
