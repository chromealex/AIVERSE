# Caverns Stage54v2c2 — managed native navigation lease publication

Date: 2026-11-24. Branch `integration/caverns-studio-stage54c-20261124`. Starting HEAD 38ace7b6; roadmap d87c2a4d; implementation e1371aff; code/test/docs HEAD `81b6361505c5eb3ee9e1f3791cce023e41598353`.

## Delivered
- `TerraWeaveCavernsPlanetNavigation.h`: optional canonical environment input contract (epoch, immutable PHYSIS mapping and existing environment policies).
- `TerraWeaveCavernsPlanetRuntimeBuild.h`: complete temporary lease map from one frozen ECS snapshot; reuse unchanged provider, reject regressing or missing environment epochs, no mutation on failed prepare.
- `TerraWeaveCavernsPlanetRuntimeFrame.h`: optional managed path prepares leases before existing real navigation/GI hooks, exposes active map only after successful native body transaction, refuses stale/recovery/retired regions, clears managed maps on restoration, checks actual stored immutable provider and environment epoch. Environment-only refresh builds all active maps before swapping under host exclusion, with no geometry or collision rebuild. `motorIntent` gates the existing ECS steering request without writing position. Legacy external-hook path remains available.
- Graph, cached/uncached native NavMesh and regional cache results stamp scope; direct graph paths validate resident/oriented segments. Regional cache resets on scope/env change or equal-revision scoped provider replacement.
- Registered planetary collision fixture adds private preparation, unchanged epoch reuse, refresh, missing/regressing epoch refusal; pathfinding skill updated.

## Evidence
Source committed only. Native build, CTest, fixed-tick integration, save/replay, actual GPU/physics/GI transaction, performance/platforms and headless acceptance NOT RUN. Existing persistent schemas unchanged; no new editable property/graph, MCP/Studio/inspector regressions NOT RUN. Existing real host hooks still mandatory, no GI replacement or fake ACK. No visible capture or standalone playable host. Main unchanged, Mac KINGSTON gate pending.

## Remaining
Stage54v2c3: provide a concrete canonical environment revision source from actual PHYSIS/NEREID authority and wire the scoped installer and managed native gate into an actual genre-neutral gameplay host/example. The supplied epoch/mapping must be immutable and advance on relevant changes; current code does not derive it automatically. External solver caches must consume the newly committed provider, and actual GI integration remains host-owned. Verify fixed-tick scope persistence, restore and environment repath; then Stage55 comprehensive native/Studio QA. Do not claim default GameCore integration or successful traversal from this source-only stage.
