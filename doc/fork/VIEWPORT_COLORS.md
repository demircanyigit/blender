# Color attributes in Solid shading

Official Blender can show a color attribute in Solid mode only for the whole viewport, with the
Attribute color type. This fork adds an option per material, so a material's Viewport Display color
can be combined with the mesh's active color attribute while the viewport's color type stays on
Material.

This is useful for checking color variation without switching to Material Preview or Rendered, for
example vertex colors that a game engine uses to vary a material's color.

## How to use it

1. Give the mesh a color attribute and make it active.
2. In the 3D viewport's Shading popover, use **Solid** and set Color to **Material**.
3. In the Material tab, open **Viewport Display** and set **Color Attribute**.

## Options

| Color Attribute | Result |
|---|---|
| None | Only the viewport color, as in official Blender. This is the default |
| Mix | Goes from the viewport color to the color attribute |
| Multiply | Viewport color multiplied by the color attribute |
| Two Colors | The attribute goes from the viewport color (0) to **Second Color** (1) |
| Color Ramp | The attribute looks up a color in a ramp |

| Setting | Used by | Meaning |
|---|---|---|
| Factor | All except None | Strength of the color attribute |
| Channel | Two Colors, Color Ramp | **Brightness**, **Red**, **Green**, **Blue** or **Alpha** picks the value that's used |
| Second Color | Two Colors | Color at value 1 |
| Ramp | Color Ramp | The colors to look up |

From Python: `viewport_color_blend`, `viewport_color_factor`, `viewport_color_channel`,
`viewport_color_b` and `viewport_color_ramp` on the material.

## Limits

- It only applies to mesh objects in Solid mode with the Material color type.
- It only changes the viewport. Renders use the material's nodes as usual.
- Official Blender ignores these settings, and loses them if it saves the file.
