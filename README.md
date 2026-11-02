<!--
Keep this document short & concise,
linking to external resources instead of including content in-line.
See 'release/text/readme.html' for the end user read-me.
-->

About this fork
===============

An unofficial fork based on Blender 5.1.0 that adds six new **Geometry Nodes** and more ways to show color
attributes in Solid shading. It isn't made or supported by the Blender Foundation, so please don't report
its bugs to the Blender project.

**[Download for Windows](https://github.com/demircanyigit/blender/releases/latest)**: unzip it and open
`blender.exe`. No installer is needed, and your official Blender keeps working next to it.

Files that use these additions won't work properly in official Blender. See
[what to know before using it](doc/fork/README.md).

## New Geometry Nodes

These are real nodes for Blender's Geometry Nodes editor, not add-ons or node groups. Add them with **Add ▸ Mesh ▸ Operations**, or **Add ▸ Curve ▸ Operations** for Sweep.

![The six new nodes side by side in the Geometry Nodes editor](https://github.com/demircanyigit/blender/releases/download/v5.1-custom-1/all_nodes.png)

### Isotropic Remesh

Rebuilds a mesh out of even, near-equilateral triangles that follow the original surface. Hard Edges keeps the
edges you choose, like the eye and mouth rims here.

- **Modes:** Uniform uses one edge length, and zero uses the input's average. Adaptive uses shorter edges where
  the surface curves, between a Min and Max Edge Length and within a Tolerance.
- **What to keep:** Preserve Boundary keeps open edges in place, and Preserve Sharp Edges keeps edges above a
  Sharp Angle. Hard Edges is a field, so it can take the `sharp_edge` attribute or an Edge Angle comparison.
- **Attributes:** point, face and corner attributes such as UVs are sampled from the nearest surface. Edge
  attributes are lost because every edge is new.

![Suzanne as the original, remeshed, and remeshed with Hard Edges](https://github.com/demircanyigit/blender/releases/download/v5.1-custom-1/isotropic_remesh.png)

### Decimate Mesh

Reduces triangles where it changes the surface the least, so flat areas lose detail before curved ones. The
result is triangles.

- **Target:** Ratio keeps a fraction of the selected vertices, and Face Count aims for a number of triangles.
- **Limits:** Max Error, Max Normal Change and Max Aspect Ratio protect quality and can stop it before the
  target. Set any of them to zero to turn it off.
- **What to keep:** Selection picks which vertices may be removed. Preserve Boundary and the Hard Edges field
  protect open edges and the edges you choose.

![Suzanne before and after Decimate Mesh](https://github.com/demircanyigit/blender/releases/download/v5.1-custom-1/decimate_mesh.png)

### Voronoi Fracture

Cuts a closed mesh into solid pieces around the points you give it, with the cut faces filled in. More points
in an area make smaller pieces there.

- **Inputs:** Cell Points takes any geometry with points, Cut Gap leaves space between the pieces, and Separate
  Islands splits parts of a cell that aren't connected.
- **Outputs:** Piece Index numbers the pieces, for example for Split to Instances. Cell Index tells you which
  point made each piece. Interior selects the cut faces, which is how the orange material is set here.
- **Good to know:** the mesh has to be closed. The pieces stay in place until you move them, as in the picture
  on the right.

![A sphere, the same sphere cut into pieces, and the pieces moved apart showing orange inside faces](https://github.com/demircanyigit/blender/releases/download/v5.1-custom-1/voronoi_fracture.png)

### Bevel

The Bevel modifier as a node, using the same code. It gave the same result as the modifier in every setting
compared. Unlike the modifier, Amount and Weight can differ per edge or vertex, for example from Random Value.

- **Inputs:** Affect picks Edges or Vertices. Width Type, Segments and Shape work as in the modifier. Selection
  picks what to bevel, and Angle skips edges flatter than a limit.
- **Geometry and Shading panels:** miters, Spread, Intersections, Clamp Overlap, Loop Slide, Harden Normals, Mark
  Seam, Mark Sharp and Material Index.
- **Output:** Bevel Faces selects the new faces, for example to give them their own material.
- **Good to know:** only edges with exactly two faces get beveled. There's no custom profile or Face Strength
  yet. Segments, Shape and Spread are one value for the whole mesh.

![A cube, a beveled cube and a cube with a random bevel on each edge](https://github.com/demircanyigit/blender/releases/download/v5.1-custom-1/bevel.png)

![Random Value connected to the Amount input of the Bevel node](https://github.com/demircanyigit/blender/releases/download/v5.1-custom-1/node_editor.png)

### Sweep

Sweeps a shape along curves, inspired by Houdini's Sweep. It gives the same surface as Curve to Mesh where the two
overlap, but needs no extra nodes for the shape and also makes UVs.

- **Shapes:** Round Tube, Square Tube, Ribbon, or your own curves in Cross Section. Columns sets the number of
  faces around the built-in shapes.
- **Per curve point:** Scale, Roll and Twist are fields. Twist grows from nothing at the start of the curve to
  the full angle at its end.
- **Outputs:** UV Map follows the shape and the curve, with Length Based UVs keeping faces in proportion. Caps
  selects the end faces made by Fill Caps.
- **Good to know:** caps are flat. Every cross section is swept along every curve. Twist needs enough curve
  points to show, so put Resample Curve in front of a two-point line.

![Three spirals swept with a round tube, a twisted square tube and a star](https://github.com/demircanyigit/blender/releases/download/v5.1-custom-1/sweep.png)

### Resample Mesh

Adds points along edges without changing the shape or the number of faces, inspired by Houdini's Resample.

- **Modes:** Length sets the distance between points, and Count sets the segments on each edge. Both are fields, so
  they can differ per edge. Selection picks which edges change.
- **Output:** New Points selects the added points, for example to place instances on them.
- **Attributes:** point and corner attributes such as colors and UVs are interpolated onto the new points, and
  shared points stay welded.
- **Good to know:** edges stay straight, so curved surfaces don't get smoother. Faces become n-gons with extra
  points, so add Triangulate if you need triangles.

![A cube before and after Resample Mesh, with its points shown in orange](https://github.com/demircanyigit/blender/releases/download/v5.1-custom-1/resample_mesh.png)

## Color attributes in Solid shading

Each material can combine its viewport color with a color attribute, so you can check vertex color variation
without leaving Solid mode. You'll find it under Material ▸ Viewport Display, with the viewport's Color set to
Material.

- **Options:** None, Mix, Multiply, Two Colors and Color Ramp. Mix isn't in the picture.
- **Settings:** Factor sets the strength. Two Colors and Color Ramp can read Brightness, Red, Green, Blue or Alpha.
- **Good to know:** it only changes the viewport, not renders, and only applies to meshes.

![Suzanne with the None, Multiply, Two Colors and Color Ramp options](https://github.com/demircanyigit/blender/releases/download/v5.1-custom-1/solid_colors.png)

## More

- [All node inputs and limits](doc/fork/NODES.md)
- [Material options](doc/fork/VIEWPORT_COLORS.md)
- [Building on Windows](doc/fork/BUILD_WINDOWS.md)

The rest of this page is Blender's own read-me.

Blender
=======

Blender is the free and open source 3D creation suite.
It supports the entirety of the 3D pipeline—modeling, rigging, animation, simulation, rendering, compositing,
motion tracking and video editing.

![Blender screenshot](https://code.blender.org/wp-content/uploads/2018/12/springrg.jpg "Blender screenshot")

Project Pages
-------------

- [Main Website](http://www.blender.org)
- [Reference Manual](https://docs.blender.org/manual/en/latest/index.html)
- [User Community](https://www.blender.org/community/)

Development
-----------

- [Build Instructions](https://developer.blender.org/docs/handbook/building_blender/)
- [Code Review & Bug Tracker](https://projects.blender.org)
- [Developer Forum](https://devtalk.blender.org)
- [Developer Documentation](https://developer.blender.org/docs/)


License
-------

Blender as a whole is licensed under the GNU General Public License, Version 3.
Individual files may have a different but compatible license.

See [blender.org/about/license](https://www.blender.org/about/license) for details.
