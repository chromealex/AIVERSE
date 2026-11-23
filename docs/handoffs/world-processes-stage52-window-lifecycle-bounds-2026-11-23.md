# AIVERSE World Processes Stage52 — window lifecycle and shared bounds
Date: 2026-11-23
Branch: feature/world-processes-stage31-bidirectional-handles
Main unchanged; draft PR #112 not merged.

## Implemented
- `apps/studio-web/src/components/EvolveGroupNumeric.tsx`: observes document visibility, releases its owner-scoped numeric preview on pagehide/unmount, and suppresses preview while the document is hidden. Returning to a visible window refreshes preview through the existing revisioned effect. Release requests carry current selection signature and monotonically increasing revision.
- `libraries/include/aiverselib/world/EvolveAreaScaleMath.h`: added pure integer `withinBounds(center,half,maxCoordinate,minHalf,maxHalf)` to prevent overflow-prone addition and subtraction and standardize range validation.
- `apps/studio-native/StudioWorldProcessesEditing.h` and `apps/studio-native/StudioSession.cpp`: numeric two-axis preview and batch commit now share `withinBounds`, complementing Stage50 shared axis scale math. Typed EvolveArea schema checks remain in both paths.
- No additional ECS authority, MapDoc schema or simulation change.

## Limitations
- Floating windows may remain document-visible when merely unfocused; focus/blur arbitration is not implemented. Ownership remains last active window, and pagehide handles actual close.
- This stage shares geometry and world bounds for numeric two-axis operations, not every legacy single-edge edit or mixed-selection eligibility rule.
- User-facing numeric UI still does not expose all advanced editing options as dockable tools.
- No TypeScript build, C++ compile, Studio runtime, screenshots, Undo/Redo or TerraWeave acceptance run, per user's deferred-testing instruction. GitHub source commits are not QA evidence.

## Next
1. Audit remaining single-area edge and group center validation for consistent typed rules.
2. Add explicit preview release on native floating-window destruction where pagehide is not delivered.
3. When user authorizes QA, run TS typecheck, native C++ build and targeted Studio acceptance before considering main merge.

## Commits
`25fee255` visibility and close lifecycle
`68648530` shared bounds helper
`539b79b8` preview bounds
`a51e37ad` batch commit bounds
