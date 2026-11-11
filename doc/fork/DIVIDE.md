# Divide

Native modifier (`DIVIDE`, Python type `DivideModifier`) available under Add Modifier > Generate.
In Modifier List mode it appears in the Topology column. Its icon shows a connected polygon patch
with highlighted grid lines.

This implements the requested grid-based polygon brickering idea. Houdini's Divide SOP documents
Bricker Polygons and its Size, Offset, and Angle controls in the
[SideFX documentation](https://www.sidefx.com/docs/houdini/nodes/sop/divide.html).
This modifier does not implement the SOP's other smoothing, convexing, dual, or edge-removal modes.

## Controls

- **Grid Size (XYZ):** spacing of the parallel cutting planes along each rotated grid axis.
  Default is 0.5 on each axis. Zero disables cutting along that axis.
- **Offset (XYZ):** grid origin in object space. The grid also rotates around this point.
- **Rotation (XYZ):** Euler rotation of the grid axes, displayed in angle units.
- **Grid Axes (X/Y/Z):** enable individual families of cutting planes while retaining their sizes.
- **Advanced > Tolerance:** classifies vertices close to a cutting plane. Existing vertices are
  not snapped onto the grid, and the modifier does not weld separate nearby vertices. An internal
  floor of 1e-7 object-space units applies to the classification tolerance.

The grid is anchored to the object origin plus Offset, not to the mesh's changing bounding box.
Increasing mesh bounds extends the same grid. Size and Offset are object-space lengths; apply
object scale if you want equal physical spacing across differently scaled objects.

## Geometry behavior

The operation splits edges and faces in a single shared BMesh. Both sides of each cut remain;
adjacent polygons share their new boundary vertices and edges. Original seams and separate mesh
islands remain separate. No volumes are extracted, caps created, gaps opened, or fragments detached.

UV/corner data and interpolatable vertex data follow BMesh edge and face splitting. New sections
inherit face attributes such as material index. Existing polygon boundaries remain, so the output
can contain triangles or n-gons around old boundaries; it is not a general all-quad remesher.
Open polygon surfaces are supported. Wire edges accompanying a surface may also be split by the
grid; a wire-only input is bypassed. Strongly non-planar n-gons have ambiguous interior surfaces;
triangulate them upstream when a fixed source tessellation matters.

## Evaluation limits

Grid planes are generated only inside the projected input bounds. Up to 2048 planes are allowed.
The modifier also limits evaluation to two million vertices, two million faces and eight million
corners. Limits are checked before conversion and after each cut; an oversized individual cut can
allocate temporary geometry before the post-cut check. Settings too fine for the tolerance or
floating-point coordinate precision are rejected. Failure retains the original input with a warning.

## Status

Source and icon integration only. No build, execution, or tests were performed for this addition,
following the requested workflow. Existing executables do not include Divide until rebuilt.
The icon can be regenerated with `python build_files/utils/make_divide_icon.py` (requires Pillow).
