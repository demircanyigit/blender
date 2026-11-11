# Shape Key In-between

<img src="images/icons/mod_shapekey_inbetween.png" width="32" height="32" alt="Shape Key In-between">

Morphs through a sequence of shape keys with one **Value**, one key after another, instead of mixing
them all at once. At 0 the mesh has the first key's shape, then it goes to the second, the third
and so on. Use it for in-between shapes, like an arm that bends through a few corrected shapes, or a
blossom that opens in steps.

Add it from **Add Modifier ▸ Deform**. It works on meshes. In the [Modifier List](MODIFIER_LIST.md)'s Add menu it's in the **Deform** column.

## Settings

- **Value**: Position in the sequence, from the first key to the last.
- **Start Key**: Index of the first shape key of the sequence. 0 is the basis.
- **End Key**: Index of the last shape key of the sequence. 0 uses the last shape key.

## In Python

```python
mod = obj.modifiers.new("Shape Key In-between", 'SHAPE_KEY_INBETWEEN')
```

The Python type is `ShapeKeyInbetweenModifier`. Official Blender doesn't know this modifier, so a file that uses it won't give the same result there.
