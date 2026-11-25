# Stage 122 — EVOLVE planetary rooted climate routing
Date: 2026-11-24. Owner: World Processes. Branch: feature/world-processes-stage31-bidirectional-handles.
Base 45285f489ccbc27b1ff3b4910c226d98ed380b27. Plan 36626129279cf8376c29c3dc64f5f35b4cdde19d. Implementation 09e8cbc13b01ec5e478abccd21874850b6a2d473.

## Delivered
- libraries/include/aiverselib/world/EvolvePlanetRootClimate.h: synchronous read-only batch for owner root sites. Explicit owner weather mappings specify canonical ground, body ID, PHYSIS cell entity and coverage in metres. Validate actual membership in PHYSIS State grid (not arbitrary components), exact requested PHYSIS climate tick, valid weather ranges, unique mapped cell identity and ground/body consistency.
- Select nearest eligible same-body weather source using canonical Surface chord distance across seams; tie-break by packed entity identity independent of input ordering. No extrapolation outside source coverage. Required missing weather fails entire batch, optional missing weather preserves baseline and applies EVOLVE. Optional wetness cap reuses existing policy; normalized precipitation is not water depth, wetness is not authoritative soil moisture.
- Reuse samplePlanetEcology and canonical SurfaceRegionIndex for nutrients/light constraints; result ordered by input site. No persistent weather mapping or duplicate forest/climate state. Limits: 4096 mappings/sites and 1048576 same-body candidate comparisons; oversized request fails closed. Expected climate tick is PHYSIS State::tick, not Simulation tick.
- tests/world_processes_gamecore_native_test.cpp: fixture uses actual initialized PHYSIS grid and regions across a face seam, deterministic tie order, independent planet/moon, tick mismatch, duplicate mapping, required/optional uncovered site and orphan weather component rejection.

## Owner use
At a synchronous owner barrier after weather publication, resolve existing authoritative rooted sites to canonical Surface ground, obtain baseline sun/water/nutrients/flooding from existing TerraWeave/NEREID owners, and supply explicit PHYSIS mappings. Call samplePlanetRootClimate with the expected climate tick and consume the ordered output in the existing vegetation owner, not a new lifecycle. Keep World, surface and mapping immutable for the duration. Rebuild batch after rollback/terrain changes.

## Remaining dependency
Stage99 inspected the existing owner interfaces; current WP tree still has GamePlanetFlora as a presentation-only scatter cache and TerraWeaveForestEcologyEcs rooted climate, soil queries and recruitment in planar xCm/zCm. This stage does NOT add a spherical rooted tree lifecycle, canopy/seed recruitment, weather grid simulation or automatic vegetation rendering. TerraWeave must supply its canonical planetary rooted identity/positions and lifecycle before integration. Host must supply a real PHYSIS mapping; no implicit projection of its planar grid. No change to planar flow, ECS persistence or Simulation scheduling.

## Verification
GitHub source inspection and branch writes performed. Native compiler, fixture execution, CTest, actual vegetation lifecycle, performance, platforms, browser/MCP, screenshots and merged-tree Mac quick NOT RUN (owner deferral). New header-only optional library consumer; no new target, persistence, renderer or solver. Skills and compatibility updated. No main sync/merge.

## Next
Stage 123: FRACTURE physical debris integration via existing Box3D/PHYSIS and generation-safe collapse receipts. Preserve owner dependencies for terrain mutation and rooted flora and do not claim end-to-end planetary ecology acceptance.
