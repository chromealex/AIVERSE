# S6c.4c.2 — staged import and folder creation history (2026-11-24)

Branch `feature/studio-html5`; base `af5c5d73`; implementation `4b9298aeb9b9629e734233a9c26b76ba68d43c2c`.

- Non-replacing external imports using the Studio history route stage all selected assets outside live content. Existing catalog creates sidecars in private staging projects at final filenames, preserves explicitly eligible source GUIDs, and validates final publication through the live catalog. Folder trees get fresh sidecars. Payload/metadata flushed before journal v2 publication. Batch preflight validates selected decisions, destinations and root GUID collisions. Reconciliation rejects duplicate GUIDs, with journal rollback attempted.
- History created=true distinguishes import/create from delete. Undo moves the imported batch to recovery slots, checks known dependencies first; Redo restores exact saved files/folders and GUIDs, independent of original picker paths. Folder creation uses the same staged owner. Asset history scope and host refresh reused.
- Added contract scenarios for multi-asset import, original source removed before Redo, whole-batch collision refusal, reference refusal, nested GUIDs, empty directories, exact metadata and folder creation round trip.

Limits: explicit replacement (including mixed batches containing replacement) still uses existing non-reversible route and resets history; legacy callers without history retain existing behavior. No persistent history across restart, automatic orphan cleanup, disk quota or trash browser. Failed staging/publication can leave recoverable orphan slots. POSIX journal and filesystem race limitations remain; not a complete dependency resolver. Full native/web builds and tests not executed; no main merge.

Next S6c.4c.3: reversible replacement retaining target GUID and old/new payload, followed by trash browser/cross-session recovery, durable document journal and agent integration.
