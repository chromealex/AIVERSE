# AIVERSE Studio HTML5 — Stage 4: Save and history

Date: 2026-11-20
Branch: feature/studio-html5
Parent commit: 239b96e02f796b8fa927b24a09b30a2788bfc8d5
Stage commit: see branch history.

## Done
- Added studio.save / studio.undo / studio.redo to native StudioSession strict RPC allowlist.
- Save calls existing Workspace::call("map_save"), not a new file writer.
- Undo/Redo use existing MapDoc snapshot history.
- studio.snapshot reports canUndo/canRedo, so HTML5 controls reflect native state.
- React toolbar now includes Save, Undo, Redo; controls disable on invalid state.
- Mock preview simulates undo/redo locally, but refuses Save (clearly demo-only).
- Hardened entity:N parser against unsigned arithmetic overflow.
- Added negative native headless contract checks for Save/Undo/Redo without an open project/history.

## Changed files
apps/studio-native/StudioSession.cpp
apps/studio-native/tests/StudioSessionTests.cpp
apps/studio-web/src/types.ts
apps/studio-web/src/mock.ts
apps/studio-web/src/App.tsx
docs/handoff/studio-html5/04-save-history.md

## Tests
Test sources authored; native and TypeScript builds not run (GitHub connector-only runtime, no complete local checkout / macOS SDK).
Commands are in Stage 3 handoff: npm run typecheck && npm run build; cmake --build; ctest -R studio_session.
No screenshot or FPS measurements run.

## Limits
- Native Save requires a map already associated with a path. New unsaved maps need Save As + project picker.
- Undo/Redo are MapDoc-scoped; broader rules/asset editing transactions need coordinated history.
- Native studio events/subscription and live ECS not yet implemented.
- Scene rendering remains a placeholder. No ImGui has been deleted from the existing editor.
- Keyboard shortcuts, tests involving a real loaded project and visual regression still pending.

## Next
1. Compile and test native host on macOS; fix issues.
2. Save As, open project hub, stable IDs and state notifications.
3. Integrate native 3D Scene Render Target in HTML5 layout without CPU frame copying.
4. Port the rest of ImGui editors, then remove legacy editor and its ImGui dependencies only after parity.
