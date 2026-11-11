# Carve

A modifier (`CARVE`) under Add Modifier > Deform, and a **Carve** node under Mesh ▸ Operations.
It moves points by a grayscale height. The height comes from two layers that are combined and
then masked. It can also set materials by layer and mask. For more material rules, use
Materialize.

## Layers

**Layer A** and **Layer B** each have a **Source**:

- **Texture**: a texture datablock (image, noise and so on), read at the point. Modifier only.
- **Pattern**: a built-in pattern:
  - **Noise**: smooth bumps with **Detail** and **Roughness**.
  - **Voronoi**: cracked cells, like flagstones.
  - **Waves**: bands, wobbled by **Distortion**.
  - Walls. Each stone is its own rounded shape with a slightly domed face and its own depth, and
    the mortar is uneven along the joints.
    - In courses: **Random Stone** (rounded stones of mixed sizes), **Coursed Blocks** (squared
      blocks), **Slate** (thin, long stones), **Dry Stone** (thin stones stacked tight) and
      **Banded** (small stones with a course of long stones every few courses). Course lines
      wander, stones sit on stepped beds and tilt a little, some are tall and span two courses,
      some are split by a crack, and joints never line up from one course to the next.
      **Stone Length**, **Row Height**, **Mortar**, **Roundness** and **Variation** shape them.
    - Without courses: **Fieldstone** (round stones, wide joints), **Rubble** (large, irregular
      stones) and **Cobblestone** (rounded stones of about the same size). Outlines are lumpy,
      not straight-sided.
    - **Ashlar**: square and long blocks of mixed sizes, fitted together.
    - These use **Stone Size**, **Mortar**, **Roundness** and **Variation**.
    - **Bricks**: even rows of bricks, every other row shifted by half.
  - Fitted walls start from rows of bricks whose corners are shaken. Neighboring stones share
    their corners, so they always fit each other. Big stones are broken into pieces. Corners wear
    away more than straight sides, the mortar is thin where stones press together and opens up at
    corners, and outlines are chipped. **Fitted Rubble**, **Rough Courses**, **Squared Stone**,
    **Mixed Rubble** (big stones broken into pieces of mixed sizes), **Worn Stone** and **Thin
    Courses**. They use **Stone Length**, **Row Height**, **Mortar**, **Roundness** and
    **Variation**.
  - Packed walls are built one stone at a time, like a mason would. Each stone is tried in a few
    poses and dropped where it lands lowest, rests without tipping, leaves the fewest gaps and
    lines up with its neighbors. The holes left at the end get small filler stones. **Round
    Stones**, **Angular Stones**, **Blocks**, **Slabs**, **Wedges**, **Mixed Stones** and **Dry
    Wall** (blocks and slabs laid flat, a few standing stones, wedges in the gaps). They use
    **Stone Size**, **Mortar**, **Roundness** and **Variation**.
  - Fitted and packed walls are laid out once and then repeat every 15 to 40 stones, with no
    seam. Changing Stone Size, Row Height, Variation or the seed lays them out again, which takes
    a second or two for packed walls. Mortar, Roundness and Roughness change at once.
    - **Roughness** adds a little grain to every stone face.
- **Attribute** (modifier) or **Value** (node): a grayscale value per point, for example a color
  attribute or the output of a texture node.

Patterns and textures read their coordinates from **Local**, **Global**, another **Object**, or a
**UV** map (modifier), or from the **Vector** input (node). Wall patterns stand upright on faces
facing any way, unless the coordinates are flat (UV, or **Flat** on the node).

Each layer then has **Black** and **White** (values below Black become 0, above White 1),
**Contrast**, **Invert** and **Strength**.

## Combine

**Blend** puts Layer B onto Layer A: Mix, Add, Subtract, Multiply, Lighter, Darker or
Difference, with **Blend Factor**.

## Masks

The masks multiply together:

- **Noise Mask**: carve only in patches of noise (**Mask Scale**, **Detail**, **Threshold**,
  **Softness**, **Invert**).
- **Vertex Group** and a **Mask Attribute** (modifier), or **Selection** and **Factor** (node).
- **Curvature**: only on edges (Convex), in creases (Concave) or both, with **Threshold** and
  **Softness**.
- **Facing**: only on surfaces facing the **Top**, the **Sides** or the **Bottom**, within
  **Facing Angle**. Any of them can be on together. With Top and Sides on, everything but the
  underside is carved. **Facing Softness** fades the edge of the mask smoothly. With none on,
  facing does not mask.

## Materials

Each layer can set **High** and **Low** materials: faces where the layer is above **Split** get
High, the others Low. With **Masked** on, a layer sets materials only where the mask is on.
**Mask Material** goes on faces where the mask is above **Threshold**. Layer A goes over the mask
material, and Layer B over both. Leave a material empty to keep the face's own material.
Materials are added to the mesh if it does not have them.

## Moving the points

Points move by (height − **Midlevel**) × **Strength** × mask, along the **Direction** (Normal, X, Y,
Z or Custom). **Max Depth** limits how far they move (0 for no limit), and **Smooth** smooths the
movement. **Seed** changes the patterns.

The modifier's **Subdivide** splits the faces first (simple subdivision, up to 6 levels), so a
low-poly wall has enough points to show the stones.

## Output

- Point attributes `carve_height`, `carve_mask` and `carve_curvature` (modifier). The node gives
  them as the Height, Mask and Curvature fields.
- The look depends on how many points there are. Few points give a blurry pattern.
