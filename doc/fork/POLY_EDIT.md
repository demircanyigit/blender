# Poly Edit

<img src="images/icons/mod_poly_edit.png" width="32" height="32" alt="Poly Edit">

Moves, extrudes, insets, pokes or deletes the selected elements, inside the stack. It works on the
selection from a [Poly Select](POLY_SELECT.md) below it.

With **Select New Faces**, the faces it makes become the new selection, so another Poly Edit can
carry on from them. A stack of Poly Edits is like a modeling history you can go back and change:
change the first extrude and everything after it follows.

![Voronoi Fracture with Exploded View, Poly Select with four Poly Edits, Panelize and Radial Array](images/modifiers_modeling.png)

*Second from the left: a tower made with Poly Select and four Poly Edits.*

Add it from **Add Modifier ▸ Edit**. It works on meshes. In the [Modifier List](MODIFIER_LIST.md)'s Add menu it's in the **Select** column.

## Settings

- **Operation**: The edit to run on the selection.
  - **Move**: Move and scale the selected points.
  - **Extrude**: Extrude the selected faces.
  - **Inset**: Inset the selected faces.
  - **Poke**: Split the selected faces into fans of triangles.
  - **Delete**: Delete the selected elements.
- **Selection**: Boolean attribute with the selection, written by a Poly Select modifier below. Leave empty to edit everything.
- **Amount**: Extrude height, inset thickness or poke offset.
- **Depth**: Raise or lower the inset faces.
- **Scale**: Scale of the moved points or extruded faces.
- **Offset**: How far to move the selected points.
- **Individual**: Edit each face on its own.
- **Select New Faces**: Make the new faces the selection, for the next Poly Edit.

## In Python

```python
mod = obj.modifiers.new("Poly Edit", 'POLY_EDIT')
```

The Python type is `PolyEditModifier`. Official Blender doesn't know this modifier, so a file that uses it won't give the same result there.
