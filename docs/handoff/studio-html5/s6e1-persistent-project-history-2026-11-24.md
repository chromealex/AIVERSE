# S6e.1 — persistent Project undo/redo (2026-11-24)

Branch `feature/studio-html5`, base `c515edde`. Implementation `eb89fb44b4dc978a82285d9b0f2887730658d81d`.

- Saved Project timeline (Rules and compatible Manifest edits) and cursor stored in .aiverse-cache/project-history.json. Checksummed bounded payload, relative owner paths, exact dependency set and continuous byte transitions. Restore only if disk bytes match the retained cursor. New owner epoch on every reopen; old tokens invalid.
- Project save/undo/redo checkpoint and document edits publish in the same existing-file durable journal. Initial empty checkpoint is prepared under the same advisory lock; compare exact old checkpoint before publication. Untracked changes persist a cleared stack; no-op saves do not rewrite history. Existing project documents and recovery checkpoint remain independent of map state.
- Invalid/corrupt or externally stale checkpoint does not expose replay; warning visible in History panel. Tests cover reopen, redo branch, stale token, external changes, corrupt checkpoint and interrupted publication.
- No full compile or tests executed for this stage. Prior QA evidence applies only to earlier HEAD. CI will be retriggered. No main merge.

Next: S6e.2 persistent Assets timeline with on-disk recovery-slot validation and transaction-consistent checkpoints; S6e.3 Map timeline persistence; S6f cross-scope coordinated undo; expanded dependency coverage and recovery storage policy; final integrated QA. Existing owner and platform limitations remain; no claim of global atomic undo or fully audited MCP coverage.
