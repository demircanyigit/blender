# Collapse

<img src="images/icons/mod_collapse.png" width="32" height="32" alt="Collapse">

Drops pieces as rigid bodies and shows where they come to rest, without a rigid body simulation.
The pieces are the loose parts of the mesh, or groups of faces with the same number in an
attribute, such as one written by [Piece ID](PIECE_ID.md). They land on ground objects and
collections, or on a flat floor under the lowest point.

Combine it with [Voronoi Fracture](VORONOI_FRACTURE.md), [Dice](DICE.md) or [Shatter](SHATTER.md)
for rubble, or use it to pile up loose objects.

Add it from **Add Modifier ▸ Generate**. It works on meshes. In the [Modifier List](MODIFIER_LIST.md)'s Add menu it's in the **Edit** column.

## Ground

Set **Ground** to the mesh the pieces land on. Under it is a list for more: **Add Object** and
**Add Collection** add an entry, **Add Selected Objects** adds every selected mesh object, and each
entry can be turned off or removed. Without any, the pieces land on a flat floor under the lowest
point. **Cage** pushes the ground out along its normals, so the pieces land further away, or pulls
it in when negative.

## Settings

- **Pieces**: What falls together as one rigid piece.
  - **Islands**: Every loose part falls as one piece.
  - **Attribute**: Faces or points with the same number in an attribute fall as one piece.
- **Attribute**: Whole number per face or point saying which piece it belongs to.
- **Ground**: Mesh the pieces land on. Without one, they land on a flat floor under the lowest point.
- **Keep**: Pieces with points in this group stay where they are.
- **Time**: Seconds the pieces fall for. Longer lets them settle more.
- **Gravity**: How hard the pieces are pulled down.
- **Push**: Random start speed and spin of each piece, so stacks tip over.
- **Seed**: Changes the random push.
- **Friction**: How much the pieces resist sliding.
- **Bounce**: How much the pieces bounce when they hit something.
- **Quality**: Solver steps per 1/60 second. Higher keeps small or fast pieces from passing through.

## Good to know

- Long **Time** values and many pieces take longer to work out. Turn the modifier off in the
  viewport while you change things below it.
- Pieces in the **Keep** vertex group stay where they are, and the others land on them.

## As a node

Geometry Nodes has a **Collapse Islands** node too, see [NODES.md](NODES.md#collapse-islands).

## In Python

```python
mod = obj.modifiers.new("Collapse", 'COLLAPSE')
```

The Python type is `CollapseModifier`. Official Blender doesn't know this modifier, so a file that uses it won't give the same result there.
