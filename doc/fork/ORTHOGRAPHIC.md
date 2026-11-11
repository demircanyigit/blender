# Orthographic view

<img src="images/icons/op_persp_ortho.png" width="32" height="32" alt="Perspective/Orthographic">

Two fixes to how the 3D Viewport works in orthographic view.

## Orthographic stays orthographic

With **Auto Perspective** on in the preferences, Blender goes back to perspective when you rotate
out of a front, side or top view. That's still true for an orthographic view that Blender switched
to by itself, but a view you switched to orthographic yourself, with **Numpad 5** or the
Perspective/Orthographic button, now stays orthographic: when you rotate it, when you Alt+middle-drag
to snap it to an axis, and when you rotate out of that axis again.

Press **Numpad 5** again to go back to perspective.

## The grid stays on screen

In an orthographic view at an angle, the floor grid used to slide off the screen and disappear
when you looked at things away from the center of the world. Now the grid is centered where the
middle of the view meets the floor, the same way it is in perspective, so it stays under what you're
looking at.

Front, side and top views look the same as before.
