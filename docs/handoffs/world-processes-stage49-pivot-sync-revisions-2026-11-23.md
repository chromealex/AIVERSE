# AIVERSE World Processes Stage49 — pivot synchronization and preview revisions
Date: 2026-11-23
Branch: feature/world-processes-stage31-bidirectional-handles
Main untouched; draft PR #112 not merged.

## Implemented
- `apps/studio-native/StudioHost.h`: transient preview revision and selection signature.
- `apps/studio-native/StudioHost.cpp`: read-only `studio.worldProcessGroupPivot` returns active editor pivot X/Z in integer world cm and whether custom. After native group pivot drag, sends `worldProcess.pivotChanged` event with coordinates. The `studio.worldProcessAreaPreview` RPC now requires `revision` and `selectionKey`, ignores older revisions, rejects mismatched current selection, and projects only previews belonging to the current selection.
- `apps/studio-web/src/components/EvolveGroupNumeric.tsx`: on panel open/selection change, fetches native pivot and subscribes to pivot events. Numeric fields update from native drag. Preview requests carry monotonically increasing client revisions and selected ID signatures; clear requests advance revision to avoid stale numeric outlines after closing/typing.
- No new ECS or MapDoc pivot state. Stage42 atomic resize remains the only persistent edit.

## Known limitations
- Revision counter is per web module instance, not globally coordinated across multiple floating Studio windows. Native host revision is session-local. Cross-window arbitration remains future work.
- The numeric preview is still calculated separately from the batch commit path; exact integer math has not been unified.
- Numeric fields synchronize native pivot into web UI, but editing the web pivot fields does not move the Stage44 draggable native pivot marker until an actual group edit. It does update the numeric preview marker.
- No TS/C++ build or runtime verification; behavior remains source-only.

## Verification
GitHub accepted source commits. No TypeScript typecheck, Vite build, native C++ compile, CTest, actual Scene drag, Undo/Redo, screenshot, save/reopen or TerraWeave acceptance performed (user deferred testing). Do not claim tested.

## Next
1. Share one pure integer-centimetre transform for both preview and commit.
2. Reconcile multi-window revision namespace and preview ownership.
3. Add native marker update when numeric custom pivot changes, without writing MapDoc.
4. Build and run acceptance when authorized; keep PR #112 draft.

## Commits
`1273584f` native preview revision state
`e434f549` pivot RPC/event and revision validation
`02427946` React pivot sync and revisioned previews
