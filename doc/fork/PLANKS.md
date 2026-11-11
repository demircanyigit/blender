# Planks

A modifier (`PLANKS`) under Add Modifier > Generate, and a **Planks** node under Mesh ▸ Operations.
**Type** picks what it makes:

- **Floor**: boards laid on the faces of the mesh.
- **Wall**: boards on the faces as siding.
- **Crate**: a box of boards, with its bottom at the origin.
- **Barrel**: a barrel of staves, with its bottom at the origin.

Floor and Wall fill connected faces that lie in one plane together. Boards run across the edges
between those faces and are cut square where the faces end, so holes in the mesh (windows, doors)
become clean openings. Crate and Barrel ignore the input mesh.

## Boards

**Width**, **Thickness** and **Gap** are shared by every type. Boards have square edges.

## Floor

- **Pattern**: Straight, Herringbone, Chevron or Basket.
- **Length**, **Random Length**, **Shortest Piece** (no piece at the end of a row is shorter) and
  **Joints** (Random, Half or Third) for Straight.
- **Angle** turns the rows.
- **Border**: a ring of boards along the outline. Only on faces with one convex outline; otherwise
  a warning is shown.
- **Joists** under the boards, with spacing, width and depth.

## Wall

- **Style**: Horizontal, Vertical, Lap (each board lies over the top of the one below, by **Lap**)
  or Board and Batten (upright boards with strips of **Batten Width** over the joints).
- **Studs** behind the boards, across their direction.

## Crate

**Size**, **Slatted** with **Slat Gap**, **Frame** (boards along the edges of each side),
**Brace** (None, Diagonal, or Z: boards along the top and bottom with a diagonal between them),
**Corner Posts** with **Post Width**, and **Lid** (Closed, Open with **Lid Angle**, or None).

## Barrel

**Height**, **Radius** at the ends, **Bulge** of the middle, **Staves**, **Ends** (Both, Bottom or
None), **Chime** (how far the staves reach past the end boards) and **Hoops** with width,
thickness and offset from the ends.

## Variation

**Random Height**, **Tilt**, **Cup** (long edges rise), **Bow** (middle rises), **Missing** and
**Seed**. Barrels use Random Height and Missing.

## Output

- Each board has its own UV island along its length, with a random offset and flip, in the active
  UV map.
- Boards pick a material at random from **Material Count** slots starting at **Material Index**
  (-1 keeps the material of the face). Hoops use **Metal Material Index**.
- Attributes: `plank_id` (point, the board), `plank_random` (point, 0 to 1 per board) and
  `plank_part` (face: 0 board, 1 joist, stud or frame, 2 hoop, -1 for the input). The node gives
  them as the Board Index, Random and Part fields.
- **Keep Surface** keeps the faces under the boards.
