# Decimate algorithms

The existing Decimate modifier now offers six algorithms in Collapse mode:

| Algorithm | Purpose | Limits |
| --- | --- | --- |
| Blender | Original Blender collapse, symmetry, weighted vertex group, optional triangulation | Original behavior remains the default |
| PMP | Quadric simplification with aspect ratio and normal-change limits | Half-edge topology; non-manifold triangles may be detached |
| Meshoptimizer | Attribute-aware reduction with UV and normal weights | Preserves topology more conservatively than Aggressive |
| Meshoptimizer Aggressive | Fast spatial reduction | Can change topology; fewer quality guarantees |
| CGAL | Garland-Heckbert plane and line quadrics | Requires consistently oriented manifold input; rejects invalid input |
| Wild QEM (Experimental) | Area-weighted plane and dynamic boundary quadrics with optional virtual pairs | Geometry-only adaptation, not the authors' full textured pipeline |

Ratio and Triangle Count targets are available. The five alternative algorithms output triangles.
Protected boundaries, UV seams, material boundaries and sharp features can prevent reaching a target.
The original Blender collapse can retain a two-triangle minimum even for a zero target.
Disable unnecessary protections to allow further reduction. Feature Angle controls automatic sharp
feature detection. An empty UV Map uses the active UV map. Meshoptimizer weights use that UV map
and averaged corner normals. Aggressive uses the seam locks but does not use attribute weights.

For alternative algorithms a vertex group is a binary selection: zero-weight vertices are protected;
positive-weight vertices can simplify. Blender retains its original weighted influence behavior.
Merge Coincident welds exactly identical positions before simplification, without changing the input.
Point, face and corner attributes are sampled back from the original surface; corner sampling uses
the output face direction to select UV chart sides. Edge attributes are not transferred. This is
surface sampling, not texture rebaking, and should be inspected on overlapping or thin surfaces.

Error Limit zero disables its extra error constraint. Meshoptimizer uses absolute geometric error;
Aggressive converts it to a relative spatial tolerance; PMP and Wild use their respective quadric
metrics. These values are not equivalent across algorithms. CGAL does not expose an error limit.
Wild Virtual Distance zero disables cross-component virtual pairs. A positive distance permits
nearby disconnected components to merge; this is experimental. No claim is made that every
algorithm is faster or produces better results on every mesh.

## Dependencies and reproducible builds

Meshoptimizer 1.3 is vendored under `extern/meshoptimizer`, with upstream code unchanged.
PMP is the fork's existing dependency. CGAL 6.2.1 and Boost 1.88.0 headers are downloaded with
SHA-256 verification by:

```
python build_files/utils/make_decimate_deps.py
```

Headers go to ignored `lib/decimation`. A local archive cache can be supplied with `--archives`.
`WITH_DECIMATE_CGAL` defaults to ON. Eigen comes from Blender's existing dependencies.
No external executables or Node runtime are needed to run these algorithms.

The Wild geometry adaptation credits the independent MIT reference snapshot in
`extern/wild_qem_reference/README.blender`. It is experimental and does not implement the complete
2025 paper, appearance quadrics, or texture atlas rebaking. Dependency license notices are installed
from `release/license/decimation`. CGAL's simplification package uses GPL-3.0-or-later; distributors
must follow the applicable Blender and dependency license terms and provide corresponding source.

## Verification

```
Blender.exe --background --factory-startup --python-exit-code 1 --python tests/python/bl_decimate_backends.py
```

Set `DECIMATE_LEGACY_FILE` to a .blend saved with a pre-backend build to exercise migration of
existing Decimate settings. The test suite also checks all algorithms, ratio/count targets,
protected vertices, UV charts, corner attributes, non-manifold input, and save/reload.
