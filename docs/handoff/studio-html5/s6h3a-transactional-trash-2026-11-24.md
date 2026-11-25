# S6h.3a — transactional Trash restore and protected cleanup (2026-11-24)

Owner Studio; branch feature/studio-html5. Base 6dd85fe150b52db6a5dc5f779c4e16fd8ccee573. Native staging 029929c90265fd6dbf61913ec93ce54bb118292a; dispatch 54fefb06b4b049580f0b0eb63d7b9cf8fe59d787; fixture 07aa10c836c4e0b1cd92fe76e0295436c933a9f9.

- Explicit Trash restore stages quarantine/replacement operations and atomically publishes the cleared Asset history cursor. Single and batch purge require an exact persistent baseline and refuse slots referenced by retained Undo/Redo. Unprotected cleanup preserves history; irreversible purge can stop partway and is not an atomic batch.
- Trash UI disables protected purge and displays specific pending journal/legacy marker reasons. Individual repaired Move/Rename replay now also includes its material snapshots.
- Added fixture for protected purge rejection, unrelated purge retaining checkpoint, and transactional restore. Source and fixture committed; build, CTest, native/UI and fault-injection execution NOT RUN. No main merge.
- Remaining: legacy dirty-marker recovery/reset workflow, interrupted cleanup diagnostics and full MCP discovery/verification. Stale history remains fail-closed; no automatic purge or checkpoint discard. Direct legacy C++ APIs remain available.

Next S6h.3b: explicit reviewed legacy checkpoint reset/recovery workflow and detailed diagnostics; then S6i.3 MCP and comprehensive QA.
