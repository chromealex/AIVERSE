# Stage 23V.2 — common native world registry and presentation
Date: 2026-11-24. Branch target: feature/aiv20261123_building-grammar / PR #114.
Parent: cd26d4437db1cfb01c2f988f0b615c83be8bfd09 (23V.1 shared Simulation/multi-scene foundation).

## Implementation
- New engine/buildings/BuildingWorldRuntime.h: single shared registry for up to 256 immutable scene definitions, automatic post-PHYSIS setup, shared fixed tick, exclusive SceneCollisionNamespaces leases, local static shell and paired dynamic collision IDs, load/unload at barriers, graph-domain owner registration with synchronous native regional nav::Mesh refresh, authoritative common Command handler with authenticated player/actor, scene/socket/epoch validation and server-owned graph masks.
- Reconstruct loaded-scene set from persistent ECS collection on authoritative checkpoint restore. Validate namespace/content identity; suspend common Simulation during external reconciliation; rebuild static and leaf owners and graph data, discard old command epoch. External graph owners must reset their revision caches at the recovery barrier.
- BuildingRenderer3DScene optionally owns a borrowed static CPU shell in addition to movable leaf Models. WorldScene accepts multiple scene-ID render owners. Host binds full procedural/imported PBR maps before upload and must retire on the graphics owner before CPU/physics teardown.
- BuildingRuntime umbrella, renderer compile contract, example README and building style skill updated.
- No genre-specific files were changed. Existing shared engine systems remain authoritative.

## Evidence and remaining limits
Exact parent code inspected; candidate tree and commit constructed with GitHub. No native C++ compilation, unit tests, network loop, or GPU execution in this environment. New host is opt-in from native GameModule::setup; no existing genre executable was rewritten. Native regional nav must be a dedicated existing mesh, other world graph consumers must be provided by their real owners. The existing actor/cargo movement must respect canTraverse and supplied ECS/PHYSIS extents. Authoritative checkpoint restoration intentionally cancels old queued/in-flight aperture commands and invalidates command epoch; deterministic lockstep resimulation is not established. The generic aperture registry fails closed for elevator recipes (existing BuildingCirculationEcsScene still needs persistent lift/rider integration and graphics). No successful full integration claim.

## Next
Inspect exact-head focused and platform CI, fix compiler errors with evidence, validate real multi-scene load/unload and save/restore, then wire one native project/server host with authenticated ingress and actual WorldScene GPU capture. Complete persistent elevator circulation composition and deterministic rollback if required. Keep PR draft, no main merge until verified.
