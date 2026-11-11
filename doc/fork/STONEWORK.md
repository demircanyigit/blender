# Stonework

A modifier (`STONEWORK`) under Add Modifier > Generate, and a **Stonework** node under Mesh ▸
Operations. It fills a mesh with whole stones, stacked one at a time from the bottom up, like a
dry-stone wall. The result is one mesh of stones.

## How it stacks

Each step tries a few stones, each turned along and across and upside down, everywhere on the
current top. It keeps the one that lands lowest, leaves the fewest gaps under it, sits level with
its neighbors, and has its weight over what holds it up. Then it drops that stone in place.

When no stone fits a low pocket, the stacking goes on over it. With **Filler Stones** on, smaller
stones fill the pockets and the top at the end, in two rounds.

## Settings

| Setting | Meaning |
|---|---|
| Fill | **Volume**: the inside of the mesh. **Box**: its bounding box. **Surface**: one layer of stones just under the surface |
| Source | **Built-in** stones, or your own from a **Collection** (the node's **Own Stones** input). Each loose part is one stone |
| Shape | Round, Angular, Blocks, Slabs, Wedges, Mixed or Dry Wall (blocks and slabs laid flat, a few standing stones, wedges in the gaps) |
| Stone Size | About half the width of a built-in stone |
| Size Variation | How much the stones differ in size |
| Flatness | Chunky stones at 0, flat ones at 1 |
| Roughness | Bumps on the built-in stones |
| Detail | How many faces each built-in stone has |
| Scale Min, Max | A random size for each finished stone, around its center. Below 1 opens gaps, above 1 lets stones overlap |
| Gap | Space between stones |
| Close Gaps | Grow the stones into the empty space around them, until they meet their neighbors at the Gap. Each stone grows by its six sides, so its faces stay flat. 0 keeps their own shapes |
| Poke Out | How far stones may stick out past the surface. Zero keeps them inside |
| Filler Stones | Fill the holes left at the end with smaller stones |
| Max Stones | Stop after this many stones |
| Seed | Random seed |
| Mortar | Add mortar: the filled shape, pulled in by **Depth**, so the stones stand out of it |
| Remesh | How the mortar is made dense: **Isotropic** (even triangles), **Voxel** (a new closed surface), **Divide** (the faces cut by a grid) or **None**. **Resolution** is the edge length, voxel size or grid size |
| Merge | Join the stones and the mortar into one closed surface, like a boolean union. Stones buried in the mortar disappear |
| Material Index, Mortar Material | Material slots of the stones and the mortar |
| Random Color | Store a random color per stone in the `stone_color` color attribute. The mortar is gray |
| Color Mode | **Hue**: random hues between **Hue Min** and **Max**, saturation between **Saturation Min** and **Max**. **Gray**: shades of gray. Both use **Value Min** and **Max** for brightness |

Your own stones keep their objects' rotation and scale. Size Variation still applies to them.

## Output

Each face has a `stone_id` and a `stone_random` (0 to 1) attribute, per stone. The mortar's
faces have a `stone_id` of -1. The node has **Stone ID**, **Random**, **Color** and **Mortar**
fields instead, and its material is set with Set Material and the Mortar field. The modifier
panel shows how many stones it placed.

## Limits

- The stacking works on a grid of cells a fifth of the Stone Size, up to 6 million cells. Larger
  meshes get coarser cells.
- A stone can only drop straight down, so it never slides under an overhang. Arches and
  undercuts stay empty.
- The mesh should be closed for Volume and Surface.
- Merge with the mortar's Remesh at None cuts its big faces into thin slivers that shade badly.
  Keep a remesh on when merging.
- Merge needs closed stones. If one of your own stones is open, the parts are kept side by side
  and the panel says so.
- A wall of a few hundred stones takes about a second.
