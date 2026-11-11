# Materialize

A modifier (`MATERIALIZE`) under Add Modifier > Edit, and a **Materialize** node under Mesh ▸
Operations. It gives materials to faces by rules. Faces that no rule picks keep their material.

## Rules

The modifier has a list of rules. They run in order, and a later rule wins over an earlier one.
The node is one rule, so chain nodes for more.

Each rule has:

- **Material**: the material the faces get. If the mesh doesn't have it yet, it is added as a new
  slot. With the **#** button, **Material Index** picks an existing slot instead.
- **Only On**: only faces that already have this material.
- **Type**, which picks the faces:
  - **Curvature**: faces on sharp edges (Convex), in creases (Concave) or both, sharper than
    **Curvature Angle**.
  - **Direction**: faces facing Up, Down, to the Sides, ±X, ±Y or a Custom direction, within
    **Direction Angle**.
  - **Height**: faces in a band of height on an **Axis**, from **Height From** to **Height To**.
    With **Relative**, 0 is the bottom of the mesh and 1 the top.
  - **Slope**: faces whose slope is between **Slope Min** and **Slope Max**.
  - **Noise**: faces in patches of noise (**Scale**, **Detail**, **Threshold**).
  - **Random**: faces picked at random by **Chance**, per Face, per Island (loose part), or the
    whole Object.
  - **Attribute** (modifier) or **Value** (node): faces where a value is above **Threshold**.
  - **Proximity**: faces closer than **Distance** to another object (modifier) or a **Target**
    mesh (node).
  - **Ambient Occlusion**: faces hidden more than **Occlusion**, looking **Occlusion Distance**
    away with **Samples** rays.
  - **Thickness**: faces where the mesh is thinner than **Thickness**.
  - **Texture**: faces where a texture is above **Threshold** (modifier only; on the node, use a
    texture node with Value).
- **Global**: directions, heights and noise in world space instead of the object's own.
- **Grow**: adds rings of faces around the picked faces, or removes them when negative.
- **Min Island**: patches of fewer faces are dropped.
- **Breakup** and **Breakup Scale**: break up the border with noise.
- **Invert** and **Seed**.

## Output

- The face attribute `material_index`, and new material slots on the evaluated mesh when needed.
- The node gives a **Picked** field: the faces that got the material.
- Ambient Occlusion and Thickness cast rays per face, so they are slow on dense meshes.
