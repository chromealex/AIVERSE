# W1.20b — opt-in ECS component access journal

Implementation: `96ab87d826b68ebf464f2ce439b3a952a1338b10`, branch `feature/aiv20261123_open_world_audit`, PR #116 Draft.

## API
`World::trackComponentChanges<T>()` enables tracking; `notifyComponentChange<T>(entity)` records a packed generational handle; `takeComponentChanges<T>()` drains a sorted deduplicated set. World add/remove/destroy/get/tryGet/each/eachExisting conservatively record mutable access. Const reads do not. World copy copies the journal, clear resets it. No disk schema changes.

## Contract and limitations
This is a conservative access journal, not proof of actual value changes. Enable before writes. Single-threaded World API only: direct Pool access and parallel range writers must collect lane-local changes and report them serially after the barrier. Do not use tracked mutable World APIs concurrently. References retained across a drain can bypass tracking. Disk load must re-enable tracking and bootstrap via full scan. Journal memory grows with unique handles until drained. No actor integration yet; keep compatibility scan. No tests or native build run. Do not merge to main without acceptance.

## Next
Implement lane-local journal merge, bounded overflow/full-scan fallback, restore bootstrap and actor integration; update existing skill/catalog with final API contract. Audit direct pool writers before enabling dirty-only mode.
