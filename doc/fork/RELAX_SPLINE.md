# Relax Spline

<img src="images/icons/mod_relax_spline.png" width="32" height="32" alt="Relax Spline">

Smooths the curves, taking out kinks and noise, while it can keep the ends in place and the overall
shape.

Add it from **Add Modifier ▸ Deform**. It works on Curves objects (hair curves).

## Settings

- **Factor**: How much to smooth in each step.
- **Iterations**: Number of smoothing steps.
- **Keep Ends**: Keep the ends of the curves in place.
- **Keep Shape**: Try to keep the overall shape.

## As a node

Geometry Nodes has a **Relax Curves** node that shares its code with this modifier, so a node tree and the modifier give the same result.

## In Python

```python
mod = obj.modifiers.new("Relax Spline", 'RELAX_SPLINE')
```

The Python type is `RelaxSplineModifier`. Official Blender doesn't know this modifier, so a file that uses it won't give the same result there.
