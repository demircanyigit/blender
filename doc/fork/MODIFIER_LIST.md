# Modifier List

<img src="images/icons/modlist_view_list.png" width="32" height="32" alt="List"> <img src="images/icons/modlist_view_stack.png" width="32" height="32" alt="Stack">

The Modifiers tab has a second way to show the stack: a **List** with one row per modifier, and the
settings of only the active modifier under it. Long stacks stay short, and you can see the whole
stack at a glance.

![The Modifier List: the stack as a list, with the settings of the active Poly Edit under it](images/ui_modifier_list.png)

## Switching

The two buttons in the header of the Modifiers tab switch between the usual **Stack** of panels and
the **List**. The choice is kept per Properties editor.

## The list

- Each row has the modifier's icon and name, and its switches: Edit Mode, On Cage, viewport and
  render. A switch that is off is faded, and a modifier that is off in the viewport and in renders
  is faded as a whole.
- Double-click a name to rename it.
- The row with the mesh's name is the base mesh, at the start of the stack. Click it to go in and
  out of Edit Mode.

## The buttons under the list

Two rows of buttons work on the active modifier and the list:

| | Button | What it does |
|---|---|---|
| <img src="images/icons/modlist_apply.png" width="32" height="32" alt="Apply"> | Apply | Applies the active modifier |
| | Duplicate | Makes a copy of the active modifier |
| <img src="images/icons/modlist_copy_to_selected.png" width="32" height="32" alt="Copy to Selected"> | Copy to Selected | Copies the active modifier to the selected objects |
| | Pin to Last | Keeps the modifier at the end of the stack |
| <img src="images/icons/modlist_end_result.png" width="32" height="32" alt="End Result"> | Show End Result | When it's off, the viewport shows the stack only up to the active modifier, so you can see what each step does without turning the others off |
| <img src="images/icons/gizmo.png" width="32" height="32" alt="Modifier Gizmos"> | Modifier Gizmos | Shows or hides the [modifier gizmos](MODIFIER_GIZMOS.md) in the 3D Viewports |
| <img src="images/icons/modlist_collapse_to.png" width="32" height="32" alt="Collapse To"> | Collapse To | Applies every modifier up to the active one |
| | Move Up, Move Down, Delete | Move or delete the active modifier |

## Right-click menu

Right-click a row for: Collapse To, Apply, Duplicate, Copy and Paste, Copy to Selected, Enable or
Disable, Rename and Delete.

| | |
|---|---|
| <img src="images/icons/modlist_copy.png" width="32" height="32" alt="Copy"> <img src="images/icons/modlist_paste.png" width="32" height="32" alt="Paste"> | **Copy** and **Paste** a modifier between objects, with all its settings |
| <img src="images/icons/modlist_rename.png" width="32" height="32" alt="Rename"> | **Rename** the modifier |

## Options menu

The menu button in the header has:

| | |
|---|---|
| <img src="images/icons/modlist_first_on_top.png" width="32" height="32" alt="First on Top"> <img src="images/icons/modlist_last_on_top.png" width="32" height="32" alt="Last on Top"> | **First on Top** or **Last on Top**: the order of the list |
| <img src="images/icons/modlist_enable_all.png" width="32" height="32" alt="Enable All"> <img src="images/icons/modlist_disable_all.png" width="32" height="32" alt="Disable All"> | **Enable All** and **Disable All** in the viewport |
| <img src="images/icons/modlist_apply_all.png" width="32" height="32" alt="Apply All"> | **Apply All** modifiers |
| <img src="images/icons/modlist_delete_all.png" width="32" height="32" alt="Delete All"> | **Delete All** modifiers |
| <img src="images/icons/modlist_copy_stack.png" width="32" height="32" alt="Copy Stack"> | **Copy Stack to Selected**: give the selected objects the whole stack |
| <img src="images/icons/modlist_favorites.png" width="32" height="32" alt="Favourites"> | **Favourites...**: see below |

## Adding modifiers

**Add Modifier** above the list opens every mesh modifier in one menu of columns: Edit, Build,
Topology, Elements, Deform and Shape on top, and Smooth, Normals, UV, Data, Select and Physics
under them. The search button next to it finds a modifier by name.

![The Add menu of the Modifier List](images/ui_modifier_add_menu.png)

## Favourites

Your favourite modifiers are buttons above **Add Modifier**: click one to add that modifier. The
Favourites button next to the search button, or **Favourites...** in the options menu, sets them up:
click a modifier to add it or remove it, move them up and down, and choose one to three columns.
**Reset to Default** brings back the starting set.

Favourites are saved in your user settings, so every file has them.

## In Python

```python
space = bpy.context.space_data        # a Properties editor
space.use_modifier_list = True        # False for the Stack
```
