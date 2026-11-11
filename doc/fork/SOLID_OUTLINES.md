# Outlines and edge lines in Solid shading

Solid shading can draw lines around more than whole objects, in any width, and it can draw lines
where the surface jumps in depth or bends sharply. Everything is in the 3D viewport's Shading
popover, under Options, and in the Workbench render settings.

## Outline

**Outline** draws a line in the outline color, like before. The gear button next to the color has
the new settings:

- **Outline By**
  - **Object**: a line around each object, as before.
  - **Loose Part**: a line around each connected part of a mesh, so one mesh made of many parts
    shows each part.
  - **Material**: a line where one material meets another.
  - **Face Set**: a line where one face set meets another. It works well with the
    [Face Set color](FACE_SETS.md) and the [Piece ID](PIECE_ID.md) modifier.
- **Width**: the line thickness in pixels, from 1 to 8. At 1 it looks exactly as before.

Loose Part, Material and Face Set work on meshes, in Object and Edit Mode. Other objects, objects
shown In Front and meshes in Sculpt Mode still get the line around the whole object.

## Edge Lines

**Edge Lines** draws lines found from the depth of the picture, in their own color. The gear
button next to the color has:

- **Depth**: lines where the surface jumps in depth, like a part in front of another part of the
  same object. **Threshold** sets how big the jump must be, relative to the distance from the view.
- **Hard**: lines along hard edges, where the shading splits: edges marked sharp, and the edges of
  flat-shaded faces.
- **Crease**: lines where the surface bends sharply. **Angle** sets how sharp the bend must be.
- **Strength**: how strong the lines are.
- **Width**: the same width as the outline.

Edge lines are like Cavity, but drawn as clean lines instead of shading. Depth and Crease follow
the surface as it is drawn, so they also show on smooth-shaded meshes. Hard follows the shading,
so a smooth-shaded mesh only gets lines where edges are marked sharp.

They go well with [toon shading](TOON_SHADING.md).

## In Python

```python
shading = bpy.context.space_data.shading
shading.show_object_outline = True
shading.outline_type = 'LOOSE_PART'   # 'OBJECT', 'MATERIAL', 'FACE_SET'
shading.outline_width = 2.0

shading.show_edge_lines = True
shading.edge_color = (0.0, 0.0, 0.0)
shading.use_edge_depth = True
shading.edge_depth_threshold = 0.05
shading.use_edge_hard = True
shading.use_edge_crease = True
shading.edge_crease_angle = 0.5236     # 30 degrees
shading.edge_strength = 1.0
```

Official Blender doesn't know these settings and ignores them.
