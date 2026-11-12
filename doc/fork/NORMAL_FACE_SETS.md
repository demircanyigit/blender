# Normals from Face Sets

<img src="images/icons/mod_normal_face_sets.png" width="32" height="32" alt="Normals from Face Sets">

Makes the shading hard where one [face set](FACE_SETS.md) meets another, and smooth inside each
set. A fast way to get crisp shading on hard-surface models: put each panel or part in its own face
set, and the borders shade sharp.

Add it from **Add Modifier ▸ Normals**. It works on meshes. In the
[Modifier List](MODIFIER_LIST.md)'s Add menu it's in the **Normals** column.

## Settings

- **Weighted**: inside each face set, big faces count more than small ones. Big flat faces stay
  flat, and the small faces around them, like bevels, take the bend. On by default.
- **Keep Sharp Edges**: keep the sharp edges the mesh already has, as well as the borders. Off,
  only the borders are sharp.

All faces are shaded smooth.

## Good to know

- A mesh without face sets shows "No face sets". Make them in Edit Mode with the
  [face set tools](FACE_SETS.md), for example **Face Sets From ▸ Angle**.
- The **Face Set** color in Solid shading shows the sets.

## In Python

```python
mod = obj.modifiers.new("Face Set Normals", 'NORMAL_FACE_SETS')
mod.use_weighted = True
```

The Python type is `NormalFaceSetsModifier`. Official Blender doesn't know this modifier, so a file
that uses it won't give the same result there.
