# AIVERSE World Processes Stage47 — two-axis corner gizmo
Date: 2026-11-23
Branch: feature/world-processes-stage31-bidirectional-handles
Main unchanged; draft PR #112 not merged.

## Implemented
- `apps/studio-native/StudioWorldProcessesEditing.h`: appended `Handle::Mode::GroupCornerXZ` (preserving existing enum ordinals); extent helper returns the pointer's X coordinate in owner-local centimetres.
- `apps/studio-native/StudioHost.h`: drag scratch now stores `valueZ`.
- `apps/studio-native/StudioHost.cpp`: exposes the positive-X/positive-Z corner handle only for a selected EVOLVE group. On drag, computes both X and Z scale ratios from the active area, supports 1m Ctrl/Command grid snapping of both coordinates, and renders transient amber candidate rectangles plus scale percentages. On release, calls the existing atomic `studio.worldProcessAreaBatchResize` with `axis:"both"`, independent rational X/Z ratios and the current pivot mode (custom, shared or individual).
- No new ECS component or second source of truth; MapDoc writes remain in StudioSession RPC.

## Interaction
Select 2+ EVOLVE areas. Drag the active area's +X/+Z corner to scale both axes. Shift uses individual pivots; otherwise the shared centroid or transient custom pivot applies. Ctrl/Command snaps both pointer axes to 1m grid. Invalid sizes do not commit. Native session validates the full group before one Undo edit.

## Outstanding
- Stage46 numeric inputs do **not yet drive the native Scene preview while typing**. They can submit exact two-axis values through the RPC, and the new corner gizmo has its own live Scene preview.
- Group corner preview currently assumes a shared/custom pivot even when Shift selects individual pivots; preview and commit parity must be reconciled.
- Group corner positive-quadrant behavior and coordinate rounding are source-only and require actual Studio interaction verification.
- Native numeric panel and preview may need a shared event/state channel; no such bridge is claimed here.

## Actual verification
GitHub accepted source commits. No C++/TS build, runtime Scene interaction, CTest, Undo/Redo, screenshot, persistence, or gameplay QA performed (user deferred tests). Do not claim runtime success.

## Next
1. Wire Stage46 numeric field changes into native read-only Scene preview state and synchronize custom pivot.
2. Fix individual-pivot two-axis preview parity with the batch RPC and validate corner bounds.
3. Compile and run native Studio + web UI acceptance when authorized; keep PR #112 draft.

## Commits
`06113409` corner mode
`c1100c0c` second drag axis
`1ee78546` atomic two-axis commit
`55011040` live group preview
`fcd75a18` group-only handle
`e88563ad` group hit-testing
