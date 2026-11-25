# S6b — Stroke ownership and agent history (2026-11-24)

Branch `feature/studio-html5`. Base `443d7c03ae3ca763d752e057c93313c6c71dc8d4`; implementation `6fdc0a179361b2502c1827bf675f82db5ce5b357`.

## Changes
- StudioSession.h/.cpp: one RAII MapDoc CompoundEdit held from Scene stroke begin to release. Dabs still use existing Workspace operations, no intermediate history entries; commit one before-image. Invalid coordinate, operation refusal, cancellation and map/project switches roll back. Reject concurrent authoring dispatches except explicit observational/status requests; linked agent calls return retryable stroke-active error. Save As and spline edits refuse during an active stroke. Cap path points at 4096; existing Caverns cap retained.
- StudioHost.cpp: cancel on Escape/disarm, Scene detach/hide/failure, replacement pointer press and project switch; notify document change after cancellation. No alternate renderer or brush implementation.
- StudioSession.cpp/.h, AgentPanel.tsx, types.ts: record exact native history entry IDs for agent edits; resolve positions from retained undo/redo stacks; send expectedVersion on navigation. Undo all available only when original retained prefix and all recorded agent edit IDs exactly match current undo stack and no known non-undoable changes or source switch occurred. Individual links navigate global map history, not selective undo.
- HistoryPanel.tsx: stroke-active status disables history jumps; StudioSessionTests.cpp: checks no intermediate undo, concurrent undo refusal, explicit cancellation and invalid coordinate rollback with redo preserved. Skill updated.

## Boundaries and next
Map transaction ownership is Studio main-thread only; external process filesystem edits and unrelated owner APIs outside the guarded Studio paths are not covered. Actual Caverns backend availability and per-dab Workspace owner rollback semantics still require integrated validation. Existing MapData rollback does not reverse arbitrary side effects, logs or file writes. Agent history does not promise selective undo of mixed changes. History still map-scoped.

Audited project/file owners: Workspace rules_set/merge/delete immediately validate and save composed rules through Project::saveRules; manifest writes and asset metadata/import/move commit use distinct owners. StudioMoveJournal provides crash recovery for internal content moves, not user Undo after successful commit. S6c must reuse these owners with explicit reversible plans, exact before/after file identity checks, conflict refusal and rollback-safe multi-file persistence; do not implement by replaying arbitrary JSON or blindly restoring files. Next implement bounded rules/manifest history with saved-file validation, then asset operations. S6 not globally complete.

## Verification
Remote commit and touched files read back; C++ build, StudioSession tests, native MCP concurrency, npm typecheck/build/test:web, actual Terrain/Caverns strokes, screenshots, performance and Mac/KINGSTON gate NOT RUN (no native checkout). No main merge or production readiness claim. Roadmap remains in-progress. Acceptance: release at cap, cancellation with existing redo, failed later dab, path refusal, Escape, detach/hide, map switch, agent retry, stale history version, evicted IDs, interleaved manual edits and agent-only suffix.
