# Push

<img src="images/icons/mod_push.png" width="32" height="32" alt="Push">

Inflates or shrinks the mesh by moving every point along its normal. Unlike scaling, every part
moves by the same distance, whatever its size or its distance from the origin.

Add it from **Add Modifier ▸ Deform**. It works on meshes. In the [Modifier List](MODIFIER_LIST.md)'s Add menu it's in the **Shape** column.

## Settings

- **Distance**: How far to move along the normals. Negative shrinks.
- **Vertex Group**: Vertex group that controls how much each point is affected.

## As a node

Geometry Nodes has a **Push** node that shares its code with this modifier, so a node tree and the modifier give the same result.

## In Python

```python
mod = obj.modifiers.new("Push", 'PUSH')
```

The Python type is `PushModifier`. Official Blender doesn't know this modifier, so a file that uses it won't give the same result there.
