# Normal Transfer

<img src="images/icons/mod_normal_transfer.png" width="32" height="32" alt="Normal Transfer">

Copies the shading normals of another object. The mesh keeps its shape and only shades like the
target. Good for hiding the seam between a head and a body, for grass and leaf cards that shade like
a soft shape around them, and for low-poly meshes that should shade like a smooth version.

Add it from **Add Modifier ▸ Normals**. It works on meshes, and the target must be a mesh. In the
[Modifier List](MODIFIER_LIST.md)'s Add menu it's in the **Normals** column.

Blender's Data Transfer modifier can copy normals too. Normal Transfer does only that, so it has
fewer settings, and it fades out with distance instead of stopping at a hard edge.

## Settings

- **Target**: the mesh object to copy the normals from. It can't be the object itself.
- **Mapping**: how each point finds its place on the target.
  - **Nearest Surface**: the closest point on the target's faces, blended across the face. This
    is the default and fits most cases.
  - **Nearest Point**: the closest point of the target. Faster, but blockier on a coarse target.
  - **Along Normal**: looks along each point's own normal, both ways, and takes the normal where
    it meets the target. Good for a shell over a body.
  - **Projected**: looks along an **Axis** of the object, both ways. Points that miss the target
    keep their own normals.
- **Factor**: how much of the target's normals to use. 0.5 is halfway between the two.
- **Max Distance**: only change the points near the target.
  - **Distance**: points closer than this take the target's normals fully.
  - **Falloff**: past the distance, the effect fades out over this length. 0 stops at the
    distance.
- **Open Edges Only**: only change the points at the open edges of the mesh, where a seam is, and
  near them.
  - **Edge Distance**: the effect fades out over this distance from the open edges.
- **Vertex Group**: only change the points in this group, by their weight. **Invert** swaps the
  weights.

Distances are in world units.

## Flat faces

Shading normals don't show on flat faces. Faces the modifier changes are made smooth. Flat faces it
doesn't change stay flat.

## Good to know

- The target's own shading counts: its sharp edges, flat faces and custom normals are copied too.
- The target's modifiers are included.
- Add it after modifiers that change the shape, so the normals are set last.

## In Python

```python
mod = obj.modifiers.new("Normal Transfer", 'NORMAL_TRANSFER')
mod.target = bpy.data.objects["Body"]
mod.use_distance = True
mod.distance = 0.05
mod.falloff = 0.1
```

The Python type is `NormalTransferModifier`. Official Blender doesn't know this modifier, so a file
that uses it won't give the same result there.
