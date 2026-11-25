# AIVERSE World Processes Stage38 — deterministic neighbor edge snapping
Date: 2026-11-23
Branch: feature/world-processes-stage31-bidirectional-handles
Main untouched. PR #112 remains draft.

## Changes
- `apps/studio-native/StudioWorldProcessesEditing.h`: pure `snapNeighborEdge(map,owner,mode,proposedCm,thresholdCm)` examines enabled neighboring authored EvolveRegion+EvolveArea rectangles. Finds ±X or ±Z boundaries within 100 cm of the dragged edge, excluding the owner. Returns local centimetres and stable target map ID. Nearest distance wins; ties resolved by lexicographic stable map ID. Does not mutate MapDoc/ECS.
- `apps/studio-native/StudioHost.cpp`: when Ctrl/Command snap is active during EvolveArea edge dragging, neighboring boundaries take precedence over the existing 1m grid fallback. Center handles continue grid snap only.
- Existing asymmetric/symmetric Shift drag, typed RPC and one Undo transaction remain in place.

## Limitations
- This is axis-aligned edge coordinate snapping; no overlap-range check on the perpendicular axis, corner snapping, group resize, group preview, or numeric editable Scene fields.
- No separate persistent spatial index; current implementation scans map entities. Intended for authoring scale, not a runtime ECS query.
- If a neighboring edge yields an invalid resulting region, preview can be suppressed and the RPC can reject the edit; no partial MapDoc writes.

## Verification
Source changes committed to GitHub. No compilation, native Scene run, Undo/Redo, save/reopen, performance, cross-platform or TerraWeave QA performed (user deferred tests). No green status claimed.

## Next
1. Restrict snapping to genuinely adjacent/overlapping projected edge spans and visualize target ID and snap guide.
2. Add multi-region group resize with explicit pivot and atomic MapDoc transaction.
3. Add editable in-Scene numeric dimensions and snapping preferences.
4. Compile/QA when user permits; keep draft PR #112 unmerged.

## Source commits
`7c050d59`, `72d24fcc`, `7cebe804`.
