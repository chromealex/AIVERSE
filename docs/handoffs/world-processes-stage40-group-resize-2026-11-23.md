# AIVERSE World Processes Stage40 — atomic EVOLVE group resize
Date: 2026-11-23
Branch: feature/world-processes-stage31-bidirectional-handles
Main untouched. PR #112 remains draft.

## Implemented
- `apps/studio-native/StudioSession.cpp`: `studio.worldProcessAreaBatchResize` takes `{entityIds:[stable IDs],axis:"x"|"z",pivot:"individual"|"shared",scaleNumerator:int,scaleDenominator:int}`. Validates 2–256 unique eligible, unlocked and visible EVOLVE area entities before writing. Uses deterministic integer-centimetre rounding and checks half-extent, typed offsets and world budget. Stages all updated entities, then one MapDoc `beginEdit/touched`. The shared pivot is the arithmetic mean of selected area centers on the active axis. Individual mode preserves each area's center.
- `apps/studio-native/StudioHost.cpp`: dragging an edge of a multi-selection computes a scale ratio from active owner's old and previewed half-extent, then calls the batch RPC. Plain edge drag = shared pivot; Shift edge drag = individual pivots. Existing single-region edit and multi-region center translation remain unchanged. Registered the new RPC as document mutation.
- `apps/studio-native/StudioSession.cpp`: Undo history label `Resize EVOLVE region group`.
- `skills/world/aiverse-evolve/SKILL.md`, `skills/world/aiverse-world-processes/SKILL.md`: updated Stage40 contracts.

## Boundaries
The edit changes authored EvolveArea offsets and half-extents; runtime `evolve::Area` is hydrated from the map and remains the single authoritative ECS region. TerraWeave still consumes `evolve::extents(world)`. No genre dependency or extra mutable region index.

## Limitations
- Only one axis is scaled per drag; group box corner scaling and 2D scale are future work.
- Preview still draws the active owner's rectangle, not all group members.
- Group scaling ratio uses the active area's half-extent and is bounded by typed RPC limits.
- No explicit group-pivot gizmo yet. Current shared pivot is the mean of selected centers.
- Multi-selection with non-EVOLVE entities is rejected atomically.

## Verification
GitHub accepted source commits. No native C++ compilation, CTest, Studio drag QA, Undo/Redo, save/reopen, ECS tick, cross-platform acceptance or screenshots were run, per user test deferral. Do not claim verified runtime behavior.

## Next
1. Render transient previews for all selected areas during group resize, including shared pivot.
2. Add explicit pivot control and numeric group scale fields in Scene.
3. Reconcile preview/commit rounding and extreme-coordinate quantization.
4. Build and run actual Studio and TerraWeave acceptance once tests are authorized. Keep PR #112 draft until verified.

## Provenance
Stage39 head `c058983c21d77d0517b76422570266eccf3bdba3`.
Stage40 source commits `c4a1abebe1b9ac09e1921e2dd5866a8cd8f124f3`, `a869484ab0517b373772139e0aa723414284b955`, `45a7d2257828a1bb84b3be7dff8250ffec8fc693`.
