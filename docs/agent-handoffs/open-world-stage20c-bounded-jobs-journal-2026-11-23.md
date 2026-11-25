# W1.20c — bounded worker-local ECS change batches

Implementation `ddf4c1c9bd766e4d4cbc0a459477a999d7d6b3a7`, branch `feature/aiv20261123_open_world_audit`, PR #116 Draft.

`engine/core/ECS.h`: World::ComponentChangeBatch records sorted unique generational handles, capped at 65536 per batch. Overflow clears partial entries and latches until drain. Workers own independent batches and call record; after join, caller serially invokes mergeComponentChanges<T>. Union and overflow are independent of lane merge order. drainComponentChanges<T> returns entities and overflow; overflow requires a full reconciliation. Legacy takeComponentChanges throws on overflow without consuming it. World copy preserves batch state; clear resets it. No save schema changes.

Scheduler wiring is not implemented: no automatic parallel writer interception. Existing direct Pool and escaped-reference limitations remain. No native build or tests run per owner request. Next: inspect scheduler and integrate lane-local notifications at write barriers, then actor bootstrap/restore and dirty reconciliation with compatibility fallback. Update skill/catalog. Keep Draft.
