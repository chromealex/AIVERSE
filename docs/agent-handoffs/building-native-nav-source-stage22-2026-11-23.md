# AIVERSE — Stage 22: native procedural-building navigation source

Date: 2026-11-23
Repository: chromealex/AIVERSE
Branch: feature/aiv20261123_building-grammar
Draft PR: https://github.com/chromealex/AIVERSE/pull/114

## Scope and implementation

NEW `engine/buildings/BuildingNavSource.h`: `BuildingNavSource`
implements the EXISTING `nav::Source` interface
(`spans(x,z)`, `link(x,z,i,x,z,j)`), including
`BuildingNavSourceOptions`. It derives a bounded, fixed-point
raster from the generated `BuildingStructureGraph`, world origin,
quarter-turn yaw and actual construction dimensions.

- Stacked walkable surfaces are sampled from `BuildingGraphPart::Floor`
  upper faces, so multiple storeys are represented at their true
  generated Y coordinates.
- A column cell is conservatively empty when its centre lies outside
  the transformed footprint or intersects structural solids within the
  configured standing headroom. Slab shaft apertures are preserved
  because no Floor solid is generated inside those cutouts.
- The adjacency callback is symmetric for orthogonal columns, checks
  step height and tests the connecting midpoint against physical graph
  solids (including partitions/walls/door leaves).
- Stair tread/landing and dynamic elevator platforms are deliberately
  excluded from standard floor spans: `BuildingNavMeshBridge` retains
  the actual separately resolved stair/lift action endpoints and
  `BuildingStairMotor` does movement only after game PHYSIS sweep.
- `engine/buildings/BuildingRuntime.h` includes the new adapter.
- `skills/world/aiverse-building-style/SKILL.md` updated with
  Stage 22 usage and limits. Existing skill catalog entry is reused.

## Ownership contract

1. The game owning a generated building retains its
   `BuildingStructureGraph` and constructs `BuildingNavSource` from
   the same construction size and world transform as collision meshes.
2. Call `source.initialize(graph,W,D,origin,yaw,options,&error)`.
3. The existing native nav owner calls
   `navMesh.build(source,source.config(),&error)`; if other world
   geometry shares the map, compose sources at the nav-owner level and
   refresh through the existing mesh, rather than installing a second
   NavMesh or replacing unrelated world tiles.
4. Only after a successful nav owner publish should
   `scene.rebuildNavigation(navMesh)` resolve off-mesh landing anchors.
5. On building geometry/door/terrain changes, regenerate or update the
   authoritative nav source as appropriate, mark affected nav tiles
   dirty and refresh the mesh before planning new building paths.
   Character motion still requires the game's actual collision sweep.

## Known constraints / no inflated claims

This is source-level rasterization of generated solids, NOT a collision-
world raster importer. It samples cell centres and boundary midpoints,
not the entire swept capsule. It does not bake external terrain,
vehicles, physics obstacles, dynamic lift platforms, arbitrary triangle
mesh supports or geometrically erode narrow corridors. Door leaves are
static geometry sampled at build; moving dynamic landing door collision
must trigger owner-managed invalidation/rebake or a separate dynamic
blocker policy. Native nav Mesh itself has no built-in off-mesh links.

No game has been wired end-to-end in this stage. The user deferred
tests, and NO clang/GCC compilation, CTest, PHYSIS playtest,
Mac GPU/GL33 screenshots, Studio TypeScript typecheck, or
multiplayer validation was performed. Source commits do not prove
building or routing correctness.

## Provenance

- `31500f09197429a6c71cf8672a055ca87539a1ab` — new nav source.
- `1db4134a412a3a77462c56e72e34d77b3ab47e7c` — runtime exposure.
- `da1b7df6340b21d59eaf7c648afd7874b4d7134e` — skill update.
- This handoff is separately committed.

## Next concrete stage

Stage 23: integrate functional doors, shutters, lifts and other dynamic
building apertures with the authoritative PHYSIS/SpatialWorld and
native nav invalidation policy, without duplicating editor or rendering
ownership. Add a committed-state change event for visuals, and let
Studio's existing inspector/generator reference the same graph state.
Then conduct focused stage 19-23 C++ and live Mac navigation tests
when the user allows testing; reconcile heavily diverged main before
taking draft PR 114 out of Draft. Include a new handoff with actual
test evidence and exact branch/commit.
