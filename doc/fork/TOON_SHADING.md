# Toon shading in Solid mode

Solid shading has a fourth lighting choice next to Studio, MatCap and Flat: **Toon**. It lights
the objects in a few flat bands instead of a smooth gradient, with an optional hard highlight and a
rim light. It works with every Object Color option (Material, Face Set, Attribute, Random and so
on), and together with the [outline and edge lines](SOLID_OUTLINES.md) it gives a cel look.

## Settings

In the 3D viewport's Shading popover, in Solid mode, set Lighting to **Toon**:

- **Light**
  - **Scene**: the light is fixed in the scene. It's the same light as the Shadow, so bands and
    cast shadows agree.
  - **View**: the light turns with the view, so the lighting stays the same as you orbit.
- **Direction**: the Light Direction of the scene, the same one the Shadow settings use. With
  **View** it is read relative to the view.
- **Bands**: the number of light steps, from 2 to 6.
- **Softness**: how soft the step between two bands is. 0 is a hard step.
- **Shadow Color**: multiplied into the dark bands. The default is a cool grey-blue.
- **Highlight**: a hard-edged highlight spot. **Size** sets how big it is.
- **Rim Light**: a bright edge where the surface turns away from the view, in its own **Color**.
  **Width** sets how far it reaches.

Shadow, Cavity, X-Ray, the outline and the edge lines all work on top of it. The Workbench render
engine has it too, in its own Lighting panel.

## In Python

```python
shading = bpy.context.space_data.shading
shading.light = 'TOON'
shading.toon_light = 'SCENE'           # or 'VIEW'
shading.toon_bands = 3
shading.toon_softness = 0.05
shading.toon_shadow_color = (0.45, 0.45, 0.6)
shading.use_toon_highlight = True
shading.toon_highlight_size = 0.3
shading.use_toon_rim = True
shading.toon_rim_color = (1.0, 1.0, 1.0)
shading.toon_rim_width = 0.2
bpy.context.scene.display.light_direction = (0.577, 0.577, 0.577)
```

Official Blender doesn't know Toon. A file saved with it opens there with an unknown lighting
choice, so switch to Studio first if the file has to look right in official Blender.
