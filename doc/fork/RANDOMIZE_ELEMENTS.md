# Randomize Elements

<img src="images/icons/mod_randomize_elements.png" width="32" height="32" alt="Randomize Elements">

Gives each face, island or other element a random rotation, scale and offset, random UVs or a
random color. Use it to break up repeated parts, like tiles, planks, bolts or rocks, so they don't
look copied.

Add it from **Add Modifier ▸ Deform**. It works on meshes. In the [Modifier List](MODIFIER_LIST.md)'s Add menu it's in the **Elements** column.

## Settings

- **Element**: What counts as one element.
  - **Face**: Each face on its own.
  - **Island**: Each connected piece.
  - **UV Island**: Each UV island.
  - **Material**: All faces with the same material.
  - **Face Set**: Each face set from sculpt mode.
- **UV Map**: UV map for UV islands and for changing UVs. The active one when empty.
- **Seed**: Change it for a different random result.
- **Geometry**: Move, rotate and scale the elements.
- **Pivot**: Point each element rotates and scales around.
  - **Center**: Average of the element's points.
  - **Bounds Center**: Center of the element's bounding box.
  - **Origin**: Origin of the object.
- **Rotation**: Largest rotation around X, Y and Z, both ways.
- **Normal Rotation**: Largest rotation around the element's normal, both ways.
- **Scale Min**: Smallest random scale.
- **Scale Max**: Largest random scale.
- **Uniform Scale**: Scale all axes by the same amount.
- **Offset**: Largest move on X, Y and Z, both ways.
- **Normal Offset**: Largest move along the element's normal, both ways.
- **UVs**: Rotate, scale and move the UVs of each element.
- **UV Rotation**: Largest UV rotation, both ways.
- **UV Scale Min**: Smallest random UV scale.
- **UV Scale Max**: Largest random UV scale.
- **UV Offset**: Largest UV move on U and V, both ways.
- **Color**: Give each element a random color.
- **Color Attribute**: Color attribute to write. It is added when missing.
- **Colors**: What kind of colors to pick.
  - **Random Hue**: A random hue.
  - **Grayscale**: A random gray.
- **Saturation**: Saturation of the colors.
- **Value Min**: Darkest color.
- **Value Max**: Brightest color.
- **Blend**: How much the new colors replace the colors there already.
- **Vertex Group**: Vertex group that controls how much each point is affected.

## As a node

Geometry Nodes has a **Randomize Elements** node that shares its code with this modifier, so a node tree and the modifier give the same result.

## In Python

```python
mod = obj.modifiers.new("Randomize Elements", 'RANDOMIZE_ELEMENTS')
```

The Python type is `RandomizeElementsModifier`. Official Blender doesn't know this modifier, so a file that uses it won't give the same result there.
