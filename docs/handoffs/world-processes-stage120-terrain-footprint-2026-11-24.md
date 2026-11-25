# Stage 120 — seam-aware planetary terrain mutation preflight
Date: 2026-11-24. Owner: World Processes. Branch: feature/world-processes-stage31-bidirectional-handles.
Base e795f04d2c92a5a65097f007e82ebd022f45d8b8. Plan 6620c5f462280850f38d18e289da19c3ec29b14e. Implementation 831a907ab9b5f944aa2822f19c918f2d1560641b.

## Delivered
- libraries/include/aiverselib/world/PlanetTerrainFootprint.h: read-only canonical cell footprint, four sorted body-local canonical corner IDs with all 1/2/3 aliases, exact signed original heights in Surface units (1/16 metre), sorted unique global grid cells touching those vertices. Reuses CubeSphere::cornerDuplicates; rejects invalid body/cell and inconsistent seam aliases, never projects to XZ or mixes bodies.
- libraries/include/aiverselib/world/GranularPlanetTerrainTransfer.h: existing PlanetSoilTarget now carries the preflight footprint. Invalid seam snapshots are rejected before invoking the staged terrain owner. Existing mass budget, revision receipts, COW and flat-path behavior retained.
- tests/world_processes_gamecore_native_test.cpp: fixture source for interior, seam, triple vertex, cross-body rejection/isolation, inconsistent aliases and actual transfer target.

## Owner dependency — BLOCKED
Inspected complete recursive trees of feature/terraweave at 2d034dd6bda65044809e9a04217587a033bc9682 and feature/terraweave-caverns-core-20261123 at 6cd16e60bd903f44c9b8e8ec3a949804a170f000. Existing Earthworks/EcsAuthority are planar; caverns ECS is not a replacement for spherical height authority. In current WP tree, Surface.h explicitly describes immutable shared snapshots, GameState::surface, MapGrid::surf and SpatialHash::surf share pointers. Surface::corners are fixed heights; mutating only one copy breaks authoritative geometry, navigation and rollback.

TerraWeave owner must supply canonical revisioned surface edit/publication, not a second soil map:
1. Validate body and expected terrain revision plus footprint before heights. Use canonical IDs/aliases and bedrock, deterministic conservative volume in cm³; no assumed square cell area or cm-to-1/16 rounding loss masquerading as transfer.
2. Stage all alias updates and their derived affected cells on a new immutable Surface snapshot in the authoritative World transaction; preserve body topology/tables. Synchronize all GameState, MapGrid, SpatialHash consumers; invalidate slopes, paths, presentation, collision and downstream terrain owner dependencies at the same barrier. No shared mutable pointer/const_cast.
3. Register versioned terrain revision/state save/hash/rollback and recreate derived references on restore. Provide owner callback to transferPlanetSoil with exact moved volume and revision receipt. Derived GPU/physics updates only after authoritative commit, not in callback.
4. Acceptance: seam and cube-corner dig/fill, unequal bodies, conservation/quantization, bedrock, stale revision, failure atomicity, COW, save/load, nav/render/collider publication, plane regression.

No real planetary height mutation or volume solver implemented in this stage. No changes to another agent's branch. The next physical feature can proceed independently: Stage 121 GRANULAR explicit physical scale and existing render/collider integration; do not claim Stage 120 terrain editing done until Terra owner dependency is available.

## Evidence
GitHub tree/source inspection and branch writes succeeded. Native compilation, CTest, browser/MCP, performance, platforms, physics/renderer capture and merged-tree Mac gate NOT RUN (owner deferral). New helper is header-only, no target or schema, no new authority or mutation RPC, existing skill updated. No main sync/merge. Actual physical terrain acceptance remains blocked.
