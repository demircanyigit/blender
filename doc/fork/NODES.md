# Geometry nodes in this fork

| Node | Add menu | What it does |
|---|---|---|
| [Isotropic Remesh](#isotropic-remesh) | Mesh ▸ Operations | Rebuilds a mesh out of even triangles |
| [Decimate Mesh](#decimate-mesh) | Mesh ▸ Operations | Reduces triangles where it changes the surface least |
| [Voronoi Fracture](#voronoi-fracture) | Mesh ▸ Operations | Cuts a closed mesh into solid pieces |
| [Bevel](#bevel) | Mesh ▸ Operations | Bevels edges or vertices, like the modifier |
| [Resample Mesh](#resample-mesh) | Mesh ▸ Operations | Adds points along edges |
| [Clip](#clip) | Mesh ▸ Operations | Cuts a mesh with a plane and removes one side |
| [Dice](#dice) | Mesh ▸ Operations | Cuts a mesh into a grid of pieces along its bounding box |
| [Dissolve](#dissolve) | Mesh ▸ Operations | Removes points, edges or faces without leaving holes |
| [Divide Mesh](#divide-mesh) | Mesh ▸ Operations | Cuts the faces with a grid, keeping the mesh connected |
| [Smooth](#smooth) | Mesh ▸ Operations | Smooths the surface |
| [Relax](#relax) | Mesh ▸ Operations | Evens out the points along the surface |
| [QuadriFlow Remesh](#quadriflow-remesh) | Mesh ▸ Operations | Rebuilds a mesh out of quads |
| [Displace](#displace) | Mesh ▸ Operations | Moves points by a height or vector |
| [Sweep](#sweep) | Curve ▸ Operations | Sweeps a shape along curves |
| [Collapse Islands](#collapse-islands) | Mesh ▸ Operations | Drops mesh islands as rigid pieces |
| [Drape](#drape) | Mesh ▸ Operations | Settles a cloth mesh over a collider |
| [Shatter](#shatter) | Mesh ▸ Operations | Breaks a mesh with material-specific patterns |
| [Masonry](#masonry) | Mesh ▸ Operations | Covers faces with brick, stone or paving patterns |
| [Planks](PLANKS.md) | Mesh ▸ Operations | Floors and walls of boards, crates and barrels |
| [Roof](ROOF.md) | Mesh ▸ Operations | Builds roofs on outlines, or covers modeled roofs |
| [Carve](CARVE.md) | Mesh ▸ Operations | Displaces with two pattern or value layers, masked |
| [Materialize](MATERIALIZE.md) | Mesh ▸ Operations | Gives a material to faces picked by a rule |
| [Cleanup](CLEANUP.md) | Mesh ▸ Operations | Runs the Clean Up menu's steps and more in one go |
| [Stonework](STONEWORK.md) | Mesh ▸ Operations | Fills a mesh with whole stones, stacked from the bottom up |
| [Distribute](DISTRIBUTE.md) | Mesh ▸ Operations | Packs instances on the faces by their measured sides, so none of them touch |
| [UV Scale](UV_SCALE.md) | Mesh ▸ Operations | Scales UVs or matches a target texel density |

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

## Clip

Cuts a mesh with a plane and removes one side. Attributes are interpolated onto the cut. Fill faces
copy the attributes of the faces next to them and are shaded flat.

| Input | Default | Meaning |
|---|---|---|
| Mesh | | Mesh to cut |
| Keep | Above | **Above**: the side Direction points to. **Below**: the other side. **Both**: keep everything and only add the cut |
| Space | Object | **Object**: the plane moves with the object. **World**: the plane stays in place |
| Origin | 0, 0, 0 | A point on the plane |
| Direction | 0, 0, 1 | The direction the plane faces |
| Distance | 0 | Moves the plane from Origin along Direction |
| Fill Cut | On | Closes the holes left by the cut. Not used with Both |

| Output | Meaning |
|---|---|
| Mesh | The cut mesh |
| Cut Edges | **Field.** The edges along the cut, for example to bevel them |
| Interior | **Field.** The faces that fill the cut |

Limits:

- Only closed loops can be filled, so cutting an open mesh may leave parts of the cut open.
- Self-intersecting meshes can get overlapping fill faces.
- World space uses the object's transform, which doesn't include the transforms of instances. Add
  Realize Instances in front if the mesh is instanced.

## Dice

Cuts a mesh into a grid of pieces along the X, Y and Z axes of its bounding box. It also works on
open meshes, which become open pieces.

| Input | Default | Meaning |
|---|---|---|
| Mesh | | Mesh to cut |
| Mode | Count | **Count**: number of cells on each axis. **Size**: size of the cells |
| Space | Object | **Object**: bounding box and axes of the object, so the cuts rotate with it. **World**: bounding box and axes of the world |
| Count X, Y, Z | 2 | Count only. Number of cells on each axis |
| Size | 0.5 | Size only. Cell size on each axis, starting from the low corner of the box. The last cell is smaller if the box doesn't divide evenly. Zero doesn't cut along that axis |
| Cut Gap | 0 | Distance between neighboring pieces |
| Fill Cuts | On | Closes the holes left by the cuts, so closed meshes make solid pieces |
| Separate Islands | Off | Makes a separate piece of every disconnected part of a cell |

| Output | Meaning |
|---|---|
| Mesh | The pieces |
| Piece Index | **Field.** Number of the piece, for splitting the mesh with Split to Instances |
| Cell Index | **Field.** Position in the grid: X + Y × Count X + Z × Count X × Count Y |
| Interior | **Field.** The faces that fill the cuts |

Limits:

- Cells without any of the mesh are skipped, so Piece Index has no gaps but Cell Index can.
- An axis along which the mesh has no size isn't cut.
- Each mesh uses its own bounding box. Add Realize Instances in front to dice instances together.
- A single evaluation is limited to one million cells. Past that the node shows a warning and leaves
  the mesh as it is.
- Pieces at the edge of a curved shape can be very small.

## Divide Mesh

Cuts the faces of a mesh with a grid of planes, like the Divide modifier. Nothing is removed,
filled or split apart: the mesh stays one connected surface with more faces.

| Input | Default | Meaning |
|---|---|---|
| Mesh | | Mesh to cut |
| Size | 0.5 | Distance between the cuts along each axis |
| Offset | 0 | Moves the grid of cuts |
| Rotation | 0 | Turns the grid of cuts around the offset |
| X, Y, Z | On | Which axes are cut across |

## Dissolve

Removes parts of a mesh and merges the faces around them, without leaving holes. Uses the same code
as Dissolve in Edit Mode.

| Input | Default | Meaning |
|---|---|---|
| Mesh | | Mesh to remove parts from |
| Mode | Edges | **Vertices**, **Edges**, **Faces**, or **Limited** for edges and points where the surface stays nearly flat |
| Selection | All | **Field.** Points in Vertices mode, faces in Faces mode, otherwise edges |
| Remove Inline Points | On | Edges and Faces. Also removes points left between only two edges |
| Face Split | Off | Vertices and Edges. Splits off face corners to keep the surrounding shape |
| Tear Boundary | Off | Vertices only. Splits off corners on open boundaries instead of merging faces |
| Angle | 5° | Limited only. Only dissolves where the surface bends less than this |
| All Boundaries | Off | Limited only. Also dissolves points on open boundaries, whatever their angle |

Keep panel, Limited only: Materials, Seams, Sharp Edges and UVs each stop faces from merging across
that kind of border.

| Output | Meaning |
|---|---|
| Mesh | The mesh with parts removed |
| Merged Faces | **Field.** The faces made by merging. Always off in Vertices mode |

Limits:

- In Limited mode a point is only dissolved if all of its edges are selected.
- Merged faces can be n-gons that aren't flat, which can shade unevenly. Add Triangulate if needed.
- In Edges mode, Merged Faces can include a face where merging wasn't possible.

## Smooth

Smooths a surface by moving points toward their neighbors. The number of points and all attributes
stay the same.

| Input | Default | Meaning |
|---|---|---|
| Mesh | | Mesh to smooth |
| Method | Average | **Average**: strong, but the mesh shrinks with more iterations. **Keep Volume**: mostly keeps the size, but only removes small bumps |
| Selection | All | **Field.** Points that may move |
| Factor | 0.5 | **Field.** Strength for each point |
| Iterations | 5 | Number of passes |
| Preserve Boundary | On | Keeps points on open boundaries in place |
| Hard Edges | Off | **Field.** Edges whose points stay in place |

## Relax

Evens out the distances between points by sliding them along the surface, so the shape stays the same.
The number of points and all attributes stay the same.

| Input | Default | Meaning |
|---|---|---|
| Mesh | | Mesh to relax |
| Selection | All | **Field.** Points that may move |
| Factor | 0.5 | **Field.** How far each point moves in one iteration |
| Iterations | 10 | Number of passes |
| Keep on Surface | On | Moves points back onto the original surface after every iteration |
| Preserve Boundary | On | Keeps points on open boundaries in place |
| Hard Edges | Off | **Field.** Edges whose points stay in place |

Limits:

- Attribute values stay on their points, so textures can shift when UVs are kept.
- It doesn't change the connections between points, so a mesh with very different numbers of edges
  per point, like the poles of a UV sphere, won't get even. Isotropic Remesh does that.

## QuadriFlow Remesh

Rebuilds a mesh out of quads that follow the flow of its surface, using the same code as the
QuadriFlow option of the Remesh operator.

| Input | Default | Meaning |
|---|---|---|
| Mesh | | Mesh to rebuild |
| Target | Face Count | **Face Count**, **Ratio** of the input's faces, or **Edge Length** |
| Face Count | 1000 | Face Count only. Desired number of quads |
| Ratio | 1 | Ratio only. Desired number of quads as a fraction of the input's faces |
| Edge Length | 0.1 | Edge Length only. Desired length of the edges. Zero leaves the mesh as it is |
| Seed | 0 | Changes the layout of the quads |
| Preserve Sharp | Off | Tries to follow the sharp features of the surface |
| Preserve Boundary | Off | Tries to follow the open boundaries of the mesh |

Limits:

- The mesh must be manifold, with faces pointing the same way and no loose or zero-length edges.
  Otherwise the node shows an error and leaves the mesh as it is. Fix it first, for example with
  Isotropic Remesh, or by turning it into a volume and back.
- The number of quads is only close to the target, and the node asks for at least 6.
- Details smaller than the quads get crumpled or lost, so use a high enough target for small features
  like eyes. A very coarse input also gives rougher results, so subdivide it first.
- It's slow, and it runs again every time something before it changes. Bake it or use a low target
  while working.
- Attributes are copied from the nearest part of the input, not blended, so they look blocky if the
  input is much coarser than the result.
- There's no way to cancel it while it runs.

## Displace

Moves the points of a mesh by a height or a vector. With no texture it gives the same result as the
Displace modifier. To use a texture, connect it to Height or Vector.

| Input | Default | Meaning |
|---|---|---|
| Mesh | | Mesh to displace |
| Mode | Normal | **Normal**: along each point's normal. **Direction**: along a direction. **Vector**: by a vector |
| Selection | All | **Field.** Points to move |
| Height | 1 | **Field.** Normal and Direction only. Height of each point |
| Direction | 0, 0, 1 | **Field.** Direction only. Its length doesn't matter |
| Vector | 1, 1, 1 | **Field.** Vector only. Movement of each point, before Midlevel and Strength |
| Midlevel | 0.5 | The value that doesn't move a point. In Vector mode it's subtracted from every axis |
| Strength | 1 | **Field.** Multiplier of the movement |

Limits:

- The mesh needs enough points to show detail. Subdivide it or use Isotropic Remesh first.
- Normal mode always uses smooth point normals, so flat shaded faces don't come apart.
- Direction and Vector are relative to the object, not the world.

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


## Collapse Islands

Drops each connected mesh island as a rigid piece. Ground takes collider geometry, instances
included, so a Collection Info node can give a whole collection; with no ground, the node uses a
floor under the mesh. Cage pushes the ground out along its normals, so the pieces land further
away; negative pulls it in. Keep is a point field that holds islands in place. Space, Time,
Gravity, Push, Seed, Friction, Bounce and Quality control the settling. The output is Mesh.
This computes a settled result during evaluation rather than creating a rigid-body simulation cache.

The Collapse modifier has a Ground object, and a list below it for more objects and collections,
with Add Object, Add Collection and Add Selected Objects buttons. It also has Cage. Its **Pieces**
option drops loose parts (**Islands**) or faces and points with the same number in an attribute
(**Attribute**, `piece` by default). A face attribute splits the mesh where different pieces meet.
See [Piece ID](PIECE_ID.md).

## Drape

Settles a mesh as cloth over Collider geometry, instances included, so a Collection Info node can
give a whole collection. Cage pushes the collider out along its normals, so the cloth rests further
away; negative pulls it in. Pin is a point field for fixed points. Floor adds a floor plane. Space,
Time, Gravity, Stiffness, Bending, Shrink, Pressure, Thickness, Friction, Self Collision and
Quality control the solver. The output is Mesh. Subdivide the input first to provide enough points
to bend; higher quality and self collision increase evaluation cost.

The Drape modifier has a Collider object, and a list below it for more objects and collections,
with Add Object, Add Collection and Add Selected Objects buttons. It also has Cage.

## Shatter

Cuts a closed mesh using Stone, Glass, Wood, Ceramic or Ice patterns. Pieces and Seed control the
breakup; Impact and Focus locate the hit; Grain Axis and Grain control directional patterns.
Roughness, Detail and Cut Gap shape the broken surfaces. Outputs are Mesh, Piece Index and
Interior, so downstream nodes can identify pieces and broken faces. Start with a closed mesh.

## Masonry

Covers selected faces with brick, stone or paving blocks. Pattern selects the layout; Direction,
Angle and Center orient it. Length, Height, Depth, Mortar and Offset size the blocks and joints.
Soldier Rows, Backing, Border and Toothed control the edges and backing. Variation inputs and Seed
change block sizes and shapes. Tile and Tile Size support a custom tile in the node version.
Outputs are Mesh, Block Index and Mortar. Small blocks on large faces can create substantial geometry.
