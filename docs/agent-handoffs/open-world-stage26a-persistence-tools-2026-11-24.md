# W1.26a — World persistence operator tools / Studio bridge

Area W1, branch `feature/aiv20261123_open_world_audit`, Draft PR #116. Base 359d4e8222fb80315b2ccddff97485b373782663.

## Implementation
- 8a9f696c: roadmap in-progress entry.
- 71adc065: shared engine/world/WorldPersistenceDiagnostics.h, exact 64-bit decimal string identifiers, ledger encoded bytes, live/materialized regional counts, bounded delta/immutable-revision bootstrap pages.
- 72dcd694: ActorPersistentSession exposes const durable state/manifest and quiescent backup catalog, retention preview/confirmed apply, independent fork; uses existing verified session APIs and codecs.
- ce16b574, 9d105541: apps/editor/core/WorldPersistenceOps.cpp, nine genre-neutral Workspace operations for project-scoped offline save management. Uses saved project content and the server's content identity, verifies external assets, confines directories to project/world-saves/<uuid>, refuses symlinks/nonregular entries, holds exclusive lifetime lock. No arbitrary filesystem path or automatic deletion. Retention requires explicit exact generation list.
- 4e81986a, 1e3f0535: Workspace registration and CMake target wiring for both standalone MCP and Studio agent link.
- 5d8d4817: native StudioTools.h allowlist group world_persistence, shared studio.op backend. No React/dock panel change in this feature branch; follow mandated feature/studio-html5 branch.
- afbf4578: legacy editor world_regions_status reads actual preview/play ECS ledger, not a durable server state or a collision residency claim.
- 2a3fba6d: source regression for >2^53 exact IDs, counts and stale cursor bootstrap.
- 6d95d25c: persistent-world skill updated.

## Verification
Native compile/CTest NOT RUN. MCP stdio smoke test NOT RUN. Studio typecheck/build/screenshots NOT RUN. No main merge, no success devlog. Roadmap remains in-progress.

## Limits / integration
Offline calls synchronously reconstruct a verified world and use exclusive directory ownership. Running server refuses offline access; no remote admin connection or server stop. Status bytes are ledger archive size, not process memory. Regions show ownership/materialization, not loaded collision cells. Existing saved actor-project format supported; other host codecs must be explicitly implemented. Filesystem safety assumes cooperative private local directory, not concurrent hostile path replacement. UI allowlist only; actual React panel registration and background operator job progress/cancellation still outstanding. Older consumers of pageSince must honor resetRequired; new tool does. The new ops do not manipulate preview state or simulate ticks. Test actual linking and project content compatibility before acceptance.

## Next
W1.26b: integrate dedicated World Persistence panel on feature/studio-html5 using approved UI kit, existing native group and typed bridge; add background operator job lifecycle/progress/cancel for offline recovery and maintenance, safe read-only diagnostics for connected authority. Then MCP smoke, native and Mac acceptance. Keep feature work on designated branches; do not merge main before gate.
