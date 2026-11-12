<!--
Keep this document short & concise,
linking to external resources instead of including content in-line.
See 'release/text/readme.html' for the end user read-me.
-->

<p align="center">
  <img src="doc/fork/images/brand/weld3d_banner.png" alt="Weld 3D, based on Blender 5.1" width="100%">
</p>

<p align="center">
  <b>A modeling-first 3D suite with 74 new modifiers, 77 new nodes and the classic look.</b><br>
  Based on Blender 5.1.
</p>

<p align="center">
  <a href="https://github.com/demircanyigit/Weld-3D/releases/latest"><img src="https://img.shields.io/badge/Download-Windows%20x64-ffba48?style=for-the-badge&logo=windows&logoColor=white" alt="Download for Windows"></a>
  <a href="doc/fork/README.md"><img src="https://img.shields.io/badge/Docs-Read%20first-3a4250?style=for-the-badge" alt="Docs"></a>
  <a href="doc/fork/BUILD_WINDOWS.md"><img src="https://img.shields.io/badge/Build-From%20source-3a4250?style=for-the-badge" alt="Build from source"></a>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/based%20on-Blender%205.1-ff6814?style=flat-square" alt="Based on Blender 5.1">
  <img src="https://img.shields.io/badge/modifiers-%2B74-6f8cd6?style=flat-square" alt="74 new modifiers">
  <img src="https://img.shields.io/badge/geometry%20nodes-%2B77-4fa35b?style=flat-square" alt="77 new nodes">
  <img src="https://img.shields.io/badge/icons-1000%2B%20classic-a0a8b4?style=flat-square" alt="Over 1,000 classic icons">
  <img src="https://img.shields.io/badge/license-GPL--3.0-a0a8b4?style=flat-square" alt="GPL-3.0">
</p>

Weld 3D is an unofficial build of Blender 5.1.0 for modelers. It adds procedural modeling modifiers, nodes for
them, new editors for your own buttons and the colored 2.79 look. Unzip it and open `blender.exe`: no installer,
and your official Blender keeps working next to it.

> [!NOTE]
> Weld 3D isn't made or supported by the Blender Foundation, so please don't report its bugs to the Blender
> project. Files that use its additions won't work properly in official Blender. See
> [what to know before using it](doc/fork/README.md).

## At a glance

<table>
<tr>
<td width="33%" valign="top">

### <img src="doc/fork/images/icons/mod_trim.png" width="32" height="32" alt=""> Build

[Trim](doc/fork/TRIM.md), [Sweep](doc/fork/SWEEP.md), [Planks](doc/fork/PLANKS.md), [Roof](doc/fork/ROOF.md),
[Stonework](doc/fork/STONEWORK.md), [Masonry](doc/fork/MASONRY.md) and [Distribute](doc/fork/DISTRIBUTE.md) make
whole parts from a few faces or edges.

</td>
<td width="33%" valign="top">

### <img src="doc/fork/images/icons/mod_poly_edit.png" width="32" height="32" alt=""> Edit in the stack

[Poly Select](doc/fork/POLY_SELECT.md), [Poly Edit](doc/fork/POLY_EDIT.md) and [Edit Poly](doc/fork/EDIT_POLY.md)
keep every modeling step editable, and the [Modifier List](doc/fork/MODIFIER_LIST.md) keeps long stacks short.

</td>
<td width="33%" valign="top">

### <img src="doc/fork/images/icons/mod_voronoi_fracture.png" width="32" height="32" alt=""> Break

[Voronoi Fracture](doc/fork/VORONOI_FRACTURE.md), [Shatter](doc/fork/SHATTER.md), [Dice](doc/fork/DICE.md),
[Edge Damage](doc/fork/EDGE_DAMAGE.md) and [Collapse](doc/fork/COLLAPSE.md) break, chip and drop meshes.

</td>
</tr>
<tr>
<td valign="top">

### <img src="doc/fork/images/icons/mod_uv_unwrap.png" width="32" height="32" alt=""> UVs and normals

[Unwrap](doc/fork/UV_UNWRAP.md), [Pack UVs](doc/fork/UV_PACK.md), [UV Scale](doc/fork/UV_SCALE.md) and the
[normal modifiers](#modifiers) follow the stack, with a [UV checker](doc/fork/UV_CHECKER.md) in Solid shading.

</td>
<td valign="top">

### <img src="doc/fork/images/icons/snap_align.png" width="32" height="32" alt=""> Tools and snapping

[Multi-Cut](doc/fork/MULTI_CUT.md), [face set tools](doc/fork/FACE_SETS.md), [Bounds and Align
snapping](doc/fork/SNAPPING.md), [viewport gizmos](doc/fork/MODIFIER_GIZMOS.md) and an
[orthographic view](doc/fork/ORTHOGRAPHIC.md) that stays orthographic.

</td>
<td valign="top">

### <img src="doc/fork/images/icons/shelf.png" width="32" height="32" alt=""> Your own buttons

The [Toolbox](doc/fork/TOOLBOX.md) and the [Shelf](doc/fork/SHELF.md) hold any button you right-click, and Quick
Favorites show their icons.

</td>
</tr>
</table>

| Area | What's new |
|---|---|
| [Modifiers](#modifiers) | 74 new modifiers, a [Modifier List](doc/fork/MODIFIER_LIST.md) view of the stack, [viewport gizmos](doc/fork/MODIFIER_GIZMOS.md) |
| [Geometry Nodes](#geometry-nodes) | 77 new nodes: 24 modeling nodes, plus node versions of the new and classic modifiers |
| [Tools](#tools) | [Multi-Cut](doc/fork/MULTI_CUT.md), [face set tools](doc/fork/FACE_SETS.md), Select Similar ▸ Face Set, [orthographic view fixes](doc/fork/ORTHOGRAPHIC.md), [Bounds and Align snapping](doc/fork/SNAPPING.md) |
| [Toolbox and Shelf](#toolbox-and-shelf) | Two new editors for your own buttons, the [Toolbox](doc/fork/TOOLBOX.md) and the [Shelf](doc/fork/SHELF.md), and icons in Quick Favorites |
| [Classic look](#classic-look) | Colored 2.79-style icons, embossed widgets, square vertices |
| [Solid shading](#color-attributes-in-solid-shading) | Color attributes mixed into the material color, [outlines around parts and edge lines](doc/fork/SOLID_OUTLINES.md), [toon lighting](doc/fork/TOON_SHADING.md), a [UV checker](doc/fork/UV_CHECKER.md), an [Island](doc/fork/ISLAND_COLOR.md) color, [eight more studio lights](doc/fork/STUDIO_LIGHTS.md) |
| [Materials](doc/fork/ALL_MATERIALS.md) | An All Materials tab for every material in the file, and giving a material to all selected objects |

## What's new

- **[Trim](doc/fork/TRIM.md)** lays copies of a pattern object along drawn edges, for scalloped bands, moldings and
  borders. **[Tile](doc/fork/TILE.md)** wraps the mesh into one tile so it repeats without seams.
  **[Sweep](doc/fork/SWEEP.md)** now also follows the edges of a mesh.
- **[Bounds and Align snapping](doc/fork/SNAPPING.md)**: snap to the corners and middles of bounding boxes, and line
  up with other points along X, Y and Z, with dotted guides.
- **Normals**: [Normal Transfer](doc/fork/NORMAL_TRANSFER.md), [Normal Smooth](doc/fork/NORMAL_SMOOTH.md),
  [Normals from Face Sets](doc/fork/NORMAL_FACE_SETS.md) and [Normal Shape](doc/fork/NORMAL_SHAPE.md), which shades a
  bush like one soft ball.
- **UVs that follow the stack**: [Unwrap](doc/fork/UV_UNWRAP.md) and [Pack UVs](doc/fork/UV_PACK.md) modifiers, a
  [Checker](doc/fork/UV_CHECKER.md) and an [Island](doc/fork/ISLAND_COLOR.md) color in Solid shading.
- **[Conform](doc/fork/CONFORM.md)** lays a mesh onto another surface, keeping its thickness, for roads over
  terrain, straps and decals.
- **[Eight more studio lights](doc/fork/STUDIO_LIGHTS.md)**, from soft and clay to neon and moonlight.

The Windows download may be an earlier build than the source.

<details>
<summary><b>Earlier additions</b></summary>

- **[Toon shading](doc/fork/TOON_SHADING.md)**: a Toon lighting choice in Solid mode with bands, softness, a shadow
  color, a hard highlight and a rim light.
- **[Outlines and edge lines](doc/fork/SOLID_OUTLINES.md)** around loose parts, materials or face sets, in any
  width, and lines where the surface jumps in depth, bends sharply or has hard edges.
- **[Face sets outside Sculpt Mode](doc/fork/FACE_SETS.md)**: a Face Set color in Solid shading, and Face ▸ Face
  Sets in Edit Mode to make, assign, merge, select, hide and clear them, or turn them into vertex groups or
  materials.
- **[Orthographic stays orthographic](doc/fork/ORTHOGRAPHIC.md)** when you rotate out of a front, side or top view.
- **[All Materials](doc/fork/ALL_MATERIALS.md)**: a Properties tab with every material in the file.
- **[Blast](doc/fork/BLAST.md)** filters small islands, **[Join](doc/fork/JOIN.md)** combines meshes from objects
  and collections, and **[Divide](doc/fork/DIVIDE.md)** adds grid cuts.
- **[Edge Damage](doc/fork/EDGE_DAMAGE.md)** wears and chips edges.
- **[Planks](doc/fork/PLANKS.md)** lays floors, walls, crates and barrels of boards. **[Roof](doc/fork/ROOF.md)**
  builds and dresses roofs.
- **[Carve](doc/fork/CARVE.md)** displaces with two layers of textures, patterns or attributes.
  **[Materialize](doc/fork/MATERIALIZE.md)** gives faces materials by a list of rules.
- **[Cleanup](doc/fork/CLEANUP.md)** runs the Clean Up menu in one go.
- **[Stonework](doc/fork/STONEWORK.md)** fills a mesh with whole stones, and **[Distribute](doc/fork/DISTRIBUTE.md)**
  packs objects on faces so none of them touch.
- **[Piece ID](doc/fork/PIECE_ID.md)** writes which piece each face or point belongs to.
- **[Decimate algorithms](doc/fork/DECIMATE.md)**: PMP, Meshoptimizer, CGAL and Wild QEM in the Decimate modifier.
- **[UV Scale and Texel Density](doc/fork/UV_SCALE.md)** as a modifier and a node.
- **[Dice](doc/fork/DICE.md), [Edit Poly](doc/fork/EDIT_POLY.md), [Collapse](doc/fork/COLLAPSE.md),
  [Drape](doc/fork/DRAPE.md), [Shatter](doc/fork/SHATTER.md), [Masonry](doc/fork/MASONRY.md) and
  [Dissolve](doc/fork/DISSOLVE.md)**, with Collapse Islands, Drape, Shatter and Masonry nodes.
- **[Multi-Cut](doc/fork/MULTI_CUT.md)**, an interactive cutting tool in Edit Mode.

</details>

## Modifiers

### Modifier List

The Modifiers tab has a second way to show the stack. The button in its header switches between the usual
**Stack** of panels and a **List**. Everything about it is on [its own page](doc/fork/MODIFIER_LIST.md).

- <img src="doc/fork/images/icons/modlist_view_list.png" width="32" height="32" alt="View List"> <img src="doc/fork/images/icons/modlist_view_stack.png" width="32" height="32" alt="View Stack"> **The list:** one row per modifier, with its icon, name and on/off toggles. Only the active modifier shows its
  settings, under the list, so long stacks stay short.
- <img src="doc/fork/images/icons/modlist_end_result.png" width="32" height="32" alt="End Result"> **Show End Result:** when it's off, the viewport shows the stack only up to the active modifier, so you can
  see what each step does without turning the others off.
- <img src="doc/fork/images/icons/modlist_first_on_top.png" width="32" height="32" alt="First On Top"> <img src="doc/fork/images/icons/modlist_last_on_top.png" width="32" height="32" alt="Last On Top"> **Order:** the list can show the first modifier on top, or the last one.
- <img src="doc/fork/images/icons/modlist_rename.png" width="32" height="32" alt="Rename"> <img src="doc/fork/images/icons/modlist_copy.png" width="32" height="32" alt="Copy"> <img src="doc/fork/images/icons/modlist_paste.png" width="32" height="32" alt="Paste"> <img src="doc/fork/images/icons/modlist_collapse_to.png" width="32" height="32" alt="Collapse To"> <img src="doc/fork/images/icons/modlist_apply_all.png" width="32" height="32" alt="Apply All"> <img src="doc/fork/images/icons/modlist_delete_all.png" width="32" height="32" alt="Delete All"> <img src="doc/fork/images/icons/modlist_copy_stack.png" width="32" height="32" alt="Copy Stack"> **Right-click menu:** rename, copy and paste, move, enable or disable, collapse up to here, apply all,
  delete all, and copy the whole stack to the selected objects.
- <img src="doc/fork/images/icons/modlist_favorites.png" width="32" height="32" alt="Favorites"> **Favorites:** reorder your favorite modifiers, choose one to three columns, and mark the modifiers you use most, and they're listed first when you add one. They're saved in
  your user settings, so every file has them.
- **Add menu:** every mesh modifier in one menu of seven columns: Model, Topology and Smooth, Elements and
  Select and Normals, Deform, Shape, UV and Data, and Physics with Geometry Nodes.

![The Modifier List: the stack as a list, with the settings of the active Poly Edit under it](doc/fork/images/ui_modifier_list.png)

![The Add menu of the Modifier List, with every mesh modifier in seven columns](doc/fork/images/ui_modifier_add_menu.png)

### New modifiers

All 74 work like the built-in ones: they're in the Add Modifier menu with their own icons, they can be applied,
and most of them also work in Edit Mode.

![Voronoi Fracture with Exploded View, a tower made with Poly Select and four Poly Edits, Panelize on a sphere, and Radial Array](doc/fork/images/modifiers_modeling.png)

*From left to right: Voronoi Fracture with Exploded View, Poly Select with four Poly Edits, Panelize and Radial
Array.*

**Model and topology**

| | Modifier | What it does |
|---|---|---|
| <img src="doc/fork/images/icons/mod_isotropic_remesh.png" width="32" height="32" alt="Isotropic Remesh"> | [Isotropic Remesh](doc/fork/ISOTROPIC_REMESH.md) | Rebuilds the mesh out of even, near-equilateral triangles that follow its surface |
| <img src="doc/fork/images/icons/mod_quadify.png" width="32" height="32" alt="QuadriFlow Remesh"> | [QuadriFlow Remesh](doc/fork/QUADRIFLOW_REMESH.md) | Rebuilds the mesh out of quads that follow its shape |
| <img src="doc/fork/images/icons/mod_voronoi_fracture.png" width="32" height="32" alt="Voronoi Fracture"> | [Voronoi Fracture](doc/fork/VORONOI_FRACTURE.md) | Cuts a closed mesh into solid pieces, from random points or the points of another object |
| <img src="doc/fork/images/icons/mod_slice.png" width="32" height="32" alt="Slice"> | [Slice](doc/fork/SLICE.md) | Cuts the mesh with a plane and removes one side, optionally filling the cut |
| <img src="doc/fork/images/icons/mod_cap_holes.png" width="32" height="32" alt="Cap Holes"> | [Cap Holes](doc/fork/CAP_HOLES.md) | Closes the holes of the mesh with new faces |
| <img src="doc/fork/images/icons/mod_face_extrude.png" width="32" height="32" alt="Face Extrude"> | [Face Extrude](doc/fork/FACE_EXTRUDE.md) | Extrudes faces along their normals and scales them |
| <img src="doc/fork/images/icons/mod_inset.png" width="32" height="32" alt="Inset"> | [Inset](doc/fork/INSET.md) | Insets faces, each on its own or as one region |
| <img src="doc/fork/images/icons/mod_panelize.png" width="32" height="32" alt="Panelize"> | [Panelize](doc/fork/PANELIZE.md) | Splits the faces into panels with a gap and a chamfer between them |
| <img src="doc/fork/images/icons/mod_poke.png" width="32" height="32" alt="Poke"> | [Poke](doc/fork/POKE.md) | Splits each face into a fan of triangles around its center |
| <img src="doc/fork/images/icons/mod_quadrangulate.png" width="32" height="32" alt="Quadrangulate"> | [Quadrangulate](doc/fork/QUADRANGULATE.md) | Joins pairs of triangles into quads |
| <img src="doc/fork/images/icons/mod_beautify.png" width="32" height="32" alt="Beautify Faces"> | [Beautify Faces](doc/fork/BEAUTIFY_FACES.md) | Flips the edges between triangles so they're more even |
| <img src="doc/fork/images/icons/mod_rotate_edges.png" width="32" height="32" alt="Rotate Edges"> | [Rotate Edges](doc/fork/ROTATE_EDGES.md) | Turns the edges between faces to connect the other corners |
| <img src="doc/fork/images/icons/mod_symmetrize.png" width="32" height="32" alt="Symmetrize"> | [Symmetrize](doc/fork/SYMMETRIZE.md) | Makes one side of the mesh a mirror of the other |
| <img src="doc/fork/images/icons/mod_sweep.png" width="32" height="32" alt="Sweep"> | [Sweep](doc/fork/SWEEP.md) | Makes a mesh by moving a shape along curves or the edges of a mesh, with resampling and smoothing |
| <img src="doc/fork/images/icons/mod_trim.png" width="32" height="32" alt="Trim"> | [Trim](doc/fork/TRIM.md) | Lays copies of a pattern object along the edges of the mesh, like a scalloped band |
| <img src="doc/fork/images/icons/mod_dice.png" width="32" height="32" alt="Dice"> | [Dice](doc/fork/DICE.md) | Cuts a mesh into a grid of solid pieces, by count or size |
| <img src="doc/fork/images/icons/mod_divide.png" width="32" height="32" alt="Divide"> | [Divide](doc/fork/DIVIDE.md) | Divides polygon surfaces into connected grid-like sections with adjustable size, offset and rotation |
| <img src="doc/fork/images/icons/mod_join.png" width="32" height="32" alt="Join"> | [Join](doc/fork/JOIN.md) | Combines evaluated meshes from objects and collections without Boolean operations, with an ID per object and per parent |
| <img src="doc/fork/images/icons/mod_collapse.png" width="32" height="32" alt="Collapse"> | [Collapse](doc/fork/COLLAPSE.md) | Drops loose parts, or pieces from an attribute, as rigid pieces onto ground objects and collections, or a floor |
| <img src="doc/fork/images/icons/mod_drape.png" width="32" height="32" alt="Drape"> | [Drape](doc/fork/DRAPE.md) | Settles a mesh as cloth over collider objects and collections, with pinning and optional self collision |
| <img src="doc/fork/images/icons/mod_shatter.png" width="32" height="32" alt="Shatter"> | [Shatter](doc/fork/SHATTER.md) | Breaks a closed mesh with stone, glass, wood, ceramic or ice patterns |
| <img src="doc/fork/images/icons/mod_masonry.png" width="32" height="32" alt="Masonry"> | [Masonry](doc/fork/MASONRY.md) | Covers faces with brick, stone or paving patterns |
| <img src="doc/fork/images/icons/mod_planks.png" width="32" height="32" alt="Planks"> | [Planks](doc/fork/PLANKS.md) | Floors and walls of boards on faces, and crates and barrels of boards |
| <img src="doc/fork/images/icons/mod_roof.png" width="32" height="32" alt="Roof"> | [Roof](doc/fork/ROOF.md) | Builds roofs on outlines, or dresses modeled roofs |
| <img src="doc/fork/images/icons/mod_carve.png" width="32" height="32" alt="Carve"> | [Carve](doc/fork/CARVE.md) | Displaces with two texture, pattern or attribute layers, masked |
| <img src="doc/fork/images/icons/mod_materialize.png" width="32" height="32" alt="Materialize"> | [Materialize](doc/fork/MATERIALIZE.md) | Gives faces materials by a list of rules |
| <img src="doc/fork/images/icons/mod_cleanup.png" width="32" height="32" alt="Cleanup"> | [Cleanup](doc/fork/CLEANUP.md) | Runs the Clean Up menu's steps and more in one go |
| <img src="doc/fork/images/icons/mod_stonework.png" width="32" height="32" alt="Stonework"> | [Stonework](doc/fork/STONEWORK.md) | Fills a mesh with whole stones, stacked from the bottom up |
| <img src="doc/fork/images/icons/mod_distribute.png" width="32" height="32" alt="Distribute"> | [Distribute](doc/fork/DISTRIBUTE.md) | Packs objects from a collection on the faces, so none of them touch |
| <img src="doc/fork/images/icons/mod_edge_damage.png" width="32" height="32" alt="Edge Damage"> | [Edge Damage](doc/fork/EDGE_DAMAGE.md) | Wears and chips the edges and corners of closed meshes |
| <img src="doc/fork/images/icons/mod_dissolve.png" width="32" height="32" alt="Dissolve"> | [Dissolve](doc/fork/DISSOLVE.md) | Removes points, edges or faces and merges the surrounding faces |

**Selection and editing in the stack**

| | Modifier | What it does |
|---|---|---|
| <img src="doc/fork/images/icons/mod_edit_poly.png" width="32" height="32" alt="Edit Poly"> | [Edit Poly](doc/fork/EDIT_POLY.md) | Edits a stored mesh at this point in the stack, with optional upstream point movement |
| <img src="doc/fork/images/icons/mod_poly_select.png" width="32" height="32" alt="Poly Select"> | [Poly Select](doc/fork/POLY_SELECT.md) | Stores a selection of points, edges or faces in the stack, for the modifiers after it |
| <img src="doc/fork/images/icons/mod_poly_edit.png" width="32" height="32" alt="Poly Edit"> | [Poly Edit](doc/fork/POLY_EDIT.md) | Moves, extrudes, insets, pokes or deletes the selected elements |
| <img src="doc/fork/images/icons/mod_vol_select.png" width="32" height="32" alt="Volume Select"> | [Volume Select](doc/fork/VOLUME_SELECT.md) | Writes a vertex group with the points inside a sphere, box or cylinder |
| <img src="doc/fork/images/icons/mod_delete_mesh.png" width="32" height="32" alt="Delete Mesh"> | [Delete Mesh](doc/fork/DELETE_MESH.md) | Deletes the points, edges or faces picked by a vertex group or an attribute |
| <img src="doc/fork/images/icons/mod_blast.png" width="32" height="32" alt="Blast"> | [Blast](doc/fork/BLAST.md) | Removes small disconnected islands or keeps the N largest parts |

Poly Select and Poly Edit work together. Select faces in Edit Mode, store them with Poly Select, then add Poly
Edit above it. The faces Poly Edit makes become the new selection, so another Poly Edit can carry on from them.
Every step stays editable, like a history you can go back and change.

**Elements**

| | Modifier | What it does |
|---|---|---|
| <img src="doc/fork/images/icons/mod_radial_array.png" width="32" height="32" alt="Radial Array"> | [Radial Array](doc/fork/RADIAL_ARRAY.md) | Arranges copies of the mesh around an axis |
| <img src="doc/fork/images/icons/mod_tile.png" width="32" height="32" alt="Tile"> | [Tile](doc/fork/TILE.md) | Wraps the mesh into one tile so it repeats without seams |
| <img src="doc/fork/images/icons/mod_exploded_view.png" width="32" height="32" alt="Exploded View"> | [Exploded View](doc/fork/EXPLODED_VIEW.md) | Pushes faces, islands or other elements apart, with a factor you can animate |
| <img src="doc/fork/images/icons/mod_randomize_elements.png" width="32" height="32" alt="Randomize Elements"> | [Randomize Elements](doc/fork/RANDOMIZE_ELEMENTS.md) | Gives each face, island or other element a random rotation, scale, offset, UVs or color |
| <img src="doc/fork/images/icons/mod_element_order.png" width="32" height="32" alt="Element Order"> | [Element Order](doc/fork/ELEMENT_ORDER.md) | Reorders faces or islands, so Build and other order-based effects run the way you want |
| <img src="doc/fork/images/icons/mod_element_pivot.png" width="32" height="32" alt="Store Element Pivot"> | [Store Element Pivot](doc/fork/STORE_ELEMENT_PIVOT.md) | Writes the center and axes of each island or other element to point attributes |

**Deform and shape**

| | Modifier | What it does |
|---|---|---|
| <img src="doc/fork/images/icons/mod_bend.png" width="32" height="32" alt="Bend"> <img src="doc/fork/images/icons/mod_twist.png" width="32" height="32" alt="Twist"> <img src="doc/fork/images/icons/mod_taper.png" width="32" height="32" alt="Taper"> <img src="doc/fork/images/icons/mod_stretch.png" width="32" height="32" alt="Stretch"> | [Bend, Twist, Taper, Stretch](doc/fork/BEND_TWIST_TAPER_STRETCH.md) | Deform the mesh around a gizmo axis, with limits |
| <img src="doc/fork/images/icons/mod_ffd_2.png" width="32" height="32" alt="FFD 2x2x2"> <img src="doc/fork/images/icons/mod_ffd_3.png" width="32" height="32" alt="FFD 3x3x3"> <img src="doc/fork/images/icons/mod_ffd_4.png" width="32" height="32" alt="FFD 4x4x4"> | [FFD 2x2x2, 3x3x3, 4x4x4](doc/fork/FFD.md) | Bend the shape with a box of control points that you move in the viewport |
| <img src="doc/fork/images/icons/mod_noise_deform.png" width="32" height="32" alt="Noise"> | [Noise](doc/fork/NOISE.md) | Moves the points by fractal noise on X, Y and Z, without a texture |
| <img src="doc/fork/images/icons/mod_push.png" width="32" height="32" alt="Push"> | [Push](doc/fork/PUSH.md) | Inflates or shrinks the mesh along its normals |
| <img src="doc/fork/images/icons/mod_melt.png" width="32" height="32" alt="Melt"> | [Melt](doc/fork/MELT.md) | Sags the shape and spreads it out toward the ground |
| <img src="doc/fork/images/icons/mod_relax.png" width="32" height="32" alt="Relax"> | [Relax](doc/fork/RELAX.md) | Evens out the spacing of the points while keeping them on the surface |
| <img src="doc/fork/images/icons/mod_shapekey_inbetween.png" width="32" height="32" alt="Shape Key In-between"> | [Shape Key In-between](doc/fork/SHAPE_KEY_INBETWEEN.md) | Morphs through a sequence of shape keys with one value |
| <img src="doc/fork/images/icons/mod_conform.png" width="32" height="32" alt="Conform"> | [Conform](doc/fork/CONFORM.md) | Lays the mesh onto the surface of another object, keeping its thickness |

![Bend, Twist, Taper and Noise on four bars](doc/fork/images/modifiers_deform.png)

*Bend, Twist, Taper and Noise.*

![A grid with uneven points, and the same grid after Relax](doc/fork/images/modifier_relax.png)

*A grid with uneven points, and the same grid after Relax.*

**UV, normals and data**

| | Modifier | What it does |
|---|---|---|
| <img src="doc/fork/images/icons/mod_uv_scale.png" width="32" height="32" alt="UV Scale"> | [UV Scale](doc/fork/UV_SCALE.md) | Scales UVs globally or per island, or matches a world-space texel density between objects |
| <img src="doc/fork/images/icons/mod_uvw_map.png" width="32" height="32" alt="UVW Map"> | [UVW Map](doc/fork/UVW_MAP.md) | Projects a UV map from a planar, cylindrical, spherical or box gizmo |
| <img src="doc/fork/images/icons/mod_smart_uv_project.png" width="32" height="32" alt="Smart UV Project"> | [Smart UV Project](doc/fork/SMART_UV_PROJECT.md) | Unwraps the mesh automatically and packs the islands |
| <img src="doc/fork/images/icons/mod_uv_unwrap.png" width="32" height="32" alt="Unwrap"> | [Unwrap](doc/fork/UV_UNWRAP.md) | Unwraps the mesh from its seams and packs the islands, so the UVs follow the stack |
| <img src="doc/fork/images/icons/mod_uv_pack.png" width="32" height="32" alt="Pack UVs"> | [Pack UVs](doc/fork/UV_PACK.md) | Packs the islands of a UV map into the 0 to 1 square |
| <img src="doc/fork/images/icons/mod_mark_by_angle.png" width="32" height="32" alt="Mark by Angle"> | [Mark by Angle](doc/fork/MARK_BY_ANGLE.md) | Marks sharp edges, seams, creases or bevel weights where faces meet at a steep angle |
| <img src="doc/fork/images/icons/mod_normal_transfer.png" width="32" height="32" alt="Normal Transfer"> | [Normal Transfer](doc/fork/NORMAL_TRANSFER.md) | Copies the shading normals of another object, fading out with distance |
| <img src="doc/fork/images/icons/mod_normal_smooth.png" width="32" height="32" alt="Normal Smooth"> | [Normal Smooth](doc/fork/NORMAL_SMOOTH.md) | Smooths the shading normals without moving the mesh |
| <img src="doc/fork/images/icons/mod_normal_face_sets.png" width="32" height="32" alt="Normals from Face Sets"> | [Normals from Face Sets](doc/fork/NORMAL_FACE_SETS.md) | Makes the shading hard where face sets meet and smooth inside each one |
| <img src="doc/fork/images/icons/mod_normal_shape.png" width="32" height="32" alt="Normal Shape"> | [Normal Shape](doc/fork/NORMAL_SHAPE.md) | Points the shading normals as if the mesh were a sphere, cylinder or box |
| <img src="doc/fork/images/icons/mod_material_by_element.png" width="32" height="32" alt="Material By Element"> | [Material By Element](doc/fork/MATERIAL_BY_ELEMENT.md) | Gives faces material indices at random, by piece, in order or by direction |
| <img src="doc/fork/images/icons/mod_data_channel.png" width="32" height="32" alt="Data Channel"> | [Data Channel](doc/fork/DATA_CHANNEL.md) | Computes a value per point, such as convexity or edge length, remaps it and writes it out |
| <img src="doc/fork/images/icons/mod_piece_id.png" width="32" height="32" alt="Piece ID"> | [Piece ID](doc/fork/PIECE_ID.md) | Writes which piece each face or point belongs to, from loose parts, edges, materials, face sets, a group, an attribute or the selection |
| <img src="doc/fork/images/icons/mod_tension.png" width="32" height="32" alt="Tension"> | [Tension](doc/fork/TENSION.md) | Stores how much the mesh is stretched or squashed compared to its rest shape |

**Curves**

| | Modifier | What it does |
|---|---|---|
| <img src="doc/fork/images/icons/mod_normalize_spline.png" width="32" height="32" alt="Normalize Spline"> | [Normalize Spline](doc/fork/NORMALIZE_SPLINE.md) | Respaces the points of the curves evenly |
| <img src="doc/fork/images/icons/mod_relax_spline.png" width="32" height="32" alt="Relax Spline"> | [Relax Spline](doc/fork/RELAX_SPLINE.md) | Smooths the curves |
| <img src="doc/fork/images/icons/mod_chamfer_spline.png" width="32" height="32" alt="Chamfer Spline"> | [Chamfer Spline](doc/fork/CHAMFER_SPLINE.md) | Cuts the corners of the curves, straight or rounded |
| <img src="doc/fork/images/icons/mod_spline_morph.png" width="32" height="32" alt="Spline Morph"> | [Spline Morph](doc/fork/SPLINE_MORPH.md) | Moves the curves toward the curves of another object |

### Viewport gizmos

The active modifier can be edited in the viewport. More on [the gizmos page](doc/fork/MODIFIER_GIZMOS.md).

- **Bend, Twist, Taper, Stretch, UVW Map, Volume Select and Radial Array:** a handle moves the center, a dial
  sets the angle, and markers set the limits, with a line between them.
- **Slice:** a handle for the plane and a marker for its distance.
- **FFD:** a dot on every control point, with the cage drawn between them, also while dragging.

They follow the viewport's Gizmos ▸ Active Modifier toggle.

## Geometry Nodes

77 new nodes for the Geometry Nodes editor. They're real nodes, not add-ons or node groups.

### Modeling nodes

Add them with **Add ▸ Mesh ▸ Operations**, or **Add ▸ Curve ▸ Operations** for Sweep. Each one is described in
more detail [further down](#modeling-nodes-in-detail).

| | Node | What it does |
|---|---|---|
| <img src="doc/fork/images/icons/mod_isotropic_remesh.png" width="32" height="32" alt="Isotropic Remesh"> | [Isotropic Remesh](#isotropic-remesh) | Rebuild a mesh out of uniformly sized, near-equilateral triangles that follow the original surface |
| <img src="doc/fork/images/icons/geo_n_decimate_mesh.png" width="32" height="32" alt="Decimate Mesh"> | [Decimate Mesh](#decimate-mesh) | Reduce the number of triangles of a mesh, removing detail where it changes the surface the least |
| <img src="doc/fork/images/icons/mod_voronoi_fracture.png" width="32" height="32" alt="Voronoi Fracture"> | [Voronoi Fracture](#voronoi-fracture) | Cut a closed mesh into solid pieces, one for the area around each of the given points |
| <img src="doc/fork/images/icons/geo_mod_bevel.png" width="32" height="32" alt="Bevel"> | [Bevel](#bevel) | Bevel the edges or vertices of a mesh, like the Bevel modifier |
| <img src="doc/fork/images/icons/mod_sweep.png" width="32" height="32" alt="Sweep"> | [Sweep](#sweep) | Make a mesh by moving a shape along curves, with control over its scale, roll and twist |
| <img src="doc/fork/images/icons/geo_resample.png" width="32" height="32" alt="Resample Mesh"> | [Resample Mesh](#resample-mesh) | Add points along the edges of a mesh without changing its shape or its number of faces |
| <img src="doc/fork/images/icons/geo_n_quadri_flow_remesh.png" width="32" height="32" alt="QuadriFlow Remesh"> | [QuadriFlow Remesh](#quadriflow-remesh) | Rebuild a manifold mesh out of quads that follow the flow of its surface |
| <img src="doc/fork/images/icons/geo_bisect.png" width="32" height="32" alt="Clip"> | [Clip](#clip) | Cut a mesh with a plane and remove one side, optionally filling the cut |
| <img src="doc/fork/images/icons/geo_knife.png" width="32" height="32" alt="Dice"> | [Dice](#dice) | Cut a mesh into a grid of pieces along the X, Y and Z axes of its bounding box |
| <img src="doc/fork/images/icons/geo_mod_decim.png" width="32" height="32" alt="Dissolve"> | [Dissolve](#dissolve) | Remove points, edges or faces and merge the faces around them, without leaving holes |
| <img src="doc/fork/images/icons/geo_mod_smooth.png" width="32" height="32" alt="Smooth"> | [Smooth](#smooth) | Smooth the surface of a mesh by moving its points |
| <img src="doc/fork/images/icons/mod_relax.png" width="32" height="32" alt="Relax"> | [Relax](#relax) | Even out the distances between the points of a mesh by sliding them along its surface |
| <img src="doc/fork/images/icons/geo_mod_displace.png" width="32" height="32" alt="Displace"> | [Displace](#displace) | Move the points of a mesh by a height or vector, like the Displace modifier |
| <img src="doc/fork/images/icons/geo_mod_collapse.png" width="32" height="32" alt="Collapse Islands"> | [Collapse Islands](doc/fork/NODES.md#collapse-islands) | Drops connected islands as rigid pieces onto ground geometry or a floor |
| <img src="doc/fork/images/icons/geo_mod_drape.png" width="32" height="32" alt="Drape"> | [Drape](doc/fork/NODES.md#drape) | Settles cloth over collider geometry, with a Pin field |
| <img src="doc/fork/images/icons/geo_mod_shatter.png" width="32" height="32" alt="Shatter"> | [Shatter](doc/fork/NODES.md#shatter) | Breaks a closed mesh and outputs Piece Index and Interior fields |
| <img src="doc/fork/images/icons/geo_mod_masonry.png" width="32" height="32" alt="Masonry"> | [Masonry](doc/fork/NODES.md#masonry) | Creates patterned blocks with Block Index and Mortar fields |
| <img src="doc/fork/images/icons/geo_mod_planks.png" width="32" height="32" alt="Planks"> | [Planks](doc/fork/PLANKS.md) | Floors, walls, crates and barrels of boards, with Board Index, Random and Part fields |
| <img src="doc/fork/images/icons/geo_mod_roof.png" width="32" height="32" alt="Roof"> | [Roof](doc/fork/ROOF.md) | Builds or covers roofs, with Part and Random fields |
| <img src="doc/fork/images/icons/geo_mod_carve.png" width="32" height="32" alt="Carve"> | [Carve](doc/fork/CARVE.md) | Displaces with two pattern or value layers, with Height, Mask and Curvature fields |
| <img src="doc/fork/images/icons/geo_mod_materialize.png" width="32" height="32" alt="Materialize"> | [Materialize](doc/fork/MATERIALIZE.md) | Gives a material to faces picked by one rule, with a Picked field |
| <img src="doc/fork/images/icons/geo_mod_cleanup.png" width="32" height="32" alt="Cleanup"> | [Cleanup](doc/fork/CLEANUP.md) | Runs the Clean Up menu's steps and more in one go, on a selection |
| <img src="doc/fork/images/icons/geo_mod_stonework.png" width="32" height="32" alt="Stonework"> | [Stonework](doc/fork/STONEWORK.md) | Fills a mesh with whole stones, with Stone ID and Random fields |
| <img src="doc/fork/images/icons/geo_mod_distribute.png" width="32" height="32" alt="Distribute"> | [Distribute](doc/fork/DISTRIBUTE.md) | Packs instances on the faces, so none of them touch, with ID, Random, Color and Filler fields |

![The original thirteen modeling nodes side by side in the Geometry Nodes editor](doc/fork/images/nodes_modeling.png)

### Node versions of the new modifiers

These nodes share their code with the new modifiers, so a node tree and a modifier give the same result.

| | Node | What it does |
|---|---|---|
| <img src="doc/fork/images/icons/mod_cap_holes.png" width="32" height="32" alt="Cap Holes"> | [Cap Holes](doc/fork/CAP_HOLES.md) | Close the holes of a mesh with new faces |
| <img src="doc/fork/images/icons/mod_face_extrude.png" width="32" height="32" alt="Face Extrude"> | [Face Extrude](doc/fork/FACE_EXTRUDE.md) | Extrude faces along their normals and scale them |
| <img src="doc/fork/images/icons/mod_inset.png" width="32" height="32" alt="Inset"> | [Inset](doc/fork/INSET.md) | Inset faces, each on its own or as one region |
| <img src="doc/fork/images/icons/mod_panelize.png" width="32" height="32" alt="Panelize"> | [Panelize](doc/fork/PANELIZE.md) | Split faces into panels with a gap and a chamfer between them |
| <img src="doc/fork/images/icons/mod_poke.png" width="32" height="32" alt="Poke"> | [Poke](doc/fork/POKE.md) | Split faces into a fan of triangles around their center |
| <img src="doc/fork/images/icons/mod_quadrangulate.png" width="32" height="32" alt="Quadrangulate"> | [Quadrangulate](doc/fork/QUADRANGULATE.md) | Join pairs of triangles into quads, the same as Triangles to Quads in edit mode |
| <img src="doc/fork/images/icons/mod_beautify.png" width="32" height="32" alt="Beautify Faces"> | [Beautify Faces](doc/fork/BEAUTIFY_FACES.md) | Flip the edges between triangles so they are more even |
| <img src="doc/fork/images/icons/mod_rotate_edges.png" width="32" height="32" alt="Rotate Edges"> | [Rotate Edges](doc/fork/ROTATE_EDGES.md) | Turn the edges between faces to connect the other corners |
| <img src="doc/fork/images/icons/mod_symmetrize.png" width="32" height="32" alt="Symmetrize"> | [Symmetrize](doc/fork/SYMMETRIZE.md) | Make one side of a mesh a mirror of the other |
| <img src="doc/fork/images/icons/mod_radial_array.png" width="32" height="32" alt="Radial Array"> | [Radial Array](doc/fork/RADIAL_ARRAY.md) | Copies of a mesh arranged around an axis |
| <img src="doc/fork/images/icons/mod_exploded_view.png" width="32" height="32" alt="Exploded View"> | [Exploded View](doc/fork/EXPLODED_VIEW.md) | Push faces, islands or other elements apart, with a factor that can be animated |
| <img src="doc/fork/images/icons/mod_randomize_elements.png" width="32" height="32" alt="Randomize Elements"> | [Randomize Elements](doc/fork/RANDOMIZE_ELEMENTS.md) | Random rotation, scale and offset, UVs or color for each face, island or other element |
| <img src="doc/fork/images/icons/mod_element_order.png" width="32" height="32" alt="Element Order"> | [Element Order](doc/fork/ELEMENT_ORDER.md) | Reorder faces or islands, keeping the faces of each element together |
| <img src="doc/fork/images/icons/mod_element_pivot.png" width="32" height="32" alt="Element Pivot"> | [Element Pivot](doc/fork/STORE_ELEMENT_PIVOT.md) | The center and axes of the island or other element of each point |
| <img src="doc/fork/images/icons/mod_bend.png" width="32" height="32" alt="Bend"> | [Bend](doc/fork/BEND_TWIST_TAPER_STRETCH.md) | Bend geometry around a gizmo, with limits |
| <img src="doc/fork/images/icons/mod_twist.png" width="32" height="32" alt="Twist"> | [Twist](doc/fork/BEND_TWIST_TAPER_STRETCH.md) | Twist geometry around the axis of a gizmo, with limits |
| <img src="doc/fork/images/icons/mod_taper.png" width="32" height="32" alt="Taper"> | [Taper](doc/fork/BEND_TWIST_TAPER_STRETCH.md) | Scale geometry more along the axis of a gizmo |
| <img src="doc/fork/images/icons/mod_stretch.png" width="32" height="32" alt="Stretch"> | [Stretch](doc/fork/BEND_TWIST_TAPER_STRETCH.md) | Stretch geometry along the axis of a gizmo and thin it in the middle |
| <img src="doc/fork/images/icons/mod_ffd_3.png" width="32" height="32" alt="FFD"> | [FFD](doc/fork/FFD.md) | Bend geometry with a box lattice of 2, 3 or 4 control points per side |
| <img src="doc/fork/images/icons/mod_noise_deform.png" width="32" height="32" alt="Noise Deform"> | [Noise Deform](doc/fork/NOISE.md) | Move points by a fractal noise on X, Y and Z |
| <img src="doc/fork/images/icons/mod_push.png" width="32" height="32" alt="Push"> | [Push](doc/fork/PUSH.md) | Inflate or shrink a mesh along its normals |
| <img src="doc/fork/images/icons/mod_melt.png" width="32" height="32" alt="Melt"> | [Melt](doc/fork/MELT.md) | Sag geometry and spread it out toward the ground |
| <img src="doc/fork/images/icons/mod_uvw_map.png" width="32" height="32" alt="UVW Map"> | [UVW Map](doc/fork/UVW_MAP.md) | Project a UV map from a planar, cylindrical, spherical or box gizmo |
| <img src="doc/fork/images/icons/geo_mod_uv_scale.png" width="32" height="32" alt="UV Scale"> | [UV Scale](doc/fork/UV_SCALE.md) | Scales an existing UV map globally or per island, or sets pixels per meter |
| <img src="doc/fork/images/icons/mod_smart_uv_project.png" width="32" height="32" alt="Smart UV Project"> | [Smart UV Project](doc/fork/SMART_UV_PROJECT.md) | Unwrap a mesh automatically: faces are projected from a few directions and packed |
| <img src="doc/fork/images/icons/mod_mark_by_angle.png" width="32" height="32" alt="Mark by Angle"> | [Mark by Angle](doc/fork/MARK_BY_ANGLE.md) | Mark sharp edges, seams, creases or bevel weights where faces meet at a steep angle |
| <img src="doc/fork/images/icons/mod_material_by_element.png" width="32" height="32" alt="Material By Element"> | [Material By Element](doc/fork/MATERIAL_BY_ELEMENT.md) | Give faces material indices at random, by piece, in order or by direction |
| <img src="doc/fork/images/icons/mod_data_channel.png" width="32" height="32" alt="Data Channel"> | [Data Channel](doc/fork/DATA_CHANNEL.md) | Compute a value per point, such as convexity or edge length, and remap it |
| <img src="doc/fork/images/icons/mod_tension.png" width="32" height="32" alt="Mesh Tension"> | [Mesh Tension](doc/fork/TENSION.md) | How much the edges around each point are stretched or squashed compared to a rest shape |
| <img src="doc/fork/images/icons/mod_vol_select.png" width="32" height="32" alt="Volume Select"> | [Volume Select](doc/fork/VOLUME_SELECT.md) | Select the points inside a sphere, box or cylinder, with a soft falloff |
| <img src="doc/fork/images/icons/mod_spline_morph.png" width="32" height="32" alt="Morph Curves"> | [Morph Curves](doc/fork/SPLINE_MORPH.md) | Move curves toward the matching curves of a target |
| <img src="doc/fork/images/icons/mod_relax_spline.png" width="32" height="32" alt="Relax Curves"> | [Relax Curves](doc/fork/RELAX_SPLINE.md) | Smooth the shape of curves |

![The original 31 node versions of the new modifiers in the Geometry Nodes editor](doc/fork/images/nodes_modifier_ops.png)

### Classic modifiers as nodes

The classic modifiers that had no node now have one too.

| | Node | What it does |
|---|---|---|
| <img src="doc/fork/images/icons/mod_array.png" width="32" height="32" alt="Array"> | Array | Repeat a mesh with offsets and merge the copies, the same as the Array modifier |
| <img src="doc/fork/images/icons/mod_build.png" width="32" height="32" alt="Build"> | Build | Show the faces of a mesh one by one over time, the same as the Build modifier |
| <img src="doc/fork/images/icons/mod_cast.png" width="32" height="32" alt="Cast"> | Cast | Push a mesh toward a sphere, cylinder or box, the same as the Cast modifier |
| <img src="doc/fork/images/icons/mod_curve.png" width="32" height="32" alt="Curve Deform"> | Curve Deform | Bend a mesh along a curve object, the same as the Curve modifier |
| <img src="doc/fork/images/icons/mod_edgesplit.png" width="32" height="32" alt="Edge Split"> | Edge Split | Split the edges that are sharp or meet at a large angle, the same as the Edge Split modifier |
| <img src="doc/fork/images/icons/mod_lattice.png" width="32" height="32" alt="Lattice Deform"> | Lattice Deform | Deform a mesh with a lattice object, the same as the Lattice modifier |
| <img src="doc/fork/images/icons/mod_laplacian_smooth.png" width="32" height="32" alt="Laplacian Smooth"> | Laplacian Smooth | Smooth a mesh while keeping its volume, the same as the Laplacian Smooth modifier |
| <img src="doc/fork/images/icons/mod_mirror.png" width="32" height="32" alt="Mirror"> | Mirror | Mirror a mesh along its axes, with optional bisect and merge, the same as the Mirror modifier |
| <img src="doc/fork/images/icons/mod_normaledit.png" width="32" height="32" alt="Normal Edit"> | Normal Edit | Set custom normals pointing away from a point or along a direction, the same as the Normal Edit modifier |
| <img src="doc/fork/images/icons/mod_remesh.png" width="32" height="32" alt="Remesh Blocks"> | Remesh Blocks | Rebuild a mesh out of blocks or voxels, the same as the Remesh modifier |
| <img src="doc/fork/images/icons/mod_screw.png" width="32" height="32" alt="Screw"> | Screw | Sweep a profile around an axis, the same as the Screw modifier |
| <img src="doc/fork/images/icons/mod_shrinkwrap.png" width="32" height="32" alt="Shrinkwrap"> | Shrinkwrap | Move the points of a mesh onto the surface of another mesh, the same as the Shrinkwrap modifier |
| <img src="doc/fork/images/icons/mod_simpledeform.png" width="32" height="32" alt="Simple Deform"> | Simple Deform | Twist, bend, taper or stretch a mesh, the same as the Simple Deform modifier |
| <img src="doc/fork/images/icons/mod_skin.png" width="32" height="32" alt="Skin"> | Skin | Build a skin around the edges of a mesh, the same as the Skin modifier |
| <img src="doc/fork/images/icons/mod_smooth.png" width="32" height="32" alt="Smooth Points"> | Smooth Points | Smooth the points of a mesh, the same as the Smooth modifier |
| <img src="doc/fork/images/icons/mod_solidify.png" width="32" height="32" alt="Solidify"> | Solidify | Give the surface of a mesh thickness, the same as the Solidify modifier |
| <img src="doc/fork/images/icons/mod_uvproject.png" width="32" height="32" alt="UV Warp"> | UV Warp | Move, rotate and scale the UV map, the same as the UV Warp modifier |
| <img src="doc/fork/images/icons/mod_warp.png" width="32" height="32" alt="Warp"> | Warp | Move a part of a mesh from one object to another, the same as the Warp modifier |
| <img src="doc/fork/images/icons/mod_wave.png" width="32" height="32" alt="Wave"> | Wave | Move the points of a mesh in ripples, the same as the Wave modifier |
| <img src="doc/fork/images/icons/mod_weighted_normal.png" width="32" height="32" alt="Weighted Normal"> | Weighted Normal | Set custom normals from the area or angles of the faces, the same as the Weighted Normal modifier |
| <img src="doc/fork/images/icons/mod_wireframe.png" width="32" height="32" alt="Wireframe"> | Wireframe | Turn the edges of a mesh into thin bars, the same as the Wireframe modifier |

![The 21 classic modifiers as nodes in the Geometry Nodes editor](doc/fork/images/nodes_classic_modifiers.png)

### Modeling nodes in detail

#### Isotropic Remesh

Rebuilds a mesh out of even, near-equilateral triangles that follow the original surface. Hard Edges keeps the
edges you choose, like the eye and mouth rims here.

- **Modes:** Uniform uses one edge length, and zero uses the input's average. Adaptive uses shorter edges where
  the surface curves, between a Min and Max Edge Length and within a Tolerance.
- **What to keep:** Preserve Boundary keeps open edges in place, and Preserve Sharp Edges keeps edges above a
  Sharp Angle. Hard Edges is a field, so it can take the `sharp_edge` attribute or an Edge Angle comparison.
- **Attributes:** point, face and corner attributes such as UVs are sampled from the nearest surface. Edge
  attributes are lost because every edge is new.

![Suzanne as the original, remeshed, and remeshed with Hard Edges](https://github.com/demircanyigit/Weld-3D/releases/download/v5.1-custom-2/isotropic_remesh.png)

#### Decimate Mesh

Reduces triangles where it changes the surface the least, so flat areas lose detail before curved ones. The
result is triangles.

- **Target:** Ratio keeps a fraction of the selected vertices, and Face Count aims for a number of triangles.
- **Limits:** Max Error, Max Normal Change and Max Aspect Ratio protect quality and can stop it before the
  target. Set any of them to zero to turn it off.
- **What to keep:** Selection picks which vertices may be removed. Preserve Boundary and the Hard Edges field
  protect open edges and the edges you choose.

![Suzanne before and after Decimate Mesh](https://github.com/demircanyigit/Weld-3D/releases/download/v5.1-custom-2/decimate_mesh.png)

#### Voronoi Fracture

Cuts a closed mesh into solid pieces around the points you give it, with the cut faces filled in. More points
in an area make smaller pieces there.

- **Inputs:** Cell Points takes any geometry with points, Cut Gap leaves space between the pieces, and Separate
  Islands splits parts of a cell that aren't connected.
- **Outputs:** Piece Index numbers the pieces, for example for Split to Instances. Cell Index tells you which
  point made each piece. Interior selects the cut faces, which is how the orange material is set here.
- **Good to know:** the mesh has to be closed. The pieces stay in place until you move them, as in the picture
  on the right.

![A sphere, the same sphere cut into pieces, and the pieces moved apart showing orange inside faces](https://github.com/demircanyigit/Weld-3D/releases/download/v5.1-custom-2/voronoi_fracture.png)

#### Bevel

The Bevel modifier as a node, using the same code. It gave the same result as the modifier in every setting
compared. Unlike the modifier, Amount and Weight can differ per edge or vertex, for example from Random Value.

- **Inputs:** Affect picks Edges or Vertices. Width Type, Segments and Shape work as in the modifier. Selection
  picks what to bevel, and Angle skips edges flatter than a limit.
- **Geometry and Shading panels:** miters, Spread, Intersections, Clamp Overlap, Loop Slide, Harden Normals, Mark
  Seam, Mark Sharp and Material Index.
- **Output:** Bevel Faces selects the new faces, for example to give them their own material.
- **Good to know:** only edges with exactly two faces get beveled. There's no custom profile or Face Strength
  yet. Segments, Shape and Spread are one value for the whole mesh.

![A cube, a beveled cube and a cube with a random bevel on each edge](https://github.com/demircanyigit/Weld-3D/releases/download/v5.1-custom-2/bevel.png)

![Random Value connected to the Amount input of the Bevel node](https://github.com/demircanyigit/Weld-3D/releases/download/v5.1-custom-2/node_editor.png)

#### Sweep

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

![Three spirals swept with a round tube, a twisted square tube and a star](https://github.com/demircanyigit/Weld-3D/releases/download/v5.1-custom-2/sweep.png)

#### Resample Mesh

Adds points along edges without changing the shape or the number of faces, inspired by Houdini's Resample.

- **Modes:** Length sets the distance between points, and Count sets the segments on each edge. Both are fields, so
  they can differ per edge. Selection picks which edges change.
- **Output:** New Points selects the added points, for example to place instances on them.
- **Attributes:** point and corner attributes such as colors and UVs are interpolated onto the new points, and
  shared points stay welded.
- **Good to know:** edges stay straight, so curved surfaces don't get smoother. Faces become n-gons with extra
  points, so add Triangulate if you need triangles.

![A cube before and after Resample Mesh, with its points shown in orange](https://github.com/demircanyigit/Weld-3D/releases/download/v5.1-custom-2/resample_mesh.png)

#### Clip

Cuts a mesh with a plane and removes one side, inspired by Houdini's Clip. The cut can be filled, and rings keep
their hole.

- **Plane:** Origin, Direction and Distance set the plane. Space picks whether it moves with the object or stays
  in place in the world.
- **Keep:** Above, Below, or Both to only add the cut to the surface.
- **Outputs:** Interior selects the fill faces, like the orange ones here, and Cut Edges selects the edges along
  the cut, for example to bevel them.
- **Good to know:** only closed loops can be filled, so cutting an open mesh may leave parts of the cut open.

![A sphere, a cube and a torus cut at an angle with orange fill faces](https://github.com/demircanyigit/Weld-3D/releases/download/v5.1-custom-2/clip.png)

#### Dice

Cuts a mesh into a grid of pieces along its bounding box. Pick a count on X, Y and Z, or a cell size, and you're
done. It doesn't need points like Voronoi Fracture, and it also works on open meshes.

- **Grid:** Count X, Y and Z, or Size in Size mode. Space uses the bounding box and axes of the object or of the
  world, so the cuts can stay straight when the object rotates.
- **Options:** Cut Gap leaves space between the pieces, Fill Cuts makes them solid, and Separate Islands splits
  parts of a cell that aren't connected.
- **Outputs:** Piece Index numbers the pieces, for example for Split to Instances. Cell Index is the position in
  the grid. Interior selects the fill faces.
- **Good to know:** each mesh uses its own bounding box, so add Realize Instances in front to dice instances
  together. Pieces at the edge of a curved shape can be very small.

![A sphere cut into 2 x 2 x 2 and 4 x 4 x 4 pieces, and a torus cut into 6 x 6 x 1 pieces](https://github.com/demircanyigit/Weld-3D/releases/download/v5.1-custom-2/dice.png)

#### Dissolve

Removes parts of a mesh and merges the faces around them instead of leaving holes, using the same code as
Dissolve in Edit Mode.

- **Modes:** Vertices, Edges and Faces use the Selection field. Limited removes edges and points where the surface
  stays nearly flat, below an Angle.
- **Options:** Remove Inline Points cleans up points left between two edges. In Limited mode, the Keep panel stops
  merging across materials, seams, sharp edges or UVs.
- **Output:** Merged Faces selects the faces that were made, except in Vertices mode.
- **Good to know:** merged faces can be n-gons that aren't flat, which can shade unevenly.

![A cube going from 96 faces to 6 with Limited, and a sphere with random edges dissolved](https://github.com/demircanyigit/Weld-3D/releases/download/v5.1-custom-2/dissolve.png)

#### Smooth

Smooths a surface by moving points toward their neighbors. Topology and attributes stay the same.

- **Methods:** Average is strong, but the mesh shrinks with more iterations. Keep Volume mostly keeps the size, but
  only removes small bumps, so big shapes stay.
- **Control:** Factor and Selection are fields, so strength can differ per point. Preserve Boundary and the Hard
  Edges field keep those points in place.

![A lumpy sphere, smoothed with Average and with Keep Volume](https://github.com/demircanyigit/Weld-3D/releases/download/v5.1-custom-2/smooth.png)

#### Relax

Evens out the distances between points by sliding them along the surface, so the shape stays the same.

- **Control:** Factor and Selection are fields, Iterations sets the number of passes, and Keep on Surface moves
  points back onto the original surface after each pass.
- **What to keep:** Preserve Boundary and the Hard Edges field keep those points in place.
- **Good to know:** it doesn't change how points are connected, so uneven topology like the poles of a UV sphere
  stays uneven. Isotropic Remesh does that.

![A sphere with uneven points, and the same sphere with even points after Relax](https://github.com/demircanyigit/Weld-3D/releases/download/v5.1-custom-2/relax.png)

#### QuadriFlow Remesh

Rebuilds a mesh out of quads that follow the flow of its surface, using the same code as the QuadriFlow option of
the Remesh operator, but as a node you can keep changing.

- **Target:** Face Count, a Ratio of the input's faces, or an Edge Length. The result is only close to the target.
- **Options:** Seed changes the layout of the quads. Preserve Sharp and Preserve Boundary try to follow sharp
  features and open boundaries.
- **Attributes:** attributes and materials carry over, copied from the nearest part of the input.
- **Good to know:** the mesh must be manifold, or the node shows an error and leaves it as it is. Details smaller
  than the quads get crumpled or lost. It's slow and runs again when anything before it changes, so bake it or use a
  low target while working.

![Suzanne as the original and remeshed with QuadriFlow at two sizes](https://github.com/demircanyigit/Weld-3D/releases/download/v5.1-custom-2/quadriflow_remesh.png)

#### Displace

Moves points by a height or a vector, like the Displace modifier, but you connect any texture or field directly.
Without a texture it gives the same result as the modifier.

- **Modes:** Normal moves along each point's normal, Direction along a direction, and Vector by a vector, for
  example from the color of a texture.
- **Per point:** Height, Direction, Vector, Strength and Selection are fields. Midlevel sets the value that doesn't
  move a point, so 0.5 pushes points both ways.
- **Good to know:** the mesh needs enough points to show detail, so subdivide it first if needed. Direction and
  Vector are relative to the object.

![A sphere displaced along its normals and a plane displaced along a direction](https://github.com/demircanyigit/Weld-3D/releases/download/v5.1-custom-2/displace.png)

## Tools

New tools and tool changes outside the modifiers and nodes.

| | Tool | What it does |
|---|---|---|
| <img src="doc/fork/images/icons/tool_multi_cut.png" width="32" height="32" alt="Multi-Cut"> | [Multi-Cut](doc/fork/MULTI_CUT.md) | A toolbar tool in Edit Mode that cuts faces point by point, slices along a dragged line and inserts edge loops with Ctrl+click |
| <img src="doc/fork/images/icons/face_set_new.png" width="32" height="32" alt="New Face Set"> | [Face set tools](doc/fork/FACE_SETS.md#setting-them) | Face ▸ Face Sets in Edit Mode, also in the right-click menu: make, assign, merge, select, hide and clear face sets |
| <img src="doc/fork/images/icons/face_sets_from_loose_parts.png" width="32" height="32" alt="Face Sets From"> | [Face Sets From](doc/fork/FACE_SETS.md#face-sets-from) | Make face sets from loose parts, materials, seams, sharp edges or angles |
| <img src="doc/fork/images/icons/face_sets_to_materials.png" width="32" height="32" alt="Face Sets to Materials"> <img src="doc/fork/images/icons/face_sets_to_vertex_groups.png" width="32" height="32" alt="Face Sets to Vertex Groups"> | [To Materials, To Vertex Groups](doc/fork/FACE_SETS.md#setting-them) | Turn face sets into materials or vertex groups |
| <img src="doc/fork/images/icons/similar_face_set.png" width="32" height="32" alt="Select Similar Face Set"> | [Select Similar ▸ Face Set](doc/fork/FACE_SETS.md#select-similar) | Select the faces in the same face sets as the selected ones |
| <img src="doc/fork/images/icons/all_materials.png" width="32" height="32" alt="All Materials"> | [All Materials](doc/fork/ALL_MATERIALS.md) | A Properties tab with every material in the file, and giving a material to all selected objects |
| <img src="doc/fork/images/icons/gizmo.png" width="32" height="32" alt="Modifier Gizmos"> | [Modifier gizmos](doc/fork/MODIFIER_GIZMOS.md) | Handles in the viewport for the active modifier |
| <img src="doc/fork/images/icons/op_persp_ortho.png" width="32" height="32" alt="Orthographic"> | [Orthographic view](doc/fork/ORTHOGRAPHIC.md) | Orthographic stays orthographic when you rotate, and the grid stays on screen |

## Toolbox and Shelf

Two new editors hold buttons you pick yourself. Choose them from any area's Editor Type menu.

### <img src="doc/fork/images/icons/tool_settings.png" width="32" height="32" alt="Toolbox"> Toolbox

More on [the Toolbox page](doc/fork/TOOLBOX.md).

A column of large buttons in sections, with tabs down the right side.

- **Adding buttons:** right-click any button in Blender and choose **Add to Toolbox**.
- **Arranging:** add, rename, move and remove tabs, sections, separators and buttons from the right-click menu.
  Each button can have its own name and icon.
- **Two styles:** a grid of icons, or a list with one named button per row.
- **Default tabs:** it starts with the modifiers and modeling tools in tabs. **Restore Default Tab** and
  **Reset Toolbox to Default** bring them back.

<img src="doc/fork/images/ui_toolbox.png" alt="The Toolbox with its View tab open: navigation, viewpoint and layout buttons, and tabs down the side">

The contents are saved in `toolbox.json` in your user settings, so they're the same in every file.

### <img src="doc/fork/images/icons/shelf.png" width="32" height="32" alt="Shelf"> Shelf

More on [the Shelf page](doc/fork/SHELF.md).

A thin strip of icon buttons. It's an editor of its own, and also a strip at the top of the 3D Viewport that you
toggle with **Alt Y**.

- <img src="doc/fork/images/icons/op_add_to_shelf.png" width="32" height="32" alt="Add to Shelf"> <img src="doc/fork/images/icons/op_remove_from_shelf.png" width="32" height="32" alt="Remove from Shelf"> **Adding buttons:** right-click any button and choose **Add to Shelf**, or **Remove from Shelf**.
- **Icon size:** small, medium, large or a custom scale, in the shelf's header.
- **Starter presets:** Modeling, Modifiers and Objects.

![The shelf strip at the top of the 3D Viewport in Edit Mode, with the Main, Objects and Modeling tabs](doc/fork/images/ui_shelf.png)

The buttons are saved in your preferences, so every shelf shows the same ones.

### Quick Favorites with icons

Items in Quick Favorites (**Q**) show their icons. The icon is picked automatically when you add an item.

## Classic look

Weld 3D brings back the Blender 2.79 look:

- **Colored icons:** more than 1,000 new icons drawn in the 2.79 style. Every Geometry Node and every new modifier
  has one, and so do the tools and many operators, such as the Merge, Parent, Select Similar and import and
  export menus.
- **Emboss:** buttons, menus, tooltips, panel headers, tabs and nodes have a light top edge and a dark bottom
  edge. Each widget style in **Preferences ▸ Themes ▸ User Interface** has a **Bevel** setting for its strength.
- **Node headers:** a gradient from left to right.
- **Vertices:** square, like in 2.79.
- **Keyframes:** shaded as if lit from the top left.

More in [the classic look notes](doc/fork/CLASSIC_UI.md).

## Color attributes in Solid shading

Each material can combine its viewport color with a color attribute, so you can check vertex color variation
without leaving Solid mode. You'll find it under Material ▸ Viewport Display, with the viewport's Color set to
Material.

- **Options:** None, Mix, Multiply, Two Colors and Color Ramp. Mix isn't in the picture.
- **Settings:** Factor sets the strength. Two Colors and Color Ramp can read Brightness, Red, Green, Blue or Alpha.
- **Good to know:** it only changes the viewport, not renders, and only applies to meshes.

![Suzanne with the None, Multiply, Two Colors and Color Ramp options](https://github.com/demircanyigit/Weld-3D/releases/download/v5.1-custom-2/solid_colors.png)

## More

- [What to know before using it](doc/fork/README.md), with a list of every page
- [All node inputs and limits](doc/fork/NODES.md)
- [Material options](doc/fork/VIEWPORT_COLORS.md)
- [Checker in Solid shading](doc/fork/UV_CHECKER.md)
- [Island color in Solid shading](doc/fork/ISLAND_COLOR.md)
- [The classic look](doc/fork/CLASSIC_UI.md)
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
