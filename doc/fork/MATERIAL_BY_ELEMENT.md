# Material By Element

<img src="images/icons/mod_material_by_element.png" width="32" height="32" alt="Material By Element">

Gives the faces material indices at random, by connected piece, in order or by the direction they
point to. Use it to spread a few materials over tiles, bricks or fractured pieces. For rules like
curvature, height or slope, see [Materialize](MATERIALIZE.md).

Add it from **Add Modifier ▸ Edit**. It works on meshes. In the [Modifier List](MODIFIER_LIST.md)'s Add menu it's in the **Data** column.

## Settings

- **Mode**: How to choose the index of each face.
  - **Random**: A random index for each face.
  - **Islands**: A random index for each connected piece.
  - **Cycle**: Go through the indices in order, face by face.
  - **Direction**: One index for each of the six directions a face can point to.
- **Min Index**: Smallest material index to use.
- **Max Index**: Largest material index to use.
- **Seed**: Changes the random indices.

## As a node

Geometry Nodes has a **Material By Element** node that shares its code with this modifier, so a node tree and the modifier give the same result.

## In Python

```python
mod = obj.modifiers.new("Material By Element", 'MATERIAL_BY_ELEMENT')
```

The Python type is `MaterialByElementModifier`. Official Blender doesn't know this modifier, so a file that uses it won't give the same result there.
