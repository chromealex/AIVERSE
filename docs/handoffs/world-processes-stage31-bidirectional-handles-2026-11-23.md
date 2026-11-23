# AIVERSE World Processes — Stage 31: bidirectional EVOLVE area handles

**Date:** 2026-11-23
**Base commit:** `e3e83f54c6caf6a6d95209bd7d397dc8e212c6a2`
**Branch:** `feature/world-processes-stage31-bidirectional-handles`
**Main:** untouched; original PR #112 remains draft.
**Verification:** source inspection only. Native compile, CTest, Studio runtime, Undo/Redo and snapshots NOT run; user deferred testing.

## Changes
- `apps/studio-native/StudioWorldProcessesEditing.h`: adds `AreaWidthNegative` and `AreaDepthNegative` modes **appended** to existing Mode values, retaining the previous mode ordinals.
- `handles()`: authoring `EvolveArea` now exposes +X, +Z, -X, -Z drag handles. All reference the same existing `halfWidthCm`/`halfDepthCm` properties and actual rectangular ECS area.
- `extent()`: both negative and positive variants calculate the same bounded 50–50000 cm half-extents from map entity center. No new gameplay ECS state, schema, or migration.
- Native `StudioHost` already carries mode through drag and forwards component/key/value on mouse-up via `studio.worldProcessAreaSet`. Thus no new RPC/Undo implementation needed for the added modes.

## Contract and constraints
- Opposing edge drag remains **symmetric about the owning map entity**. Dragging -X adjusts ±X equally; it does NOT move the center or hold the opposite edge fixed.
- Existing authored `pos` remains the EVOLVE area center. No offsets have been introduced.
- All four modes write only the half-extent in the same MapDoc edit; repeated-click overlapping handle cycling still uses `hitCandidates`.
- Only entities with `EvolveArea` use these controls; legacy `EvolveRegion` radius remains an editor-only preview.

## Changed files
- `apps/studio-native/StudioWorldProcessesEditing.h`
- `skills/world/aiverse-evolve/SKILL.md`
- `skills/world/aiverse-world-processes/SKILL.md`
- This handoff.

## Explicit future work
1. Decide and implement pivot-preserving asymmetric side resize and typed area center offset in MapDoc + validation + ECS hydration, without a second area authority. Existing symmetric modes should remain available.
2. Verify Studio hit selection, four-direction resize, RPC, Undo/Redo, save/reopen and real TerraWeave forest climate integration in a native build when authorized.
3. Merge only after real CI/native acceptance and draft PR review.

## Provenance
Base SHA `e3e83f54c6caf6a6d95209bd7d397dc8e212c6a2`.
Initial Stage31 code commit `43f5d551afcf6c3d50df202341a11ac523cd6459`.
Retrieve the final handoff commit from `git log -1 -- docs/handoffs/world-processes-stage31-bidirectional-handles-2026-11-23.md`.
