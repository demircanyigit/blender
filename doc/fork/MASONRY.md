# Masonry: continuous walls

Enable **Masonry > Options > Continuous Walls** to continue a brick layout across the narrow
vertical faces of a beveled wall. In Geometry Nodes, enable the **Continuous Walls** input.
The option starts disabled so existing files retain their previous result.

![The same rounded wall with per-face generation and continuous rigid bricks](images/masonry_continuous.png)

This mode measures distance along connected walls, lays out the pattern once, and places each
brick as one rigid solid. The internal edges of the input mesh no longer cut bricks into strips.
Bricks follow the wall direction at their centers; they do not bend across the curve.

## Supported surfaces

- Vertical walls, including straight subdivided walls and rounded vertical corners.
- **Auto** direction with Running, Stack, Flemish, English, Ashlar, Ledgestone or Rubble patterns.
- Pattern rotation, depth variation, tilt, borders, toothed boundaries, materials and selection.
- Closed round walls get a layout seam where their unfolded ends meet.

Marked **Sharp** edges, non-manifold edges, selection boundaries and corners turning 45 degrees
or more break the continuous layout. Mark a vertical edge sharp to deliberately end a wall strip.
These are geometric edge markings; changing smooth shading alone does not mark an edge sharp.

The unfolded boundary must be one convex outline. Sloped surfaces, concave outlines and walls
with openings or branching boundaries keep the original per-face behavior. This fallback avoids
filling openings or creating bricks across ambiguous surfaces.

## What to expect

Increasing the number of bevel segments should improve the wall approximation without producing
additional rows of thin brick fragments. Partial bricks still occur at actual layout boundaries.
Long rigid bricks on a tight curve can leave space behind their ends and wider wedge-shaped joints.
Use a shorter brick length or a larger corner radius when appropriate. Rigid bricks approximate
the curve with flat faces; this mode does not produce curved or tapered bricks.

The modifier and node share the same geometry code. The original mesh stays unchanged.

## Regression checks

```bat
blender.exe -b --factory-startup --python-exit-code 1 --python tests/python/bl_masonry_continuous.py
```

The suite checks subdivision-independent brick counts, bevel-segment fragmentation, sharp breaks,
right-angle breaks, round-wall seams, closed brick meshes, and modifier/node agreement.
