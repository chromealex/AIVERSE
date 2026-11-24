# S6c.2 — bounded project document history (2026-11-24)

Branch `feature/studio-html5`; S6c.1 base `5c54c11d`; implementation `1101505aac4514844b0dce6b4091c4a30a7e7f6c`.

- ProjectHistory retains owner-generated before/after plans for up to 64 edits and 16 MiB estimated retained payload; oldest entries evicted, new edit drops redo, no-op preserves redo, oversized/untracked write resets history. Load resets version and stack. Separate from MapDoc.
- Label explicit Rules set/merge/delete and compatible Manifest edits at existing save owner. Changes to map list form a history barrier; changes to rules source/module require reopen. Other changed saves are barriers rather than gaps.
- Undo/redo requires exact project/version, clean in-memory state and matching saved baselines; candidate manifest and all composed rules are validated using existing loaders before publication. Exact original bytes restored by the shared S6c.1 plan. No arbitrary request-supplied paths or JSON replay.
- Workspace MCP `project_history_status` and `project_history_step`; native `studio.projectHistory` and `studio.projectHistoryStep` (one step; explicit direction/version). Native stroke guard blocks mutation and host refreshes the document. Agent map history stays conservative.
- Added native contract scenarios for combined rules/manifest undo/redo, stale version, external dependency changes, exact bytes, independent map revision/history, no-op, redo truncation, barrier, reopen, native route and memory/count limits.

Limits: not a single unified chronological map/project timeline; no multi-step atomic jump, asset operation history, durable crash recovery, external writer synchronization, or complete agent history. Direct owner callers using unlabelled saves deliberately invalidate project history; external writes through other owners may be refused at navigation if they modify a recorded dependency. Project history is session-local. Retained payload estimate excludes allocator overhead and temporary composition allocations.

Next S6c.3: explicit Map / Project scope in HistoryPanel, typed bridge contract, Undo/Redo buttons with expectedVersion and visible blocked/conflict state; keep map toolbar semantics unchanged and refresh all dependent panels. Then S6c.4 metadata/content operations, S6c.5 durable recovery. No main merge.

Verification: remote implementation diff read back. Tests added but not executed; native build, MCP invocation, frontend build and Mac/KINGSTON acceptance not run.
