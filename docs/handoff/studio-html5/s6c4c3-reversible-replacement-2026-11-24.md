# S6c.4c.3 — reversible asset replacement (2026-11-24)

Branch `feature/studio-html5`; base `4641ab64`; implementation `f04d68d1d3def54e33efa986f5daefeec6ce142a`; preview/UI fencing `377cf53ed74fcbfc3cd16d50908531b73c155a3a`.

- Replacement-only batches stage new bytes in project-local recovery storage, allocate outgoing slots and retain exact target sidecar baseline. Stream SHA-256 using the shared pack implementation; preserve target GUID, importer and settings by never moving its sidecar. Original picker source not required for Undo/Redo. Legacy explicit replacement callers also use the staged owner.
- Preview records source/target digests, target metadata digest and target GUID, advertises replacementAvailable only for eligible regular-file conflicts. Commit checks those identities again. No-op identical payload is not recorded. Mixed creation/replacement batches explicitly refused by native and UI; apply separately.
- Journal v3 adds two-operation replacement chains with digests, safe content/trash paths and distinct slots. Recovery checks all chains before writes and handles before-first-rename, between-renames and after-second-rename phases; rejects unexpected occupancy or payload mismatch. v2/v3 do not restore material documents. Existing move journal owner reused.
- Asset History dispatch handles replacement replay. Inactive old/new payload and record.json remain on disk after history eviction. Added test sources for stale preview, sidecar conflict, exact GUID/settings, deleted external source, redo, mixed refusal, three crash phases and altered payload recovery refusal.

Limits: session-local history, synchronous hashes, POSIX journal sync implementation, no external writer lock, no automatic disk quota/purge or cross-session trash UI. Mixed batches must be split. Recovery journal covers asset renames, not arbitrary project document writes. Native and web compilation/test execution, fault injection on actual filesystems, and Mac/KINGSTON verification not performed; no main merge.

Next S6c.4c.4: recovery browser for retained deletion/import/replacement records and explicit restore/purge policy with safe validation; S6c.5 durable project document journal; S6d agent integration.
