# Island color in Solid shading

Solid shading has an **Island** color. Each loose part of a mesh gets its own color, so you can
see which parts are connected inside one object. **Random** does the same per object.

## How to use it

In the 3D Viewport's Shading popover, use **Solid** and set Object Color to **Island**.

A loose part is a group of faces connected by edges. Parts that only touch, or overlap, without
sharing an edge are separate parts.

## Good to know

- It works in Object Mode and Edit Mode, and shows the result of the modifiers.
- The colors are the same kind as the face set colors. They're picked in the order of the faces,
  so they stay the same as long as the parts keep their order.
- Objects that aren't meshes show their material color. So does a mesh in Sculpt Mode.
- To draw a line around each part instead, set the outline to **Loose Part**, see
  [SOLID_OUTLINES.md](SOLID_OUTLINES.md).
- From Python: `shading.color_type = 'ISLAND'`. Official Blender doesn't know this color.
