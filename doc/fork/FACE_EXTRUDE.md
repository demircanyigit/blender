# Face Extrude

<img src="images/icons/mod_face_extrude.png" width="32" height="32" alt="Face Extrude">

Extrudes faces along their normals and scales them, without leaving the stack. It extrudes all
faces, or the faces picked by a vertex group or an attribute, for example one written by
[Poly Select](POLY_SELECT.md) or [Volume Select](VOLUME_SELECT.md).

Add it from **Add Modifier ▸ Generate**. It works on meshes. In the [Modifier List](MODIFIER_LIST.md)'s Add menu it's in the **Edit** column.

## Settings

- **Amount**: Distance to move the faces along their normals.
- **Scale**: Scale of the extruded faces around their center.
- **Individual**: Extrude each face on its own, rather than connected faces as one region.
- **Selection**: Vertex group or attribute that selects the faces to extrude. All faces when empty.
- **Invert**: Extrude the faces that are not selected.

## As a node

Geometry Nodes has a **Face Extrude** node that shares its code with this modifier, so a node tree and the modifier give the same result.

## In Python

```python
mod = obj.modifiers.new("Face Extrude", 'FACE_EXTRUDE')
```

The Python type is `FaceExtrudeModifier`. Official Blender doesn't know this modifier, so a file that uses it won't give the same result there.
