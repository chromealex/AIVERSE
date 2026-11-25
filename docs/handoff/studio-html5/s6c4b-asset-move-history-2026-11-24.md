# S6c.4b — bounded internal asset move history (2026-11-24)

Branch `feature/studio-html5`, base `34b8149a`, implementation `88453a36a558f9b63fc1a2cdced40fb2d210712c`.

- Existing transfer owner optionally captures actual resolved source/destination file+sidecar pairs, exact bytes (bounded), and material library before/after while the journal is pending. Record only after successful commit. Asset history shares its existing 64-entry/16 MiB payload budget with metadata entries; supports undo/redo across compatible metadata and moves. Folder/large/unsupported transfers remain usable but are history barriers. External imports are not replayed.
- Replay is native-only, exact version via existing assetHistoryStep. Validate regular files, non-symlink parents, GUID and importer from existing-only index, exact payload and metadata bytes, all destination collisions and material library baseline. Recheck known read-only rules references and conservatively refuse exact path strings in the current map/rules. Reuse StudioMoveJournal, exact material document write plan and index rebuild; attempt rollback on error. UI scope description updated; result exposes historyRecorded.
- Transfer contract source adds successful undo/redo with material bytes and GUID, occupied destination, changed asset and changed material conflicts.

Limits: regular files only, no folder tree replay, large assets (budget), no content creation/deletion, no explicit rename UI beyond existing move/keep-both. This is not a full reference graph: other saved maps, external tools, scripts, relative or encoded references and concurrent external writers are not covered. Existing journal's POSIX durability and recovery limitations remain. Exact-byte payload retention is intentionally conservative; a streaming digest/identity plan for large assets is future work. Native and frontend builds/tests not executed; no main merge.

Next: S6c.4b follow-up folder/rename and reference coverage, then S6c.4c reversible import/replace/delete with recoverable storage. S6c.5 durability and S6d agents remain.
