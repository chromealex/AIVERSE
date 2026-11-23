# AIVERSE World Processes Stage35 — Shift symmetric EVOLVE resize
Date: 2026-11-23
Branch: feature/world-processes-stage31-bidirectional-handles
Main unchanged. PR #112 draft.

## Implementation
- `apps/studio-native/StudioHost.h`: drag state stores `symmetric`; `worldProcessDrag` accepts optional symmetric modifier.
- `apps/studio-native/StudioHost.cpp`: Shift held during a Scene edge drag selects symmetric resize; plain drag preserves opposite edge. Host uses the existing pure `previewArea` to render the same mode while dragging. On release, symmetric mode sends the derived half-extent through the existing `studio.worldProcessAreaSet` direct-field RPC (no `edgeSide`); asymmetric mode sends `edgeSide` and absolute local edge as in Stage33. This preserves one MapDoc Undo operation. Active handle family is highlighted and the drag label shows the mode.
- No changes to authoritative ECS `evolve::Area` or TerraWeave forest integration; Studio preview remains transient.

## Interaction
LMB edge drag = move one edge with opposite edge fixed.
Shift+LMB during drag = resize symmetrically around area center. Shift can be toggled while dragging; the mode at release determines the committed operation.
Center handles still translate center without resizing.
Escape cancels drag without MapDoc writes.
Existing selection gestures and camera controls retain precedence on initial press.

## Verification
GitHub source commits only. No native compilation, Studio runtime interaction, real screenshots, Undo/Redo/save/reopen or gameplay acceptance was performed, as requested by user. Treat all code as unverified.

## Remaining
- Native compilation and runtime QA when authorized.
- Verify host label highlighting, selection collision, shift transitions and rounding against `WorldProcessesExtendedAuthoring`.
- Unify exact integer math of preview and RPC in a shared helper if discrepancies are found.
- PR #112 must stay draft until accepted.
## Provenance
Stage34 head `ac8cb3d1b9f8fb730cd7bab83070066771f9c2fa`.
Stage35 commits `eff7d767`, `3cf2e87b`, `dfe10480`.
