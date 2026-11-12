# Bounds and Align snapping

Two more things to snap to, in the **Snap Target** list of the Snapping popover in the 3D
Viewport header. Turn on as many as you like together with Vertex, Edge and the others.

## Bounds

<img src="images/icons/snap_bounds.png" width="32" height="32" alt="Bounds">

Snaps to the bounding boxes of other objects: the 8 corners, the middles of the 12 edges, the
middles of the 6 sides, and the middle of the box. Good for setting objects on top of each other,
side by side or centered, whatever their shape.

Corners hidden behind the object can be snapped to as well. Objects in Edit Mode are left out,
since their box changes while you edit them.

## Align

<img src="images/icons/snap_align.png" width="32" height="32" alt="Align">

While moving, snaps onto lines along X, Y and Z through other points, so what you move lines up
with them without typing numbers. Dotted lines in the axis colors show what it's lined up with.

- In Object Mode, the points are the origins of the other visible objects.
- In Edit Mode, they're the vertices that aren't selected.

Near two lines on different axes, it snaps to where they cross: for example lined up with one
vertex on X and with another on Y.

Align works when moving, not when rotating or scaling. Other snap targets win when one is close
enough, so with Vertex on too, Align only takes over away from vertices.

## In Python

```python
ts = bpy.context.scene.tool_settings
ts.snap_elements = {'VERTEX', 'BOUNDS', 'ALIGN'}
```
