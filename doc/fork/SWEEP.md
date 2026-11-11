# Sweep

<img src="images/icons/mod_sweep.png" width="32" height="32" alt="Sweep">

Makes a mesh by moving a shape along curves: a round or square tube, a flat ribbon, or the curves of
another object as the cross section. The shape can follow the radius of the curve points, roll
around the curve and twist along it, and the result can get UVs.

Add it from **Add Modifier ▸ Generate**. It works on Curves objects (hair curves).

## Settings

- **Shape**: Shape to sweep along the curves.
  - **Round Tube**: Sweep a circle.
  - **Square Tube**: Sweep a square.
  - **Ribbon**: Sweep a straight line.
  - **Cross Section**: Sweep the curves of another object.
- **Cross Section**: Curves object whose curves are swept.
- **Columns**: Number of faces around the shape.
- **Radius**: Radius of the tube.
- **Width**: Width of the square or ribbon.
- **Scale**: Scale of the shape.
- **Use Curve Radius**: Scale the shape by the radius of the curve points.
- **Roll**: Rotation of the shape around the curve.
- **Twist**: Rotation reached at the end of each curve.
- **Fill Caps**: Close the ends of open curves.
- **UV Map**: Name of the UV map to make. Empty for none.
- **Length Based UVs**: Repeat the UV map along the curve so faces keep their proportions.

## As a node

Geometry Nodes has a **Sweep** node too, described in [Modeling nodes in detail](../../README.md#sweep).

## In Python

```python
mod = obj.modifiers.new("Sweep", 'SWEEP')
```

The Python type is `SweepModifier`. Official Blender doesn't know this modifier, so a file that uses it won't give the same result there.
