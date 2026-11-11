# Symmetrize

<img src="images/icons/mod_symmetrize.png" width="32" height="32" alt="Symmetrize">

Makes one side of the mesh a mirror of the other, like **Mesh ▸ Symmetrize** in Edit Mode. Unlike
the Mirror modifier, it doesn't add a copy: it replaces the other side, so it also works on meshes
that were never symmetric.

Add it from **Add Modifier ▸ Generate**. It works on meshes. In the [Modifier List](MODIFIER_LIST.md)'s Add menu it's in the **Edit** column.

## Settings

- **Direction**: Which side to copy to the other: -X to +X, -Y to +Y, -Z to +Z, +X to -X, +Y to -Y or +Z to -Z.
- **Merge Distance**: Points this close to the middle are merged.

## As a node

Geometry Nodes has a **Symmetrize** node that shares its code with this modifier, so a node tree and the modifier give the same result.

## In Python

```python
mod = obj.modifiers.new("Symmetrize", 'SYMMETRIZE')
```

The Python type is `SymmetrizeModifier`. Official Blender doesn't know this modifier, so a file that uses it won't give the same result there.
