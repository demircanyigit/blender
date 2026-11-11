# Exploded View

<img src="images/icons/mod_exploded_view.png" width="32" height="32" alt="Exploded View">

Pushes faces, islands or other elements apart, with a **Factor** you can animate from 0 to 1. Use it
for exploded views of products and machines, for breaking things apart with
[Voronoi Fracture](VORONOI_FRACTURE.md), or for motion graphics.

![Voronoi Fracture with Exploded View, Poly Select with four Poly Edits, Panelize and Radial Array](images/modifiers_modeling.png)

*On the left: Voronoi Fracture with Exploded View.*

Add it from **Add Modifier ▸ Deform**. It works on meshes. In the [Modifier List](MODIFIER_LIST.md)'s Add menu it's in the **Elements** column.

## Settings

- **Element**: What moves as one piece.
  - **Face**: Each face on its own.
  - **Island**: Each connected piece.
  - **UV Island**: Each UV island.
  - **Material**: All faces with the same material.
  - **Face Set**: Each face set from sculpt mode.
- **UV Map**: UV map for UV islands. The active one when empty.
- **Direction**: Which way the elements move.
  - **From Center**: Away from the center point.
  - **Normal**: Along the normal of each element.
- **Center**: Point the elements move away from, in object space.
- **Factor**: How far the explosion has gone. Animate it from 0 to 1.
- **Distance**: How far the elements move at a factor of 1.
- **Randomness**: How much the distance varies between elements.
- **Tumble**: Largest random rotation of each element at a factor of 1.
- **Seed**: Change it for a different random result.
- **Vertex Group**: Vertex group that controls how much each point is affected.

## As a node

Geometry Nodes has a **Exploded View** node that shares its code with this modifier, so a node tree and the modifier give the same result.

## In Python

```python
mod = obj.modifiers.new("Exploded View", 'EXPLODED_VIEW')
```

The Python type is `ExplodedViewModifier`. Official Blender doesn't know this modifier, so a file that uses it won't give the same result there.
