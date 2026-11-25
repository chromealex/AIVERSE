# S6c.5 — project document crash recovery (2026-11-24)

Branch `feature/studio-html5`; base `1bf399bb`; implementation `c49eeb366171ee6a9b79975da5d4dd0bceef18be`; isolation/performance `923f2473589aa3e0f53eedf0f078a3d924d4a367`.

- Shared ProjectFileEdits owner now stages exact before/after bytes and permissions, including unchanged dependencies, in a bounded project-local journal. Immutable snapshots verified by shared SHA-256. Stable advisory lock prevents cooperating writers/recovery from processing the same active journal. Publish after durable pending marker, replace each file and sync parent directory; commit marker retires pending state. No-op validates baseline without snapshot publication.
- On project open, pending journal validates all paths and snapshots and current bytes before rolling back any changed target; unknown bytes, corrupt backup, permission/path mismatch refuse recovery, retaining journal. Committed marker is not rolled back. Recovery is idempotent across partial rollback. Project::load covers Workspace/MCP and Studio; Studio resolves inner document journal before outer move journal.
- Platform implementation isolated in ProjectDocumentJournal.h and ProjectFileEdits.cpp; linked through existing editor core. POSIX fsync/F_FULLFSYNC; Windows file FlushFileBuffers + MoveFileExW WRITE_THROUGH, directory durability not established. Publication snapshots loaded once, not on every file.
- Project and asset history tokens include per-owner epoch, not just local counter, to fence restart reuse.
- Added contract sources: every publication boundary, no-op/unchanged dependency, unknown external bytes refusal, corrupt backup refusal, owner load recovery, committed marker cleanup, reverse transaction and epoch.

Limits: only existing-file owner operations using ProjectFileEdits, not all legacy direct writers, file creation/deletion, arbitrary multi-owner atomic save, or full durable user history. POSIX and Windows behavior not verified; directory rename across mounts not supported; no lock against non-cooperating writers, nor unified lock with legacy asset journal. Prior .aiverse-save-* orphan stages are not auto-recovered. Recovery conflict requires manual inspection; no repair UI yet. Full native/web builds, crash/power-loss injection, Mac/KINGSTON QA not performed. No main merge.

Next S6d: typed agent change links to owner-native map/project/asset histories, conservative scope and exact version/prefix/suffix checks. Avoid claiming combined atomic undo until a coordinator exists. Then integrated QA and hardening.
