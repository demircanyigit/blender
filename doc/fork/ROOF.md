# Roof

A modifier (`ROOF`) under Add Modifier > Generate, and a **Roof** node under Mesh ▸ Operations.

## Modes

- **Build**: each group of connected faces facing up is the outline of a building and gets a
  roof. **Shape** is Gable, Hip, Shed, Flat, Pyramid, Gambrel or Mansard.
  - **Pitch** sets the slope. Gambrel and Mansard also have **Lower Pitch** and **Break** (how much
    of the width the steep lower part covers).
  - **Auto Ridge** runs the ridge along the longest side; otherwise **Ridge Angle** sets it.
  - Flat roofs follow the outline and get low walls (**Parapet Height** and **Width**).
  - Pyramid follows convex outlines.
  - The other shapes are built on the rectangle around the outline. Outlines that are not
    rectangles get a warning.
  - Gable and Gambrel ends, and the high side of a Shed, get wall faces.
- **Cover**: the faces are already a roof, of any shape. Faces steeper than 80 degrees are walls
  and are left out. Faces flatter than **Flat Angle** get a sheet if **Flat Roofing** is on,
  otherwise they are left out.

## Shared steps

1. **Overhangs**: open edges that face down the slope are eaves and reach out by **Eave
   Overhang**, measured level. Other open edges reach out by **Rake Overhang**. Outer corners
   are filled.
2. **Slab**: the roof gets **Thickness** under it. In Cover mode only the overhangs do, since the
   modeled faces are kept as they are.
3. **Covering**: Tiles, Shingles, Metal or Planks, laid in rows level across each slope from the
   eave up, and cut where the slope ends. **Row Spacing** is how much of each row shows, **Tile
   Width** the width of a tile (or of a metal sheet), **Overlap** how much each row covers the one
   below. Random Height, Tilt, Missing and Seed vary the tiles.
4. **Trim**:
   - **Ridge Caps** on edges that fold outward (ridges and hips).
   - **Valleys** strips in edges that fold inward. Edges count when they fold more than **Fold
     Angle**.
   - **Fascia** boards along the outer edges.
   - **Gutters** along level eaves.

## Output

- **Materials**: Roof (the slab; -1 keeps the face's material), Covering, Trim (caps, valleys,
  fascia, gutters) and Walls (gable walls, parapets).
- **Attributes**: `roof_part` (face: 0 slope, 1 underside, 2 fascia, 3 covering, 4 ridge, 5 valley,
  6 gutter, 7 wall, 8 parapet, -1 for the input) and `roof_random` (point, 0 to 1 per tile). The
  node gives them as the Part and Random fields.
- **UVs**: the slab's UVs run down the slope, and each tile has its own island.
- The input mesh is kept.
