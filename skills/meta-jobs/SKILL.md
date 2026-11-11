---
name: meta-jobs
description: Configure AIVERSE crafting construction queues timed slots speedups cancellation using the existing aiverse::meta service. Use when adding or changing these mechanics in a game's native C++ meta layer.
---

1. Inspect existing project `meta/extensions.json`; reuse `extensions.liveops.jobs`. Do not regenerate algorithms.
2. Run `python3 tools/meta/catalog.py describe jobs` for only this schema and example. Read `docs/meta-liveops.md` for authority, persistence or action semantics when needed.
3. Configure IDs, bounds and rewards; reuse economy assets. Keep presentation labels localized in metadata where the schema permits it.
4. Call `Service::execute` with service `jobs`, action `start, claim, cancel, speed`, stable operation ID, and arguments such as `{"id":"forge","task":"job1"}`. Select one action per request. Read the action table for its exact arguments.
5. Jobs snapshot cost/reward/refund and reserve fixed slots. Speed only running jobs; cancelling or accelerating never shifts existing reservations. Completed unclaimed jobs count against maxQueued.
6. Link `aiverse::meta` (registered native modules use `aiverse_native_use_meta`). Host supplies identity/time/authority. Preserve the same operation ID on retries; Commit must atomically persist the whole snapshot.
7. Run `tools/meta/compose.py --project PROJECT --validator PATH_TO_aiverse_meta_validate`, then `tools/meta/catalog.py pin --project PROJECT` for a new unpinned project or `verify --project PROJECT` for an existing pin. Never replace a differing pin without reviewing the upgrade. For implementation changes run `tests/meta` including native consumer and LiveOps tests.
8. Report what was configured and integrated. JSON alone does not deploy a server or install a screen.
