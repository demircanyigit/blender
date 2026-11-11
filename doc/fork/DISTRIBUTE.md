# Distribute

A modifier (`DISTRIBUTE`) under Add Modifier > Generate, and a **Distribute** node under Mesh ▸
Operations. It packs objects from a collection on the faces of a mesh, by their measured sides, so
none of them touch. Then it can fill the gaps with smaller objects and turn the surface into bumpy
mortar between them.

## How it packs

Each object is measured by its bounding box, turned and scaled as the object is. Objects are placed
one at a time. For each one, a few spots are tried, most of them right next to objects already
placed and the rest anywhere on the faces. A spot counts only when the object's box is at least
**Gap** away from every other box. Of the spots that count, the one closest to its neighbors wins,
so the objects pack tightly.

When no spot is left, the gaps are filled: with the **Fillers** collection if it has objects, or
else with copies of the objects at **Filler Scale**, then at that scale again.

## Settings

| Setting | Meaning |
|---|---|
| Collection | The objects to place. Each object is one item, measured as it is turned and scaled |
| Fillers | Small objects for the gaps at the end. Empty uses smaller copies of the objects |
| Vertex Group | Only the faces whose points are all in the group. The node has a **Selection** input instead |
| Gap | Space between the objects |
| Tries | How many spots are tried for each object. More packs tighter and takes longer |
| Stay Inside | Objects must not hang over the edges of the surface |
| Fill Gaps | Fill the gaps at the end |
| Filler Scale | Size of the smaller copies, when there are no fillers |
| Align to Normal | Objects stand on the faces. Off keeps them upright |
| Direction | Which way the objects line up. **Auto** is level on walls and along X on floors |
| Rotation | **Aligned**, turned in **Steps** of 90 degrees, or **Random** |
| Jitter | Largest random turn on top of that |
| Scale Min, Max | A random size for each object |
| Offset | Distance from the surface to the bottom of each object. Negative sinks them in |
| Mortar | Replace the surface with mortar: a dense copy, moved by **Offset** along the normals, with bumps |
| Remesh | How the mortar is made dense: Isotropic, Voxel, Divide or None |
| Resolution | Edge length, voxel size or grid size of the mortar |
| Noise Strength, Scale, Detail | Height, size and small bumps of the mortar's noise |
| Mortar Material | Material slot of the mortar |
| Keep Surface | Keep the surface under the objects, when there is no mortar |
| Max Count | Stop after this many objects |
| Realize | Make the objects real geometry instead of instances |
| Random Color | A random color per object, in the **Hue** or **Gray** mode, with min and max ranges |

## Output

The objects are instances, unless **Realize** is on. Each one has these attributes on the instance
domain: `distribute_id` (its number), `distribute_random` (0 to 1) and, with Random Color,
`random_color`. With Realize on, `random_color` is the mesh's active color.

The node outputs **ID**, **Random**, **Color** and **Filler** as fields. Its **Instances** input
takes instances: each top-level instance is one item, for example from a Collection Info node with
**Separate Children** on. Other geometry in that input is one more item.

## Limits

- Objects are packed by their bounding boxes, not their shapes. Round objects keep the gaps of
  their boxes.
- The packing is random and greedy, not perfect. More **Tries** packs tighter.
- On curved surfaces, objects sit on the face under their center, so wide objects can float over
  or cut into the curve.
