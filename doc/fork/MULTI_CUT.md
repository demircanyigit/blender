# Multi-Cut

<img src="images/icons/tool_multi_cut.png" width="32" height="32" alt="Multi-Cut">

A tool in the Edit Mode toolbar, right after the Knife and Bisect tools, for cutting faces point by
point.
Click on faces, edges or points to place cut points; each point is joined to the one before it,
straight across the face they share or across the faces in between.

What a click would do is shown under the mouse before you click.

## Cutting

| Do this | To |
|---|---|
| Click on a face, an edge or a point | Place the next cut point |
| Click the first point again | Close the cut. A closed shape drawn inside one face becomes a face of its own |
| Hold the mouse button and drag | Slide the point along its edge |
| Shift while sliding | Snap the point in steps of **Snap Step** |
| Backspace | Remove the last point |
| Enter, Space or right-click | Make the cut |
| Esc | Drop the cut |
| Ctrl+click on an edge | Insert an edge loop through it |
| Drag from a face or empty space, before the first point | Slice the mesh along the dragged line |

A cut can start, bend and end inside faces. An end inside a face stays there as an edge.

## Settings

- **Snap Step**: the step along the edge while Shift is held, as a percentage of the edge's length.
  25% by default. It's in the tool settings in the header.

## In Python

```python
bpy.ops.mesh.multi_cut('INVOKE_DEFAULT')
```

The tool's id is `builtin.multi_cut`.
