# Melt

<img src="images/icons/mod_melt.png" width="32" height="32" alt="Melt">

Sags the shape and spreads it out toward the ground, like ice cream or wax melting. Animate
**Amount** to melt it over time.

Add it from **Add Modifier ▸ Deform**. It works on meshes, curves, surfaces and text. In the [Modifier List](MODIFIER_LIST.md)'s Add menu it's in the **Shape** column.

## Settings

- **Amount**: How far it has melted.
- **Spread**: How far the melted part spreads out.
- **Solidity**: Low melts like ice cream, high holds the top together longer.
- **Axis**: Axis that points up: X, Y or Z.
- **Flip**: Melt toward the other end of the axis.
- **Vertex Group**: Vertex group that controls how much each point is affected.

## As a node

Geometry Nodes has a **Melt** node that shares its code with this modifier, so a node tree and the modifier give the same result.

## In Python

```python
mod = obj.modifiers.new("Melt", 'MELT')
```

The Python type is `MeltModifier`. Official Blender doesn't know this modifier, so a file that uses it won't give the same result there.
