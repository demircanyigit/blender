# Tension

<img src="images/icons/mod_tension.png" width="32" height="32" alt="Tension">

Stores how much the mesh is stretched or squashed compared to its rest shape, in a point attribute.
Put it after an Armature or other deform, and use the attribute in a material for wrinkles in
skin and cloth where it squashes, or for color where it stretches.

Add it from **Add Modifier ▸ Edit**. It works on meshes. In the [Modifier List](MODIFIER_LIST.md)'s Add menu it's in the **Data** column.

## Settings

- **Strength**: Multiply the tension values.
- **Attribute**: Name of the point attribute to write. Positive where stretched, negative where squashed.

## As a node

Geometry Nodes has a **Mesh Tension** node that shares its code with this modifier.

## In Python

```python
mod = obj.modifiers.new("Tension", 'TENSION')
```

The Python type is `TensionModifier`. Official Blender doesn't know this modifier, so a file that uses it won't give the same result there.
