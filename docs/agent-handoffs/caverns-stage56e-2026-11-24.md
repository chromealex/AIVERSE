# Caverns Stage56e — native volumetric motor admission

Date: 2026-11-24. Branch `integration/caverns-studio-stage54c-20261124`. Start c6f8ec76d4f32f99c5f0a89fcadc78948115297f; roadmap f0ee20f8dd01b88cf197c32dfbc195e39742cfcc; implementation 7d854a4f3c3ade83e1406cc6d2f18b6209f5be85.

## Delivered source
- `engine/physics/Character.h/.cpp`: additive collision-only oriented world-velocity sweep using existing Box3D mover and true-plane solver, clipping at final contacts, no ground snap/step-up/gravity, no Scene::step or proxy commit. Free motion includes dynamic overhead planes; ordinary ground queries retain existing behavior.
- `engine/physics/TerraWeaveCavernsPlanetMotorBridge.h`: route speed/approach cap, PHYSIS orientation or zero-G chart orientation for flight, canonical NEREID sphere/flat sample depth in compatible support frame, existing LocomotionModes swim policy, caller-supplied existing flight motor velocity proposal. Validate matching native Scene, proxy, region, current environment lease, chart and SDF traversal segment. Stage character and swim policy state; commit once after admission. No new ECS owner, flight thrust solver or water solver.
- Existing collision fixture source adds real Box3D vertical ceiling sweep and route cap/rejection checks. Updated existing Caverns skill, area rules, native README.

## Delivery evidence and limitations
Source and docs committed; compile/link, CTest, native swimming/flight play, persistence/replay, performance, platform matrix, headless, Studio/MCP, GPU and Mac acceptance NOT RUN. Connector-only; no Mac KINGSTON build volume. No new authored inputs or persistence schema, no new inspector/MCP/Studio/graph operation; existing route profile and host locomotion state reused. New APIs are additive. Native motor remains sole authority; caller must choose exactly one mode per tick and stage any stateful flight proposal. No independent flight controller or automatic swimming transition. Water depth compatibility is conservative; tilted incompatible water rejected. Water currents and advanced support feedback remain host concerns. Actual end-to-end native host lifecycle and default GameClient integration still open; do not claim live playable feature. Main unchanged.

## Next
Stage57: explicit surface/subsurface and ground/swim/flight handoff, with existing route scope, portal geometry, occupied/destination chart pins, native coverage and collision admission, canonical ECS transition state, rollback and no double motor. Integrate in a project that actually owns native planet physics and shared GI; then complete real QA.
