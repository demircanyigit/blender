# UV Scale and Texel Density

**UV Scale** is a native modifier and Geometry Node. Both use the same implementation and the
approved checker-tile icon: blue for the modifier and teal for the node.

Use **Add Modifier > Modify > UV Scale**, or **Modifier List > UV & Data > UV Scale**.
The node is under **Add > Mesh > Operations > UV Scale**.

## Scaling

Choose **Scale**, then set the U and V scale factors. Both start at 1, so adding the modifier
preserves the input UVs. Set both to the same value to keep the UV proportions; negative values
mirror an axis.

- **Individual Islands on:** each UV island scales around its own UV bounding-box center.
- **Individual Islands off:** all UVs scale together around the overall UV bounding-box center.
- **Pivot:** choose Bounds Center, UV Origin (0, 0), or a custom UV position. Origin and Custom use
  the same specified position for every island.

Islands are identified by UV continuity across shared mesh edges, not by marked seams or whether
UV coordinates happen to overlap. Disconnected meshes with stacked UVs stay separate islands.

**UV Map** selects an existing map. Leave it empty to use the active map. The other maps and mesh
positions remain unchanged. This feature transforms an unwrap; it does not create one.

## Matching texture scale between objects

Choose **Texel Density**, then use the same **Pixels per Meter** and **Texture Size** on each
object. In the node, set Texture Width and Texture Height individually.

For example, a one-meter square with a 1024 x 1024 texture and a target of 512 pixels per meter
needs a UV square 0.5 units wide. A two-meter square needs a UV square 1 unit wide. Both then
show the same physical texture scale.

The calculation includes unapplied object scale, including nonuniform scale, and the scene's
Unit Scale (meters per Blender unit). The node uses its owning object's transform; realize
instances before this node when instance transforms need to contribute to the density.

With Individual Islands enabled, each island reaches the target independently. With it disabled,
the entire mesh reaches the target on average, preserving differences between islands.

Density is an area-based average:

```text
current density = sqrt(UV triangle area * texture width * texture height / surface area in m²)
UV scale factor = target density / current density
```

Triangle areas are summed with positive UV areas, so mirrored islands are supported. Rectangular
textures use width times height. This matches average pixel density; it does not repair stretched
UVs or force equal pixel density along both axes of a distorted unwrap.

## Behavior and limits

- Scaling preserves the chosen pivot and UV shape. It does not pack islands, add padding, or
  prevent overlaps. UVs can extend beyond the 0-1 tile, including into adjacent UDIM tiles.
- A zero-area UV island or zero-area surface cannot have a meaningful density. It stays unchanged
  and a message is shown; other islands are still processed.
- A missing UV map produces a message and leaves the mesh unchanged.
- The operation uses geometry at its position in the modifier stack. Put it after size-changing
  modifiers if their changes should affect density.
- Both versions recalculate from their input on each evaluation, without accumulating scaling.
- Texture size is explicit, allowing the same density to be used with different materials. It
  does not automatically inspect material images or support different resolutions per face.

## Validation

Run the regression suite with a build of this fork:

```bat
blender.exe --background --factory-startup --python-exit-code 1 --python tests/python/bl_uv_scale.py
```

It checks global and individual pivots, connected and split UV islands, world-space density,
rectangular textures, scene units, mirrored and collapsed UVs, named maps, modifier/node parity,
dependency updates, and saving/reloading both native types.
