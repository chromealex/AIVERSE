# Caverns Stage54v2c3a — canonical environment revision and tick capture

Date: 2026-11-24. Branch `integration/caverns-studio-stage54c-20261124`. Starting HEAD 8febc48f; roadmap 9bdd2df9; environment source 36e43419; code/test/docs HEAD `6751b741e29246263e189fd40e972dc7b7625435`.

## Delivered
- `engine/world/TerraWeaveCavernsPlanetEnvironmentEpoch.h`: deterministic fingerprint of actual PHYSIS gravity and traction, NEREID plane/geometry/river-grade/sphere/wave components and flood water columns. Persistent monotonic environment revision, explicit immutable policy revision, simulation wave time only when applicable. Static inputs reuse epoch; unrelated actor state does not affect it. Conservative global invalidation (FNV fingerprint, not collision-free); no second solver.
- Canonical native input callback maps chart points through the existing simulation-anchor frame into the actual PHYSIS cell grid. For absent PHYSIS grid returns no mapped cell. Use same persistent epoch for native lease refresh.
- `TerraWeaveCavernsNavigationEnvironment.h`, `TerraWeaveCavernsPlanetNavigation.h`, runtime candidate and ECS provider adapters: existing NEREID spherical sampling at frozen fixed simulation time in addition to existing flat sampling; wet if either owner reports submerged. Flat default unchanged.
- Shared canonical provider set reuses unchanged SDF/environment sources, clears derived state on rollback. `installCanonicalCavePlanetNavigation` registers environment revision resource and installs capture -> existing repath -> steering immediately before host's existing motor. Native frame still consumes managed leases separately; no local native epoch in deterministic routing.
- Existing registered planetary authority fixture extends epoch change/reuse tests for gravity, traction, sphere water and stale provider rejection; existing pathfinding skill updated.

## Evidence
Source committed, NOT RUN: compiler, CTest, actual fixed-tick installer execution, save/rollback persistence, native physics/renderer/GI, performance and headless. Host must register actual PHYSIS/NEREID persistence; existing schema ownership unchanged. New optional derived revision resource registered only by new installer. No new authored map/graph or editor property; MCP/Studio/inspector regression NOT RUN. No visual capture. Main unchanged. Mac KINGSTON gate pending.

## Remaining
54v2c3b: actual genre-neutral native host/example using the canonical installer, environment-owner tick order, managed native lease refresh and real existing motor; exercise actual scope persistence/rollback and repath in registered fixture. Default spherical GameCore still has grid movement, not automatically migrated to Box3D. Actual GI hook implementation remains host responsibility. Global environment changes/active waves may replan frequently; measure, then optimize regional invalidation without introducing a separate simulation. No claims of verified gameplay until Stage55 native/Studio QA.
