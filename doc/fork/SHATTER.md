# Shatter

<img src="images/icons/mod_shatter.png" width="32" height="32" alt="Shatter">

Breaks a closed mesh the way a material breaks: stone into chunky pieces, glass into shards around
an impact, wood into long splinters, ceramic into a few big pieces, and ice into a mix of cracks and
chunks. An **Impact** object sets where it was hit, and the pieces get smaller there.

For an even break into cells, see [Voronoi Fracture](VORONOI_FRACTURE.md).

Add it from **Add Modifier ▸ Generate**. It works on meshes. In the [Modifier List](MODIFIER_LIST.md)'s Add menu it's in the **Edit** column.

## Settings

- **Material**: How it breaks.
  - **Stone**: Chunky pieces of mixed sizes, with small chips.
  - **Glass**: Shards between cracks running out from the impact and rings around it.
  - **Wood**: Long splinters along the grain.
  - **Ceramic**: A few big pieces.
  - **Ice**: Cracks from the impact like glass, with chunks like stone.
- **Pieces**: About how many pieces to break into.
- **Seed**: Changes the pattern.
- **Impact**: Where it was hit. The pieces are smaller there, and glass cracks out from it. Without one, the middle of the mesh.
- **Focus**: How much smaller the pieces get near the impact.
- **Grain Axis**: Direction of the wood grain.
  - **Auto**: The longest side of the mesh.
  - **X**
  - **Y**
  - **Z**
- **Grain**: How many times longer than wide the splinters are.
- **Roughness**: How jagged the broken faces are. Glass and ceramic look best at 0.
- **Detail**: Points on the broken faces for the roughness to move.
- **Cut Gap**: Distance between neighboring pieces.
- **Interior Material**: Give the broken faces their own material.
- **Interior Material Index**: Material index of the broken faces.

## As a node

Geometry Nodes has a **Shatter** node too, see [NODES.md](NODES.md#shatter).

## In Python

```python
mod = obj.modifiers.new("Shatter", 'SHATTER')
```

The Python type is `ShatterModifier`. Official Blender doesn't know this modifier, so a file that uses it won't give the same result there.
