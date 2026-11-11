# Normalize Spline

<img src="images/icons/mod_normalize_spline.png" width="32" height="32" alt="Normalize Spline">

Respaces the points of the curves evenly, by a fixed length between them or a fixed number per
curve. Use it before other curve modifiers or [Sweep](SWEEP.md), so they get evenly spaced points.

Add it from **Add Modifier ▸ Generate**. It works on Curves objects (hair curves).

## Settings

- **Mode**: How to space the points.
  - **Length**: Space the points by a fixed length.
  - **Count**: Give each curve a fixed number of points.
- **Length**: Distance between the points.
- **Count**: Number of points on each curve.

## In Python

```python
mod = obj.modifiers.new("Normalize Spline", 'NORMALIZE_SPLINE')
```

The Python type is `NormalizeSplineModifier`. Official Blender doesn't know this modifier, so a file that uses it won't give the same result there.
