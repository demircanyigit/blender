# Piece ID

Native modifier (`PIECE_ID`, Python type `PieceIdModifier`) under Add Modifier > Edit. It is in the
Data column of the Modifier List menu.

Piece ID writes a whole number per face or point that says which piece it belongs to. The mesh is
not changed. Modifiers further down the stack read the number: **Collapse** with **Pieces** set to
**Attribute** drops every piece as one rigid body, and shaders can read it through an Attribute node.

## Controls

- **Name:** the attribute that is written. The default is `piece`, which is also Collapse's default.
- **Domain:** one number per **Face** or per **Point**.
- **From:** where the pieces come from:
  - **Islands:** one piece per loose part.
  - **Split Edges:** faces joined by edges are one piece, split at **Seams** and/or **Sharp** edges.
    A connected mesh can be broken into parts this way.
  - **Material:** one piece per material slot.
  - **Face Sets:** one piece per sculpt face set. The panel then shows **New**, **Select**,
    **Clear** and **Merge** buttons and a **From** menu to set them in Edit Mode, and the
    **Face Set** color in Solid shading shows them. See [FACE_SETS.md](FACE_SETS.md).
  - **Vertex Group:** in the group or not, from **Threshold**. A face uses the average weight of
    its points. **Invert** swaps the two.
  - **Attribute:** one piece per value of another attribute, such as Stonework's
    `.stonework_stone`, Distribute's `distribute_id` or Join's `object_id`. Point attributes are
    read per point, the rest per face.
  - **Selection:** the faces selected in Edit Mode, and the rest.
- **Combine:** keeps the pieces already in the attribute and splits them further. For example, one
  Piece ID from Material and a second one from Islands with Combine gives each loose part of each
  material its own number.
- **Random:** also writes a value from 0 to 1 per piece, to the same name with `_random` added,
  changed by **Seed**.

The numbers always start at 0 and follow the order the pieces first appear. The panel shows how
many pieces there were in the last evaluation.

When a point has to take a number from faces, it takes the number of its first face. When a face
has to take a number from points, it takes the number of its first point.

## By object and by parent

**Join** writes two face attributes when **Piece IDs** is on (the default for new Join modifiers):

- `object_id`: 0 for the receiving mesh, then 1, 2, 3 ... for each source object in order.
- `parent_id`: the same number for every object under the same top parent.

Join, then Collapse with **Pieces** set to **Attribute** and the name `object_id` or `parent_id`,
drops each object, or each parent with its children, as one piece. Piece ID with **From** set to
**Attribute** can combine those with other splits.

Source integration only: no build, runtime execution or tests were run for this addition.
