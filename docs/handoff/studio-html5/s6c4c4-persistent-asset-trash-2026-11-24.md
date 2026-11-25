# S6c.4c.4 — persistent asset Trash and versions (2026-11-24)

Branch `feature/studio-html5`; base `b47568e0`; native `41ad8b8c2ee277090fca3ab1e47553e42d713e97`; UI `59867d227624cfe95757c5586d7c77b8e87a3253`.

- Native recovery list scans project-local recovery slots, paged up to 100 items, bounded to 10k listed records; empty slots excluded, malformed payload records shown invalid. Review revalidates payload digest, root sidecar identity, safe paths and slot contents. New deletion/creation records include root sidecar digest. Legacy records have no original sidecar digest.
- Restore deletion/undone creation through journaled quarantine replay at original path; path/GUID collision refusal. Restore an inactive replacement version against exact live GUID/path, preserve current metadata, retain outgoing active version in a new record; use v3 replacement journal.
- Action token binds record bytes, current sidecar and live payload identity where applicable; native apply requires exact project/history version and confirmation. Pending journal blocks mutations. Explicit purge removes only one reviewed recovery slot, not live content; no automatic purge. Restore/purge invalidate session-local asset history and pending transfer previews. Purge is irreversible and not crash-atomic; no bulk purge.
- HTML5 Assets toolbar Trash dialog uses shared controls, paged list, filter on current page, previous-version label, file byte count, invalid record error, explicit confirmation and refresh. Folder size is not enumerated in list. No persistent undo timeline.
- Contract sources: independent listing after reset, stale token, delete restore, replacement target change refusal and previous version restore, outgoing version purge, browser demo refusal.

Limits: no disk quota/automatic retention, invalid or partial staging records require manual inspection, no full recursive storage accounting, no external writer synchronization, POSIX journal constraints. Cross-session restore is explicit, not a continuation of prior Undo/Redo. Purge does not provide undo. Native/Web compilation and tests not run; no visual acceptance or main merge.

Next S6c.5: durable multi-file project document recovery and restart fencing, followed by S6d agent integration. Revisit full cross-platform build/QA, reference coverage and disk retention policy during acceptance.
