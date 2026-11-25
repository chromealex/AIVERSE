# S6c.4a — asset metadata history (2026-11-24)

Branch `feature/studio-html5`, base `b7c73204`. Native implementation `3d4ad40fcd499d7771dce1dad6a9e860281e9ff2`; web integration `d28502456fd5c0caea4ce0305715f7ad955ed0c9`.

## Implemented
- StudioAssetCatalog retains validated exact before/after sidecar plans for importer settings; unchanged setting saves preserve raw bytes and redo. Uses shared ProjectFileEdits instead of a fixed temporary filename. Replay validates unchanged GUID, importer, all non-settings metadata fields and indexed physical path; refuses missing files, symlinks, duplicate GUIDs and external sidecar changes. Existing-only catalog scan does not generate unrelated sidecars. SQLite reindexed after write; in-process rollback attempted if reconciliation fails. Save staging directories excluded from asset enumeration.
- Separate bounded asset metadata history in StudioSession, reset on project reopen or transfer attempt (conservative barrier, including failed transfer). Explicit native assetHistory/assetHistoryStep with expectedVersion and stroke guard. No direct arbitrary path writes. Agent map undo remains unsafe for uncovered edits.
- HistoryPanel Assets scope with typed bridge, explicit single-step navigation, budget/status/conflicts and conservative demo mock. Host emits assets.changed and document.changed; Assets and pinned Import Settings reread.
- Contract sources for exact-byte undo/redo, no-op, external modification, missing unrelated sidecar, moved path refusal, stale version, session reopen and web scope fencing.

## Limits
Metadata only, not asset payload identity checks beyond GUID/path/importer and existence. No import/move/delete undo, no durable crash atomicity or external-writer lock. Existing-only full index scan has not been profiled. Index is disposable; rollback failure is reported, not hidden. Not a unified map/project/assets history; asset changes by other owners are not automatically recorded. Native bridge only, no new standalone MCP mutation endpoint. Actual asset import/pack/GPU compression is not replayed.

## Next
S6c.4b — audited internal move plans with reversible source/destination asset + sidecar and affected reference documents, exact file identities and no-overwrite conflict checks. Reuse existing transfer/journal/material owners; support only proven safe move subsets first. Then S6c.4c import/replace/deletion semantics and S6c.5 durable recovery. Preserve genre independence.

## Verification
Remote commits read back. Added test sources, not executed. C++ build, native catalog/session tests, web typecheck/build/test:web, fault injection, Windows/macOS filesystem behavior, WKWebView screenshots and Mac/KINGSTON acceptance not run. No main merge.
