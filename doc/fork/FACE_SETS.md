# Face sets outside Sculpt Mode

Face sets are groups of faces: every face has a number, and faces with the same number are one
set. Sculpt Mode uses them to hide, mask and protect parts of a mesh, and the
[Piece ID](PIECE_ID.md) modifier can make one piece per set. This fork lets you see and set them in
Object and Edit Mode too. They are stored in the mesh's `.sculpt_face_set` face attribute, the same
one Sculpt Mode uses, so all of these see the same sets.

## Seeing them

In the 3D viewport's Shading popover, use **Solid** and set Object Color to **Face Set**. Every face
shows the color of its set, the same colors as in Sculpt Mode. Faces in the default set are white.

It works in Object Mode and Edit Mode, so you can see the sets while selecting faces. To draw a
line between sets, set the outline to **Face Set**, see [SOLID_OUTLINES.md](SOLID_OUTLINES.md). Objects that
aren't meshes show their material color. In Sculpt Mode the mesh shows its material color too,
because Sculpt Mode shows the sets with its own overlay.

Next to it, **[Island](ISLAND_COLOR.md)** gives each loose part its own color.

## Setting them

In Edit Mode, Face > Face Sets. The same menu is in the right-click menu in Face select mode.

| | Tool | What it does |
|---|---|---|
| <img src="images/icons/face_set_new.png" width="32" height="32" alt="New Face Set"> | New Face Set | Puts the selected faces in a new set, one more than the highest set of the mesh |
| <img src="images/icons/face_set_assign.png" width="32" height="32" alt="Assign to Face Set"> | Assign to Face Set | Puts the selected faces in the set with the number you give. It starts from the set of the active face |
| <img src="images/icons/face_set_split_islands.png" width="32" height="32" alt="Split Selection by Islands"> | Split Selection by Islands | Each connected part of the selection gets its own new set |
| <img src="images/icons/face_set_merge.png" width="32" height="32" alt="Merge Face Sets"> | Merge Face Sets | The whole sets of the selected faces become one set: the set of the active face |
| <img src="images/icons/face_set_clear.png" width="32" height="32" alt="Clear Face Set"> | Clear Face Set | Puts the selected faces back in the default set |
| <img src="images/icons/face_set_select.png" width="32" height="32" alt="Select Face Set"> | Select Face Set | Selects every face in the same sets as the selected faces |
| <img src="images/icons/face_set_select_boundary.png" width="32" height="32" alt="Select Face Set Boundaries"> | Select Face Set Boundaries | Selects the edges where two sets meet, in Edge select mode. You can then Mark Seam or Mark Sharp on them |
| <img src="images/icons/face_set_select_more.png" width="32" height="32" alt="Grow by Face Set"> | Grow by Face Set | Adds the whole sets next to the selection |
| <img src="images/icons/face_set_select_less.png" width="32" height="32" alt="Shrink by Face Set"> | Shrink by Face Set | Removes the whole sets at the border of the selection |
| <img src="images/icons/face_set_hide_others.png" width="32" height="32" alt="Hide Other Face Sets"> | Hide Other Face Sets | Hides every face that isn't in the sets of the selection. Reveal Hidden brings them back |
| <img src="images/icons/face_set_randomize_colors.png" width="32" height="32" alt="Randomize Colors"> | Randomize Colors | New colors when two sets next to each other look too alike. The sets stay the same |
| <img src="images/icons/face_sets_to_vertex_groups.png" width="32" height="32" alt="To Vertex Groups"> | To Vertex Groups | One vertex group per set, named "Face Set 2" and so on. Vertices where sets meet are in both groups |
| <img src="images/icons/face_sets_to_materials.png" width="32" height="32" alt="To Materials"> | To Materials | Each set gets a new material in a new slot, colored like the set |

### Face Sets From

Face > Face Sets > Face Sets From makes sets from what the mesh already has. It replaces the sets of
the whole mesh, starting from the set after the default one. With **Selected Only** in the Adjust
Last Operation panel it changes only the selected faces, and the new sets start above the sets
of the other faces.

| | From | What it does |
|---|---|---|
| <img src="images/icons/face_sets_from_loose_parts.png" width="32" height="32" alt="Loose Parts"> | Loose Parts | One set per connected part |
| <img src="images/icons/face_sets_from_materials.png" width="32" height="32" alt="Materials"> | Materials | One set per material slot |
| <img src="images/icons/face_sets_from_seams.png" width="32" height="32" alt="UV Seams"> | UV Seams | A new set wherever a seam cuts |
| <img src="images/icons/face_sets_from_sharp.png" width="32" height="32" alt="Sharp Edges"> | Sharp Edges | A new set wherever an edge is marked sharp |
| <img src="images/icons/face_sets_from_angle.png" width="32" height="32" alt="Angle"> | Angle | A new set wherever faces meet at a sharper angle than the one you give |

The tools work on every mesh in Edit Mode at once. A mesh without face sets gets them the first
time you assign one, with every other face in the default set.

### Select Similar

Select > Select Similar has **Face Set** <img src="images/icons/similar_face_set.png" width="32" height="32" alt="Face Set"> in Face select mode. It
selects every face in the same sets as the selected faces, on all meshes in Edit Mode.

### Separate

Mesh > Separate (P) has **By Face Sets** <img src="images/icons/op_separate_face_sets.png" width="32" height="32" alt="By Face Sets">. Each face set
becomes its own object, like By Material does for materials. The last face set stays in the
original object. It works in Edit Mode and Object Mode. A mesh with one face set or none is left
as it is.

### Piece ID

When the Piece ID modifier's **From** is **Face Sets**, its panel shows **New**, **Select**,
**Clear** and **Merge** buttons, and a **From** menu with the Face Sets From choices. They work in
Edit Mode.

## In Python

```python
bpy.ops.mesh.face_set_new()
bpy.ops.mesh.face_set_assign(face_set=3)
bpy.ops.mesh.face_sets_from(mode='LOOSE_PARTS', selected_only=False)  # 'MATERIALS', 'SEAMS', 'SHARP', 'ANGLE'
bpy.ops.mesh.face_set_merge()
bpy.ops.mesh.face_set_clear()
bpy.ops.mesh.face_set_select()
bpy.ops.mesh.face_set_select_boundary()
bpy.ops.mesh.face_set_select_more()
bpy.ops.mesh.face_set_select_less()
bpy.ops.mesh.face_set_hide_others()
bpy.ops.mesh.face_set_randomize_colors()
bpy.ops.mesh.face_sets_to_vertex_groups()
bpy.ops.mesh.face_sets_to_materials()
bpy.ops.mesh.select_similar(type='FACE_SET')
bpy.ops.mesh.separate(type='FACE_SETS')
```
