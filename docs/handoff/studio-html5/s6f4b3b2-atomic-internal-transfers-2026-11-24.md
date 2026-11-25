# S6f.4b.3b.2 — atomic internal Move/Rename recording (2026-11-24)

Owner Studio; branch feature/studio-html5. Base b7a496e3511c04593821ebead5739c8e5ab65d95. Staged internal transfer 7a48bb2f299f631d86b8618a044b435aeb28b8c0; native dispatch 7a48abfd789ea2059e06d59c90d1c82f2a74e394; endpoint guard ca3b565544751c5dcc49c40bf5254aa16a2a7c89; fixture 60928429fb85cda139e4478469e3e7f77a3aef99.

- Internal transfer resolves actual endpoints including Keep Both, captures file/tree and sidecar digests, computes indexed material slot remaps and preserves unrelated material document fields. Existing ordinary chain planner validates typed model URI edits and prepares stationary/moving resource snapshots. No live commit callback in staged mode.
- Native Studio uses the paired-cursor outer journal v4/v5 for internal and external transfers. Map/rules references at both actual endpoints remain fenced; unrecordable internal moves fail before publication. Index refresh and material change notification follow commit. Legacy direct transfer callers retain the previous path.
- Fixture covers first checkpoint with Move, material and glTF repair, exact undo, rejected stale material baseline, Rename and persisted cursor. Source and fixture committed; build, CTest, native/UI and actual fault-injection execution NOT RUN. No main merge.
- Remaining: explicit Trash restore/purge and batch cleanup still use legacy recording. General external writer concurrency and pre-staging catalog/Trash side effects remain outside the atomic publication guarantee.

Next S6h.3: finish explicit recovery operation durability and diagnostics; then S6i.3 MCP integration and comprehensive QA.
