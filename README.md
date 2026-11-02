<!--
Keep this document short & concise,
linking to external resources instead of including content in-line.
See 'release/text/readme.html' for the end user read-me.
-->

About this fork
===============

An unofficial fork based on Blender 5.1.0 that adds six geometry nodes and more ways to show color
attributes in Solid shading. It isn't made or supported by the Blender Foundation, so please don't report
its bugs to the Blender project.

**[Download for Windows](https://github.com/demircanyigit/blender/releases/latest)**: unzip it and open
`blender.exe`. No installer is needed, and your official Blender keeps working next to it.

Files that use these additions won't work properly in official Blender. See
[what to know before using it](doc/fork/README.md).

### Isotropic Remesh

Rebuilds a mesh out of even triangles, with one edge length or shorter edges where the surface curves.
Hard Edges keeps the edges you choose, like the eye and mouth rims here.

![Suzanne as the original, remeshed, and remeshed with Hard Edges](https://github.com/demircanyigit/blender/releases/download/v5.1-custom-1/isotropic_remesh.png)

### Decimate Mesh

Reduces triangles where it changes the surface the least, down to a ratio or a face count.

![Suzanne before and after Decimate Mesh](https://github.com/demircanyigit/blender/releases/download/v5.1-custom-1/decimate_mesh.png)

### Voronoi Fracture

Cuts a closed mesh into solid pieces around the points you give it.

![A sphere, the same sphere cut into pieces, and the pieces moved apart showing orange inside faces](https://github.com/demircanyigit/blender/releases/download/v5.1-custom-1/voronoi_fracture.png)

### Bevel

The Bevel modifier as a node. Amount and Weight can differ per edge or vertex, for example from Random
Value.

![A cube, a beveled cube and a cube with a random bevel on each edge](https://github.com/demircanyigit/blender/releases/download/v5.1-custom-1/bevel.png)

![Random Value connected to the Amount input of the Bevel node](https://github.com/demircanyigit/blender/releases/download/v5.1-custom-1/node_editor.png)

### Sweep

Sweeps a tube, ribbon or your own cross section along curves, with Scale, Roll and Twist per curve point. It
also outputs UVs.

![Three spirals swept with a round tube, a twisted square tube and a star](https://github.com/demircanyigit/blender/releases/download/v5.1-custom-1/sweep.png)

### Resample Mesh

Adds points along edges without changing the shape or the number of faces, by length or by count.

![A cube before and after Resample Mesh, with its points shown in orange](https://github.com/demircanyigit/blender/releases/download/v5.1-custom-1/resample_mesh.png)

### Color attributes in Solid shading

Each material can combine its viewport color with a color attribute, so you can check vertex color variation
without leaving Solid mode.

![Suzanne with the None, Multiply, Two Colors and Color Ramp options](https://github.com/demircanyigit/blender/releases/download/v5.1-custom-1/solid_colors.png)

### More

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
