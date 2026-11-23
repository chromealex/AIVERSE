# AIVERSE World Processes Stage41 — group resize previews
Date: 2026-11-23
Branch: feature/world-processes-stage31-bidirectional-handles
Main untouched; PR #112 remains draft.

## Implementation
- `apps/studio-native/StudioWorldProcessesEditing.h`: pure `previewGroupResize(map,selected,ownerId,mode,proposedCm,individual,output,pivotPoint)` computes candidate rectangles for all selected EVOLVE regions. The active owner's preview determines the rational scale ratio; shared mode uses the arithmetic mean of authored centers on the active axis, individual mode keeps each center. All members must have EvolveRegion and EvolveArea; invalid geometry rejects the whole preview.
- `apps/studio-native/StudioHost.cpp`: during edge dragging with multiple selected regions, overlays each candidate rectangle in amber and shows the shared centroid marker/label. Shift selects individual pivots, ordinary drag uses shared pivot. No document mutations until mouse-up.
- Existing `studio.worldProcessAreaBatchResize` is the sole group resize commit RPC, one MapDoc Undo transaction. ECS Area and TerraWeave remain authoritative.

## Limits
- This is a read-only visual group preview, not a new numeric editable Scene widget. Numeric scale is already accepted as exact integer numerator/denominator in the Stage40 RPC, but no interactive numeric input was added in Stage41.
- Shared pivot is a computed mean on the active axis; user-defined pivot is not implemented.
- Preview only scales one axis per drag. Group translation preview remains future work.
- Geometry is derived separately from session RPC; exact boundary/rounding parity requires later runtime verification.

## Verification
Source commits accepted by GitHub. No compilation, CTest, native Scene interaction, screenshots, Undo/Redo, save/reopen, ECS tick or CI executed (user requested tests later). Do not claim QA green.

## Next
1. Add editable numeric Scene control for exact group scale and dimensions, preserving one atomic Undo.
2. Expose optional user-defined pivot and 2-axis scaling.
3. Reconcile preview and RPC integer arithmetic and invalid mixed selections.
4. Run real native compilation/acceptance only when user authorizes; keep PR #112 draft.

## Commits
`21e14d58149b9642c24caa64c41695db7e8a209d`
`945d739168de09da32c0d0074296b9be4e042f2a`
