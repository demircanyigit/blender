# All Materials

<img src="images/icons/all_materials.png" width="32" height="32" alt="All Materials">

A Properties tab, right under the World tab, that lists every material in the file, not only the ones
of the active object. Pick one in the list to edit it there, whether or not anything uses it. The
Material tab still shows the slots of the active object, as before.

## The list

Each row shows a ball in the material's Viewport Display color, its name, how many users it has,
and its fake user toggle. The ball is drawn, not rendered, so the list never waits on EEVEE or
Cycles, and it follows the color as you change it. The slot list of the Material tab (and the other
places that list an object's slots) shows the same balls; Grease Pencil materials keep their own
preview. You can rename a material in the list, and filter or sort the list from the arrow under it.

| Button | What it does |
|---|---|
| + | Adds a new material with the default shader |
| − | Deletes the material from the file. Slots that used it become empty. It asks first when something uses the material |
| Duplicate | Makes a copy of the material |

Nothing uses a material made here yet, so new materials and copies get a fake user. Without it,
saving the file would drop them. Turn the fake user off in the list if you don't want that.

Under the list are the same panels as in the Material tab: Preview, Surface, Volume, Settings,
Viewport Display, Line Art and the rest, for the picked material. They follow the render engine,
like in the Material tab. The slot list of the Material tab isn't shown here.

The picked material is stored with the Properties editor, so it stays picked after saving and
reopening. Pinning works as in the other tabs.

## Giving a material to all selected objects

In the Material tab, when other objects are selected besides the active one, a row of two buttons
appears under the material. They are also in the slot list's menu.

| Button | Icon | What it does |
|---|---|---|
| Assign to Selected | <img src="images/icons/material_to_selected.png" width="32" height="32" alt="Assign to Selected"> | Puts the active material in the active slot of every selected object. Objects without slots get one |
| Add Slot to Selected | <img src="images/icons/material_slot_to_selected.png" width="32" height="32" alt="Add Slot to Selected"> | Adds a new slot with the active material to every selected object that doesn't have it yet |

Both work on all objects that can have materials: meshes, curves, surfaces, text, metaballs,
hair curves, point clouds and volumes. Grease Pencil objects take Grease Pencil materials only, and the
others take the rest. Objects whose data is linked from another file are skipped. Slots follow the
**Link Materials To** preference, like the Add Material Slot button.

In Python, the operators are `object.material_to_selected(mode='ACTIVE_SLOT')` or
`mode='NEW_SLOT'`, and `material.browse_new()`, `material.browse_duplicate()` and
`material.browse_delete()` for the list. The picked material is
`SpaceProperties.browse_material`.
