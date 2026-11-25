# AIVERSE World Processes Stage51 — preview lifecycle and validation alignment
Date: 2026-11-23
Branch: feature/world-processes-stage31-bidirectional-handles
Main unchanged. Draft PR #112 remains unmerged.

## Implemented
- `apps/studio-web/src/components/EvolveGroupNumeric.tsx`: repaired malformed React preview effect left by Stage49 (stray closing brace and duplicate clear); consolidated native bridge import and ensured invalid numeric inputs send owner-scoped revisioned clear requests. This is a source-level syntax correction, not a claimed build pass.
- `apps/studio-native/StudioHost.cpp`: an active preview owner may release its overlay even if selection changed, but a different owner cannot release it. Explicit clear releases preview owner/selection state. Selection changes also invalidate stale numeric overlays and reset transient pivot.
- `apps/studio-native/StudioWorldProcessesEditing.h`: numeric group preview now validates EvolveArea using the same typed `lib::worldprocess::extended::validate` as the batch commit. Disabled-but-authored areas are no longer silently rejected by preview while the commit accepts them.

## Remaining limitations
- Preview owner arbitration is still last-active-writer, not multi-window compositing.
- Native host and React effects can race during simultaneous selection changes; verify with actual floating windows.
- Group preview still does not share the entire validation transaction with MapDoc commit; the pure scale axis arithmetic was shared in Stage50.
- There may be other integration or compile issues from earlier untested stages; do not merge before a real build.

## Actual verification
GitHub accepted source commits. No TypeScript typecheck, Vite build, C++ compilation, CTest, native runtime Scene drag, screenshots, Undo/Redo, save/reopen or ECS/TerraWeave acceptance was performed, as tests are deferred.

## Next
1. Add explicit window-close release and selection event handling across all native/HTML5 shells.
2. Consolidate all region eligibility checks and bounds in a shared pure function.
3. Run targeted C++/TS build and runtime acceptance once user authorizes testing; keep draft PR #112 unmerged.

## Commits
`e3da8f66` React effect syntax and scoped clear
`fe26d4b1` native lifecycle cleanup
`d40946e7` typed preview validation
