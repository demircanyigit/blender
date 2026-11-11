# Spline Morph

<img src="images/icons/mod_spline_morph.png" width="32" height="32" alt="Spline Morph">

Moves the curves toward the matching curves of another object. Animate **Factor** to morph one set
of curves into another, like hair from one style to another.

Add it from **Add Modifier ▸ Deform**. It works on Curves objects (hair curves).

## Settings

- **Target**: Object whose curves to morph toward.
- **Factor**: How far to move toward the target.

## As a node

Geometry Nodes has a **Morph Curves** node that shares its code with this modifier, so a node tree and the modifier give the same result.

## In Python

```python
mod = obj.modifiers.new("Spline Morph", 'SPLINE_MORPH')
```

The Python type is `SplineMorphModifier`. Official Blender doesn't know this modifier, so a file that uses it won't give the same result there.
