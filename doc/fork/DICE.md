# Dice

<img src="images/icons/mod_dice.png" width="32" height="32" alt="Dice">

Cuts a mesh into a grid of pieces along the X, Y and Z axes of its bounding box, by the number of
cells or by their size. With **Fill Cuts**, a closed mesh becomes solid blocks, ready for
[Collapse](COLLAPSE.md), rigid bodies or [Exploded View](EXPLODED_VIEW.md).

Add it from **Add Modifier ▸ Generate**. It works on meshes. In the [Modifier List](MODIFIER_LIST.md)'s Add menu it's in the **Edit** column.

## Settings

- **Mode**: How the cells are chosen.
  - **Count**: Choose the number of cells on each axis.
  - **Size**: Choose the size of the cells. The last cell on each axis is smaller if the bounding box doesn't divide evenly.
- **Count**: Number of cells along each axis.
- **Size**: Size of the cells on each axis. Zero doesn't cut along that axis.
- **Cut Gap**: Distance between neighboring pieces.
- **Fill Cuts**: Close the holes left by the cuts, so that closed meshes make solid pieces.
- **World Space**: Cut along the world axes, so the cuts stay straight when the object rotates.
- **Interior Material**: Give the faces that fill the cuts their own material.
- **Interior Material Index**: Material index of the faces that fill the cuts.

## As a node

Geometry Nodes has a **Dice** node too, described in [Modeling nodes in detail](../../README.md#dice).

## In Python

```python
mod = obj.modifiers.new("Dice", 'DICE')
```

The Python type is `DiceModifier`. Official Blender doesn't know this modifier, so a file that uses it won't give the same result there.
