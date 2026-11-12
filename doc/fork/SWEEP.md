# Sweep

<img src="images/icons/mod_sweep.png" width="32" height="32" alt="Sweep">

Makes a mesh by moving a shape along curves or along the edges of a mesh: a round or square tube, a
flat ribbon, or the curves or edges of another object as the cross section. The shape can follow
the radius of the curve points, roll around the path and twist along it, and the result can get
UVs. The path can get new, evenly spaced points first, and be smoothed to round its corners.

Add it from **Add Modifier ▸ Generate**. It works on Curves objects (hair curves) and on meshes.

## On a mesh

Draw the path as edges, for example by extruding a single point in Edit Mode, then add Sweep.
Connected edges are followed as one path, and closed loops stay closed. Where more than two edges
meet at a point, the path splits there. Every edge of the mesh is used, so a mesh with faces gets a
tube on each edge, like a wireframe, and the faces themselves are replaced by the result.

The materials of the mesh carry over to the result.

## Settings

- **Shape**: Shape to sweep along the curves.
  - **Round Tube**: Sweep a circle.
  - **Square Tube**: Sweep a square.
  - **Ribbon**: Sweep a straight line.
  - **Cross Section**: Sweep the curves of another object.
- **Cross Section**: Curves object whose curves are swept, or a mesh object whose edges are
  swept, for example a flat outline drawn with edges.
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
- **Resample**: make new, evenly spaced points along the path before sweeping.
  - **None**: use the points as they are.
  - **Count**: spread this many points along each path.
  - **Length**: put points at this distance from each other.
- **Smooth**: smoothing steps of the path. Rounds its corners, and works best after Resample,
  since a path with few points has little to smooth. 0 for none.
  - **Factor**: how much each step moves the points.
  - **Keep Ends**: keep the ends of open paths where they are.

## As a node

Geometry Nodes has a **Sweep** node too, described in [Modeling nodes in detail](../../README.md#sweep).

## In Python

```python
mod = obj.modifiers.new("Sweep", 'SWEEP')
```

The Python type is `SweepModifier`. Official Blender doesn't know this modifier, so a file that uses it won't give the same result there.
