# Geometry nodes in this fork

| Node | Add menu | What it does |
|---|---|---|
| [Isotropic Remesh](#isotropic-remesh) | Mesh ▸ Operations | Rebuilds a mesh out of even triangles |
| [Decimate Mesh](#decimate-mesh) | Mesh ▸ Operations | Reduces triangles where it changes the surface least |
| [Voronoi Fracture](#voronoi-fracture) | Mesh ▸ Operations | Cuts a closed mesh into solid pieces |
| [Bevel](#bevel) | Mesh ▸ Operations | Bevels edges or vertices, like the modifier |
| [Resample Mesh](#resample-mesh) | Mesh ▸ Operations | Adds points along edges |
| [Sweep](#sweep) | Curve ▸ Operations | Sweeps a shape along curves |

Inputs marked **field** can take a different value per element, for example from Random Value.
Other inputs take one value, and linking a field to them shows a red link.

## Isotropic Remesh

Rebuilds a mesh out of evenly sized, near-equilateral triangles that follow the original surface.

| Input | Default | Meaning |
|---|---|---|
| Mesh | | Mesh to remesh |
| Mode | Uniform | **Uniform**: one edge length. **Adaptive**: shorter edges where the surface curves more |
| Target Edge Length | 0.1 | Uniform only. Zero uses the input's average edge length |
| Min Edge Length | 0.02 | Adaptive only. Edge length in the most curved areas |
| Max Edge Length | 0.2 | Adaptive only. Edge length in flat areas |
| Tolerance | 0.002 | Adaptive only. Allowed distance between edges and the curved surface |
| Iterations | 10 | Number of split, collapse, flip and relax passes |
| Preserve Boundary | On | Keep open boundary edges in place |
| Preserve Sharp Edges | Off | Keep edges whose faces meet at more than Sharp Angle |
| Sharp Angle | 30° | Angle that counts as sharp |
| Hard Edges | Off | **Field.** Edges to always keep, for example the `sharp_edge` attribute |

Limits:

- The whole mesh is rebuilt. Point, face and corner attributes are sampled from the nearest surface,
  and edge attributes are lost.
- Non-manifold triangles get detached from their neighbors, which leaves seams.

## Decimate Mesh

Reduces the number of triangles, removing detail where it changes the surface the least.

| Input | Default | Meaning |
|---|---|---|
| Mesh | | Mesh to reduce |
| Selection | All | **Field.** Vertices that may be removed |
| Mode | Ratio | **Ratio** or **Face Count** |
| Ratio | 0.5 | Ratio mode. Fraction of the selected vertices to keep |
| Face Count | 1000 | Face Count mode. Desired number of triangles |
| Max Error | 0 | Largest distance to the original surface. Zero disables the limit |
| Max Normal Change | 0 | Largest change of face normals. Zero disables the limit |
| Max Aspect Ratio | 10 | Don't make triangles more elongated than this. Zero disables the limit |
| Preserve Boundary | On | Keep the shape of open boundaries |
| Hard Edges | Off | **Field.** Edges to always keep |

The three limits can stop the node before it reaches the target.

## Voronoi Fracture

Cuts a closed mesh into solid pieces, one for the area around each given point. More points in an
area make smaller pieces there. It needs a build with Manifold, which the default build has.

| Input | Default | Meaning |
|---|---|---|
| Mesh | | Closed mesh to cut |
| Cell Points | | Each point becomes the center of a cell |
| Cut Gap | 0 | Distance between the inside faces of neighboring pieces |
| Separate Islands | On | Make a separate piece of every disconnected part of a cell |

| Output | Meaning |
|---|---|
| Mesh | The pieces |
| Piece Index | **Field.** Number of the piece, for splitting the mesh with Split to Instances |
| Cell Index | **Field.** Index of the point this piece was made from |
| Interior | **Field.** The faces made by the cuts |

## Bevel

Runs the same bevel code as the Bevel modifier.

| Input | Default | Meaning |
|---|---|---|
| Mesh | | Mesh to bevel |
| Affect | Edges | **Edges** or **Vertices** |
| Selection | All | **Field.** Edges or vertices to bevel |
| Width Type | Offset | **Offset**, **Width**, **Depth**, **Percent** or **Absolute**, as in the modifier |
| Amount | 0.1 | **Field.** Size for each edge or vertex. Zero or less doesn't bevel it |
| Segments | 1 | Number of segments |
| Shape | 0.5 | 0.5 is round, less is concave, more is convex |
| Angle | 30° | Edges only. Only bevel where faces meet at more than this. Zero bevels every selected edge |
| Weight | 1 | **Field.** Multiplies Amount |

Geometry panel: Miter Outer, Miter Inner, Spread, Intersections, Clamp Overlap, Loop Slide.

Shading panel: Harden Normals, Mark Seam, Mark Sharp, Material Index (-1 uses the neighboring
faces' material).

| Output | Meaning |
|---|---|
| Mesh | The beveled mesh |
| Bevel Faces | **Field.** The faces made by the bevel |

Limits:

- There's no custom profile or Face Strength.
- Only edges with exactly two faces are beveled, as in the modifier. The Angle limit also applies to
  selected edges.
- Segments, Shape and Spread use one value for the whole mesh, because the bevel code does.
- Large random amounts can look messy with Clamp Overlap off.

## Resample Mesh

Adds points along edges without changing the shape or the number of faces. Attributes are
interpolated onto the new points. Points shared by edges stay welded.

| Input | Default | Meaning |
|---|---|---|
| Mesh | | Mesh to resample |
| Selection | All | **Field.** Edges to resample |
| Mode | Length | **Length** or **Count** |
| Length | 0.1 | **Field.** Length mode. Distance between points |
| Count | 2 | **Field.** Count mode. Number of segments on each edge |

| Output | Meaning |
|---|---|
| Mesh | The resampled mesh |
| New Points | **Field.** The points that were added |

Limits:

- Edges are treated as straight, so curved surfaces don't get smoother.
- Faces become n-gons with extra points on their edges. Add Triangulate if you need triangles.
- One evaluation adds at most 100 million points. Past that the node shows a warning.

## Sweep

Sweeps a shape along curves.

| Input | Default | Meaning |
|---|---|---|
| Curve | | Curves to sweep along |
| Shape | Round Tube | **Round Tube**, **Square Tube**, **Ribbon** or **Cross Section** |
| Cross Section | | Cross Section only. Curves to sweep |
| Columns | 12 | Number of faces around the built-in shapes |
| Radius | 0.1 | Round Tube only |
| Width | 0.2 | Square Tube and Ribbon only |
| Scale | 1 | **Field.** Size of the shape at each curve point |
| Roll | 0° | **Field.** Rotation around the curve at each point |
| Twist | 0° | **Field.** Rotation reached at the end of the curve, growing from none at its start |
| Fill Caps | Off | Close the ends of curves that aren't cyclic, if the shape is closed |
| Length Based UVs | On | Repeat the UVs along the curve so faces keep their proportions |

| Output | Meaning |
|---|---|
| Mesh | The swept mesh |
| UV Map | **Field.** X goes around the shape and Y goes along the curve |
| Caps | **Field.** The faces that close the ends |

Limits:

- Caps are flat. There are no round or grid caps.
- Every cross section is swept along every curve. It doesn't cycle through them.
- Twist needs enough curve points to show, so put Resample Curve in front of a two-point line.
- Radius and Width are single values. Use Scale for sizes that vary.
