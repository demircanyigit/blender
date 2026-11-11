# Panelize

<img src="images/icons/mod_panelize.png" width="32" height="32" alt="Panelize">

Splits the faces into panels with a gap and a chamfer between them. It's quick for sci-fi panels,
tiles and paneled walls: every face becomes one panel, and the gaps can get their own material.

![Voronoi Fracture with Exploded View, Poly Select with four Poly Edits, Panelize and Radial Array](images/modifiers_modeling.png)

*Panelize on a sphere is third from the left.*

Add it from **Add Modifier ▸ Generate**. It works on meshes. In the [Modifier List](MODIFIER_LIST.md)'s Add menu it's in the **Build** column.

## Settings

- **Gap**: Width of the gap between panels.
- **Chamfer**: Width of the slope on the edge of each panel.
- **Height**: How far the panels stand out, or sink when negative.
- **Gap Material Offset**: Added to the material index of the gap faces.
- **Even**: Keep the gap the same width at sharp corners.
- **Relative**: Scale the gap by the size of each face.
- **Vertex Group**: Only panelize faces whose points are all in this group.

## As a node

Geometry Nodes has a **Panelize** node that shares its code with this modifier, so a node tree and the modifier give the same result.

## In Python

```python
mod = obj.modifiers.new("Panelize", 'PANELIZE')
```

The Python type is `PanelizeModifier`. Official Blender doesn't know this modifier, so a file that uses it won't give the same result there.
