# Chamfer Spline

<img src="images/icons/mod_chamfer_spline.png" width="32" height="32" alt="Chamfer Spline">

Cuts the corners of the curves, straight or rounded. Use it to round off sharp corners of paths,
pipes and cables before [Sweep](SWEEP.md).

Add it from **Add Modifier ▸ Generate**. It works on Curves objects (hair curves).

## Settings

- **Radius**: Size of the cut.
- **Segments**: Number of points in each rounded corner. Zero cuts straight.
- **Limit Radius**: Keep the cuts from overlapping.

## In Python

```python
mod = obj.modifiers.new("Chamfer Spline", 'CHAMFER_SPLINE')
```

The Python type is `ChamferSplineModifier`. Official Blender doesn't know this modifier, so a file that uses it won't give the same result there.
