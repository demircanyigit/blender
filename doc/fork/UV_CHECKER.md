# Checker in Solid shading

Solid shading has a **Checker** color. It shows a checker pattern on the active UV map of each
mesh, without a material or an image. Use it to see stretched UVs, and to check that objects have
the same texel density.

## How to use it

1. In the 3D Viewport's Shading popover, use **Solid**.
2. Under **Color**, pick **Checker**.
3. Pick a pattern:
   - **Color**: a different color for each square, with a thin line around it. Colors change
     across U and get lighter across V, so you can see where each part of the UVs goes.
     This is the default.
   - **Gray**: light and dark gray squares.
   - **UV**: red grows across U and green across V, under the squares. A turned or flipped
     island shows the colors going the wrong way.
4. **Scale** sets how many squares fit across one UV unit. The default is 8.

Meshes without a UV map show their material color instead.

## Checking texel density

When two objects have the same texel density, the squares on them are the same size. Set the same
**Pixels per Meter** and **Texture Size** with the [UV Scale](UV_SCALE.md) modifier on each object,
and the squares match.

With Scale at 8 and a texture size of 1024, each square stands for 128 pixels. So at 512 pixels
per meter, a square is 25 cm across.

## Good to know

- It only changes the viewport. Renders, and the other color choices, are as before.
- From Python: `shading.color_type = 'CHECKER'`, `shading.checker_type` and
  `shading.checker_scale`.
- Official Blender doesn't know this color.
