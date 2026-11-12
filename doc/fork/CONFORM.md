# Conform

<img src="images/icons/mod_conform.png" width="32" height="32" alt="Conform">

Lays the mesh onto the surface of another object, keeping its own thickness. Good for roads and
paths over terrain, straps and belts around a body, decals and panels on a curved surface.

Add it from **Add Modifier ▸ Deform**. It works on meshes, and the target must be a mesh. In the
[Modifier List](MODIFIER_LIST.md)'s Add menu it's in the **Deform** column.

## How it works

Each point is measured from the bottom of the mesh along the **Axis**. That height is its
thickness. The point is moved onto the target, then lifted by its thickness again. So a flat
strip with some height stays the same height everywhere, and follows the bumps of the target.

With **Keep Thickness** off, the height is dropped and the whole mesh is flattened onto the
surface.

## Settings

- **Target**: the mesh object to lay the mesh onto. It can't be the object itself.
- **Mode**:
  - **Along Axis**: moves the points along the axis until they meet the target, in whichever
    direction it's closer.
  - **Nearest Surface**: moves the points to the closest place on the target, and lifts them
    along the target's surface direction there. Good for wrapping around round shapes.
- **Axis**: the direction of the object to move the points in. The default is **-Z**, straight
  down in the object's own space. Thickness is measured along this axis in both modes.
- **Offset**: distance to keep from the target surface, in world units.
- **Keep Thickness**: keep the height of each point above the bottom of the mesh.
- **Vertex Group**: only move the points in this group, by their weight. A weight of 0.5 moves a
  point halfway. **Invert** swaps the weights.

Points that find no surface stay where they are.

## Good to know

- The axis is the object's own, so rotating the object changes the direction.
- Add a [Subdivision Surface](https://docs.blender.org/manual/en/latest/modeling/modifiers/generate/subdivision_surface.html)
  or more edge loops before Conform when the mesh is too coarse to follow the target.
- The target's modifiers are included.

## In Python

```python
mod = obj.modifiers.new("Conform", 'CONFORM')
mod.target = bpy.data.objects["Terrain"]
```

The Python type is `ConformModifier`. Official Blender doesn't know this modifier, so a file that
uses it won't give the same result there.
