# AIVERSE Building Generator — Stage 23C dynamic navigation and multigraph integration

Date: 2026-11-23
Repository: chromealex/AIVERSE
Branch: feature/aiv20261123_building-grammar
Draft PR: https://github.com/chromealex/AIVERSE/pull/114

## Goal and verified repo context

User requested Stage 23C after Stage 23A generic graph router and Stage 23B authoritative physical apertures. Re-examined actual current branch files before modifying them: `BuildingNavSource.h`, `BuildingApertureOwner.h`, `BuildingGraphChanges.h`, `BuildingRuntime.h`, plus earlier actual native `nav::Source`/`SpatialWorld` APIs. The existing nav owner is `nav::Mesh` with `build/refresh/markDirty`. No alternate pathfinding graph or physics world is introduced.

## Source changes delivered

1. `engine/buildings/BuildingNavSource.h`:
   - Added read-only `standingHeight()` used by downstream dynamic collision overlays.
   - Static walkability no longer includes movable `DoorLeaf` or `RoofPanel` (or moving `ElevatorPlatform`) in permanent static headroom blockers. The graph's authored solids remain intact for Studio preview and geometry recipes.
2. `engine/buildings/BuildingApertureOwner.h`:
   - Exposes `ownedColliderIds()` and `closedColliderBounds()` for read-only authoritative physical state inspection by graph consumers. The owner of open/close still exclusively mutates `SpatialWorld`.
3. NEW `engine/buildings/BuildingDynamicNavSource.h`:
   - `BuildingDynamicNavSource` implements existing `nav::Source` over a long-lived `BuildingNavSource`.
   - `synchronize(apertures, spatial, error)` validates every closed registered aperture against the exact current `SpatialWorld` AABB, rejects unexpected colliders for open apertures, and only then atomically replaces its derived blocker snapshot.
   - `spans()` excludes base standing surfaces obstructed by those physically closed apertures; `link()` rejects boundary crossings blocked by aperture collision.
   - Crucial multi-storey correctness: after filtering a low-floor span from a column, dynamic `link()` maps the filtered index back to the original base Source index before delegating the native height/partition crossing check; it must never link the wrong higher-floor span.
   - `buildingDynamicNavInvalidator(dynamic, apertures, spatial, mesh)` supplies a real Navigation-domain adapter for `BuildingGraphChangeRouter`: synchronize physical blockers, then call existing `markBuildingNavCollisionChanged` for the event's bounds. Native `nav::Mesh` owner must later run its normal `refresh(dynamic)`.
4. `engine/buildings/BuildingRuntime.h` includes the new source for native game users.
5. `skills/world/aiverse-building-style/SKILL.md` includes Stage 23C lifetime, ordering and caveats. Existing skill catalog registration is reused.

## Contract for authoritative game code

1. Build the STATIC source from the generated building graph and the same world transform used by generated collision; keep it alive.
2. Attach `BuildingApertureOwner` with the game's own unique physical collider IDs and live obstruction probe.
3. Construct `BuildingDynamicNavSource dynamic(staticSource)`; call `dynamic.synchronize(apertures, spatial, &error)` and `mesh.build(dynamic, dynamic.config(), &error)`.
4. Register the Navigation consumer with `router.registerConsumer(BuildingGraphDomain::Navigation, buildingDynamicNavInvalidator(dynamic, apertures, spatial, mesh))`. Each other *relevant* graph system registers its own owner callback independently.
5. On a server-authorized `apertures.setOpen(socket, open, spatial, router, exactMask)`, physical collision commits FIRST; then the graph router snapshots the actual physical state and marks nav tiles dirty. Refused/unregistered domains remain pending and can be retried through `retryGraphPublication`.
6. The existing navigation owner subsequently runs `mesh.refresh(dynamic)` and `circulation.rebuildNavigation(mesh)` before routing. Collision checks still gate real actor motion. A successful router ACK is not a proof that the NavMesh refresh has run.

## What is NOT yet implemented

- This adapter is specific to the existing procedural aperture colliders. It does not automatically combine unrelated terrain, static outside floors, dynamic characters, vehicles, third-party physics AABBs or independent building regions into a universal nav source.
- No actual MapGrid/HPA/flow-field, RoadGraph, ZoneGraph, ShaderGraph/VFX, audio, or simulation owner callbacks have been wired. Stage 23A supplies the shared notification contract and this Stage 23C supplies the concrete native NavMesh consumer; all other adapters remain future work.
- No real game tick/Studio interaction/authorization/renderer path is yet bound to this source.
- Filtering cell centres and link midpoints is conservative but not full capsule erosion, so PHYSIS/motor sweep remains required; external doors with no adjacent walkable exterior floor do not gain magically connected navigation.
- No build, native unit tests, CTest, graphics/Metal/GL33 capture or multiplayer validation was performed, consistent with user request to defer tests. Source presence is NOT compile/runtime verification.

## Exact stage provenance

- `b0bad6f111d4d40ec7d75248141885e47aa9af04`: static source excludes movable blockers.
- `ef9f7383684c03328fd899257f4b7c48eec88e47`: aperture physical collider readback.
- `f4e4e8649fec31fb896fb711e6273358c87381c5`: dynamic overlay source and navigation callback.
- `138004bc3e0b549619bba6c072a11154f2691c6a`: include/cleanup fix.
- `c4a7383a0f85f0f56827f864be078170fbc8a652`: native runtime umbrella exposure.
- `e844d889dc89a69902d5b4cf5fa16f917627ce13`: agent skill update.
- This document receives a separate final Stage 23C commit.

## Next development stage

Stage 23D: wire concrete owner adapters for the existing MapGrid path-cache/HPA/flow-field runtime and building-room connectivity, with explicit rebuild/bounds/epoch contracts. Then connect actual authority and game-world fixed tick for opening/closing doors, including optional exterior floor composition and real renderer animation. Do not bypass the present graph-owner contracts or duplicate NavMesh. Every stage requires its own skill update and handoff. Keep PR 114 Draft until source builds and tests later pass and the divergent main branch has been reconciled.
