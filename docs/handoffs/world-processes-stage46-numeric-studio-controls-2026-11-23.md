# AIVERSE World Processes Stage46 — numeric EVOLVE group authoring in Studio web shell
Date: 2026-11-23
Branch: feature/world-processes-stage31-bidirectional-handles
Main untouched; PR #112 draft.

## Implemented
- `apps/studio-web/src/components/EvolveGroupNumeric.tsx`: compact React numeric authoring overlay for selected groups (2+ entities). Exact integer percent scale X/Z, pivot mode (shared/individual/custom), and world-space custom pivot X/Z in centimetres. Validates integer input and submits one `studio.worldProcessAreaBatchResize` call with `axis:"both"`, integer scale ratios and optional custom pivot.
- `apps/studio-web/src/App.tsx`: mounts the control in the connected project editor shell when multiple entities are selected; uses the existing `refresh` callback and error banner.
- `apps/studio-web/src/styles.css`: compact dark overlay styling.
- Existing C++ Stage42 batch RPC validates selected EVOLVE regions and commits one MapDoc Undo transaction. The numeric panel is intentionally hidden for single selection.

## Interaction
Select at least two EVOLVE regions; open the EVOLVE group scale overlay. Enter X/Z percentages (100 = unchanged), select pivot mode, optionally type custom pivot coordinates in cm, and Apply. Native session rejects mixed/non-EVOLVE or locked selections atomically. The control does not create a new ECS authoring authority.

## Limits
- Numeric input is a compact overlay in the Studio shell, not yet a dockable Scene toolbar widget.
- Custom pivot fields initialize at zero, not from the Stage44 transient native pivot. They are explicit world coordinates and must be set by the user.
- The native drag preview does not reflect numeric fields while typing; applying refreshes the snapshot.
- Two-axis corner gizmo is not implemented. A user can perform exact two-axis scaling via the panel.
- The panel is mounted only in the main Studio editor return path; floating-only windows do not independently show it.

## Verification
GitHub accepted source commits. No TypeScript typecheck, Vite build, C++ compile, native runtime, screenshot, Undo/Redo or CI run (user deferred testing). Source-only; not QA-verified.

## Next
1. Share transient pivot state between native Scene and numeric panel.
2. Add a real two-axis corner manipulator and numeric live preview.
3. Verify bridge reply semantics and exact numeric validation with TypeScript/C++ builds when authorized.
4. Keep PR #112 draft until acceptance.

## Commits
`1bb12883` React numeric panel
`d5302373` Studio shell integration
`4cd26b87` overlay styling
