# Poly Select

<img src="images/icons/mod_poly_select.png" width="32" height="32" alt="Poly Select">

Stores a selection of points, edges or faces in the stack and writes it to an attribute, so the
modifiers after it can work on only those elements. It's the start of a
[Poly Edit](POLY_EDIT.md) chain.

![Voronoi Fracture with Exploded View, Poly Select with four Poly Edits, Panelize and Radial Array](images/modifiers_modeling.png)

*Second from the left: a tower made with Poly Select and four Poly Edits.*

Add it from **Add Modifier ▸ Edit**. It works on meshes. In the [Modifier List](MODIFIER_LIST.md)'s Add menu it's in the **Select** column.

## How to use it

1. Select points, edges or faces in Edit Mode.
2. Click **Store Selection** in the modifier. The selection is kept, even if you select something
   else later.
3. Add [Poly Edit](POLY_EDIT.md) or another modifier after it, and set its selection to the
   **Output** attribute.

## Settings

- **Domain**: What kind of element the selection is of: Points, Edges or Faces.
- **Output**: Name of the boolean attribute to write the selection to.
- **Invert**: Output the elements that are not stored.

## In Python

```python
mod = obj.modifiers.new("Poly Select", 'POLY_SELECT')
```

The Python type is `PolySelectModifier`. Official Blender doesn't know this modifier, so a file that uses it won't give the same result there.
