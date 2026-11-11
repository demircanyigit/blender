# Blast

Native modifier (`BLAST`, Python type `BlastModifier`) in Add Modifier > Generate, and the Elements
column of Modifier List mode. Its icon highlights a retained large island while crossing out small
parts. Source and icon only: this addition has not been built, executed or tested, and no tests were
created for it.

The workflow is inspired by [SideFX Labs Delete Small Parts](https://www.sidefx.com/docs/houdini/nodes/sop/labs--delete_small_parts.html),
which filters connected pieces by size and can extract the largest piece. Blast implements the
specific mesh measures and largest-N controls below, rather than duplicating the entire Labs tool.

## Controls

**Measure** determines both the threshold value and largest-island ranking:

| Measure | Island value |
| --- | --- |
| Face Count | Number of original polygons, not triangulated face count |
| Surface Area | Sum of triangulated polygon areas |
| Volume | Absolute signed enclosed volume, for eligible closed/oriented islands |
| Bounding Box Size | Longest side of the object-space axis-aligned bounding box |

**Filter** chooses how values are used:

- **Threshold:** keep islands at or above Minimum Faces, Area, Volume or Size. Equality is kept.
- **Keep Largest N:** ignore the threshold and retain up to Keep Count eligible islands.
- **Threshold + Largest N:** apply the threshold, then retain at most Keep Count of its largest
  survivors. Smaller parts do not fill the remaining slots if too few pass the threshold.

**Invert Result** keeps eligible islands that the filter would remove. **Keep Count = 0** retains
no eligible islands before inversion. Ties are resolved by first input vertex order, so unchanged
input has deterministic results. Vertex reordering can change which equally sized island wins.
The default is Surface Area / Threshold / Minimum Area 0.01.

## Islands and preservation

Connectivity follows mesh vertices and edges, including wire edges. Faces sharing a vertex belong
to one component. Close or coincident but unwelded pieces stay separate. A wire bridging two chunks
makes them one connected island. Loose points and wire-only components are included: their face
count and area are zero, while Bounding Box Size can measure a wire's length.

Whole islands are retained or removed, without cutting, remeshing, filling or Boolean operations.
Surviving positions, edges, faces, corner data, materials and supported attributes are selected
through the existing mesh-copy-selection implementation. If every island survives, input is bypassed;
if every island is removed, the result is an allocated empty mesh.

Area, size and volume use object-space units (length squared and cubed where applicable). Apply
object scale for consistent physical thresholds across differently scaled objects. Bounding Box Size
is axis-aligned in object space and can therefore change when geometry rotates within that space.
It is a longest-side length, not bounding-box volume; flat islands can have a non-zero size.

## Volume eligibility

Volume is accumulated in double precision around a per-island reference point using compensated
summation. Global reversed winding still gives the same absolute size. Islands with no faces,
open edges, edges with other than two incident faces, or inconsistent adjacent-face orientation
are preserved and warned about. Wires attached to an otherwise closed component also make it
ineligible for volume filtering.

These protected islands stay outside threshold/rank/invert selection. Consequently, Keep Largest N
can output N eligible closed islands plus any protected islands. Choose Area, Face Count or Bounding
Box Size to filter open surfaces and loose geometry instead. The volume check is edge-based;
it is not a self-intersection, nested-shell or general solid-validity solver. Self-intersecting
surfaces can have algebraic volumes that are not physical solid volumes.

Non-finite coordinates bypass spatial measurements with a warning; Face Count does not require
valid coordinates. Negative/non-finite thresholds and negative keep counts also retain the input
with a warning. Connectivity runs once, and triangulation/edge-volume checks are requested only
when the selected measure needs them. Largest-N selection uses a deterministic partial sort.

The icon can be regenerated with `python build_files/utils/make_blast_icon.py` (requires Pillow).
Existing executables will include Blast only after rebuilding.
