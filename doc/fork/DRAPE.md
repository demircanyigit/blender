# Drape

<img src="images/icons/mod_drape.png" width="32" height="32" alt="Drape">

Settles a mesh as cloth over other objects and shows where it comes to rest, without a cloth
simulation or baking. Use it for tablecloths, blankets, cushions and covers.

Add it from **Add Modifier ▸ Generate**. It works on meshes. In the [Modifier List](MODIFIER_LIST.md)'s Add menu it's in the **Build** column.

## Colliders

Set **Collider** to the mesh the cloth lands on. Under it is a list for more: **Add Object** and
**Add Collection** add an entry, **Add Selected Objects** adds every selected mesh object, and each
entry can be turned off or removed. **Cage** pushes the colliders out along their normals, so the
cloth rests further away, or pulls them in when negative. **Floor** adds a flat floor under the
lowest point.

## Settings

- **Collider**: Mesh the cloth lands on.
- **Floor**: Land on a flat floor under the lowest point of the cloth and the collider.
- **Pin**: Points in this group stay where they are.
- **Time**: Seconds the cloth falls and settles for.
- **Gravity**: How hard the cloth is pulled down.
- **Stiffness**: How much the cloth resists stretching.
- **Bending**: How much the cloth resists folding, from silk at 0 to leather near 1.
- **Shrink**: Pull the cloth taut between the pins. Negative values give it slack.
- **Pressure**: Puff up closed meshes, like cushions and pillows.
- **Thickness**: Distance the cloth keeps from what it touches.
- **Friction**: How much the cloth grips what it touches instead of sliding off.
- **Self Collision**: Keep folds from passing through each other. Slower.
- **Quality**: Solver steps per 1/60 second. Higher keeps the cloth from stretching and from passing through thin colliders.

## Good to know

- The cloth needs enough faces to fold. Subdivide it, or put a Subdivision Surface modifier below
  Drape.
- Points in the **Pin** vertex group stay where they are, like the corners of a hanging sheet.
- Long **Time** values and dense cloth take longer to work out. Turn the modifier off in the
  viewport while you change things below it.

## As a node

Geometry Nodes has a **Drape** node too, see [NODES.md](NODES.md#drape).

## In Python

```python
mod = obj.modifiers.new("Drape", 'DRAPE')
```

The Python type is `DrapeModifier`. Official Blender doesn't know this modifier, so a file that uses it won't give the same result there.
