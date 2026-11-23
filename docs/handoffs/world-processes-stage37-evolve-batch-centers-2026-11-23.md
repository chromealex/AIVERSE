# AIVERSE World Processes Stage37 — atomic EVOLVE multi-center move
Date: 2026-11-23
Branch: feature/world-processes-stage31-bidirectional-handles
Main untouched; PR #112 remains draft.

## Implemented
- `apps/studio-native/StudioSession.cpp`: `studio.worldProcessAreaBatchMove` accepts `{entityIds:[stable map IDs],axis:"x"|"z",deltaCm:integer}`, with 2..256 unique entities. Validates every member's EvolveRegion+EvolveArea, lock/hidden status, typed offsets, and world bounds before mutating anything. Stages copies then commits all in one MapDoc beginEdit/touched. No partial writes on invalid input.
- `apps/studio-native/StudioHost.cpp`: when moving an EvolveArea center X/Z handle with multiple selected objects, submits the shared center delta for all selected IDs through the new batch RPC; single selection retains `studio.worldProcessAreaSet`. The batch RPC is classified as a document mutation.
- `apps/studio-native/StudioSession.cpp`: Undo label `Move EVOLVE region group`.
- No new ECS component. Runtime continues to hydrate authored offsets into persisted `evolve::Area` and TerraWeave consumes ECS extents.

## Limits
- This stage supports **group center translation on one axis**. Group edge resize, group preview rectangle, object-edge snapping, and editable numeric fields in Scene are not yet implemented.
- Every selected ID must be an eligible EVOLVE area; mixed selections are rejected rather than silently skipped.
- Source uses existing MapDoc/Studio APIs. Compilation and runtime behavior remain unverified.

## Verification
GitHub accepted source commits. No C++ compilation, Studio interaction, Undo/Redo/save/reopen, ECS tick, platform capture or CI executed; user deferred testing. Do not claim passing QA.

## Next
1. Add deterministic snapping to nearby EVOLVE edges and typed numeric Scene edits.
2. Add batch preview and optionally group resize with explicit pivot.
3. Check strict `Fixed::raw` coordinate quantization in authoring vs map hydration.
4. Run full native build and acceptance when user permits; keep PR draft until then.

## Commits
`ef4bdd78`, `602e1037`, `8c3a89be`, `46835427`.
