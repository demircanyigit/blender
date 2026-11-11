# Edit Poly

<img src="images/icons/mod_edit_poly.png" width="32" height="32" alt="Edit Poly">

Lets you edit the mesh in Edit Mode at its place in the stack. The modifiers below it are already
applied when you edit, and the modifiers above it keep working on your edits. It's like applying
the modifiers below, editing, and adding the ones above again, but nothing is lost: you can turn
it off, reset it or delete it.

Add it from **Add Modifier ▸ Edit**. It works on meshes. In the [Modifier List](MODIFIER_LIST.md)'s Add menu it's in the **Select** column.

## How to use it

1. Add Edit Poly where you want to edit in the stack.
2. Click **Start Editing**. The object goes into Edit Mode with the mesh as it is at that point;
   the modifiers before Edit Poly are turned off until you finish.
3. Edit as usual, with any Edit Mode tool.
4. Click **Finish Editing** to go back. The edits are stored in the modifier.

Later, **Edit** opens the stored mesh again, and **Reset** throws the edits away, so the mesh
comes through unchanged.

## Settings

- **Follow Changes Below**: Move the edited points with the mesh below when it only moves, as long as its point count stays the same.

## Good to know

- With **Follow Changes Below**, the edited points move along when the mesh below only moves, as
  long as its number of points stays the same. If something below adds or removes points, the stored
  mesh is used as it is.
- The stored mesh is saved in the file, so it grows with the size of the mesh.

## In Python

```python
mod = obj.modifiers.new("Edit Poly", 'EDIT_POLY')
```

The Python type is `EditPolyModifier`. Official Blender doesn't know this modifier, so a file that uses it won't give the same result there.
