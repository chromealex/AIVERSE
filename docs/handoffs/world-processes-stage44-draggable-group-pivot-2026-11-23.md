# AIVERSE World Processes Stage44 — draggable group pivot (source-only)
Date: 2026-11-23
Branch: feature/world-processes-stage31-bidirectional-handles
Main untouched; PR #112 draft.

## Implemented
- `apps/studio-native/StudioWorldProcessesEditing.h`: groupCentroid computes mean of stable selected EvolveArea centers; GroupPivotX/GroupPivotZ modes appended to existing enum values.
- `apps/studio-native/StudioHost.h`: transient editor-only pivot state and selected-ID snapshot. Changes of selection reset the custom pivot.
- `apps/studio-native/StudioHost.cpp`: draws pivot marker and numeric centimetre coordinates; group selection exposes X/Z draggable handles. Releasing a pivot drag updates only transient editor state. During subsequent group edge resize, the existing Stage42 atomic batch RPC receives `pivot:"custom"` with X/Z coordinates; Shift still selects individual pivots.

## Ownership
No new persistent ECS component or MapDoc pivot field. The custom pivot is a Studio editing affordance; ECS `evolve::Area` remains authoritative after map hydration. Only group resize commits through existing MapDoc RPC.

## Incomplete
- **Editable numeric Scene fields for scale X/Z and pivot coordinates are not implemented.** Pivot coordinates are displayed as read-only labels.
- **Simultaneous two-axis drag in Scene is not implemented.** The Stage42 RPC supports independent X/Z numeric scales.
- Stage41/43 previews have not yet been consolidated; their displayed pivot may differ from the new custom pivot.
- Snap/hover behavior for pivot handles, group eligibility and exact rounding need real runtime QA.

## Actual verification
GitHub accepted source commits only. No C++ compile, CTest, native Studio interaction, screenshots, Undo/Redo, persistence or gameplay acceptance was run (user deferred tests). Do not claim feature acceptance.

## Next
1. Add actual editable numeric Scene controls for pivot X/Z and scale X/Z, backed by the Stage42 RPC.
2. Consolidate preview math and overlays to reflect custom pivot exactly.
3. Verify build and behavior when authorized, keep PR #112 draft.

## Commits
`f15d41bf` group centroid
`b934a25f` transient pivot state
`56f65340` draggable handles
`7361b230` group resize custom pivot wiring
