# S6f.4b.2 — atomic Map history checkpoint (2026-11-24)

Owner Studio; branch feature/studio-html5. Base 1f372bca717aea9e29ac77397bf6fe15dbce4ccb (grouped Window menu retained). Map checkpoint 56c844260595a8f32ba5f9e0a37a4930f6761898; Studio integration e38250bbe4935354056ec4f60223218eba7e3602; baseline guard b0b6b71b54ed0cf17c68bfd245dae426ecef720f; fixture 9c14d3271f22fcb4e13f8441ab321b1c5075907d.

- Restore deferred baseline/checkpoint foundation. Loader records actual map and read sidecar SHA-256 values. Staged save rejects missing or externally modified baseline files, unrelated occupied outputs and changes to removed dependencies. Removed sidecars are not deleted.
- Studio Save/Save As holds per-map lock and stages map history v2 exact-file fingerprint and Undo/Redo snapshots in the same creation-aware journal as map/terrain/Project documents. First checkpoint creation participates in rollback. No legacy dirty marker is created by this path. Legacy interrupted-save markers refuse atomic save; v1 history remains readable.
- Added fixture for first checkpoint creation and update at each publication boundary, reopened Undo/Redo and label, external map change refusal. Code and fixtures committed, NOT BUILT OR RUN. No actual fault injection, frontend/native QA or main merge.
- Limitations: legacy standalone map_save remains separate; map checkpoints are bounded by existing snapshot budgets. The projected file set is derived from serialization outputs, and reopen validates it against actual loader reads; unusual extra-field/sidecar combinations may refuse restore and require fixtures. Legacy interrupted marker resolution remains separate. Exact checkpoint concurrent same-map-byte history changes are not independently tracked across sessions.

Next S6f.4b.3: integrate Asset checkpoint into outer history journal and eliminate post-commit dirty-marker window; then S6h.3 recovery and S6i.3 MCP. 
