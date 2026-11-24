---
name: aiverse-granular
description: Use AIVERSE GRANULAR for Bounded cellular loose solids, Studio grid painting and TerraWeave terrain mass transfer.
---
# GRANULAR — AIVERSE skill

## When to activate
Bounded cellular loose solids, Studio grid painting and TerraWeave terrain mass transfer.

Typical requests: sand, soil settling, granular cells, dig and fill, loose debris. Use this skill only for this mechanic; for multi-system orchestration see `skills/world/aiverse-world-processes/SKILL.md`.

## Canonical code and API
- **Gameplay headers:** `libraries/include/aiverselib/world/GranularEcs.h`, `libraries/include/aiverselib/world/GranularDocument.h`.
- **Authoritative ECS:** `granular::Field, Source`.
- **Lifecycle:** `aiverse::granular::install(sim)`; per-tick behavior: `granular::advance(world,tick)`. Install once in the authoritative `Simulation::setup` phase, before the persistence registry seals; don't install the same system twice via suite and direct calls.
- **CMake target:** no dedicated `AIVERSE::lib_*` target for this skill. This is an opt-in existing-native-module gameplay recipe (`runtime: none` in `skills/catalog.json`). The actual project links its established `AIVERSE::libraries` and any optional host owners (Box3D, Renderer3D, PHYSIS, TerraWeave) only when needed.
- **Studio-authored components:** `GranularField, GranularSource`. These are map authoring fields, not a writable duplicate of live ECS output.
- **Adjacent adapters:** `examples/world/world_processes_native/GranularTerraWeaveBinding.h`, `GranularWorldIntegration.h`, `GranularTerraWeaveTransfer.h`, `PhysicalSceneHost.h`. Native project integrations are optional; include only in the target that already owns those subsystems.

## Minimal C++20 usage pattern
```cpp
#include "aiverselib/world/GranularEcs.h"
aiverse::granular::install(sim);
// For 2D authored grids:
auto doc=granular::authoring::create(8,8,1000);
granular::authoring::paint(doc,2,3,200,false);
// Game host later calls granular::authoring::apply(world,entity,doc).
```

If using normal GameCore map projects instead of a custom module, prefer the compiled opt-in `world_processes` native module and Studio. Example fragments: `examples/native/world_processes/authoring-examples.json`; native setup/lifecycle: `examples/native/world_processes/README.md`.

## Do not cross these ownership boundaries
TerraWeave is sole canonical soil authority; NEREID water and Box3D rigid collisions are not replaced. Editor brush operates on authored MapDoc; render/collider are derived tiles. Not full 3D SPH/DEM.

## Acceptance checklist (execute when user lifts test deferral)
- Grid ≤128×128, conservation except Source and explicit Terra transfers, blocked cells empty, world revision/cache invalidation, GPU fence and rollback.
- Verify C++ compilation, CTest source and actual run outcome; include GCC/Clang, ASan/UBSan where supported.
- Check authoritative tick order, World snapshot/COW, persistent schema IDs, clean reload, rollback/replay checksums, malformed input rejection, and expired Entity generation references.
- If the feature is visually observable, capture **real** native Studio/game screenshots with source SHA and platform/backend provenance; do not use fabricated renders as QA proof.
- Stage13/18/19 GPU, Box3D, TerraWeave and CTest not yet run.

## Handoffs and current verification
- Read matching `docs/handoffs/world-processes-stage13-granular-evolve-society-2026-11-23.md`, `docs/handoffs/world-processes-stage18-granular-terra-box3d-2026-11-23.md`, `docs/handoffs/world-processes-stage19-physical-native-host-2026-11-23.md` (some stage handoff names contain extra suffixes; search `docs/handoffs/world-processes-stageNN-*` when needed).
- The PR is **DRAFT**; latest source changes have **not** been verified by a completed full native build or end-to-end scene test. Do not infer production readiness from the presence of API/docs.

## Cross-cutting contract
- Read `AGENTS.md`, `docs/agents/ecs.md`, `docs/ecs-mechanic-composition.md`, and `skills/world/aiverse-world-processes/SKILL.md` only as necessary for system boundaries.
- All live game state must be ECS-owned: `World` + `Simulation`. C++20, stable packed-generation `Entity` links in memory; map authoring stores only stable document ID strings. Never serialize ephemeral ECS slot numbers.
- Integration is opt-in. For regular GameCore maps, compile `examples/native/world_processes/WorldProcessesModule.cpp`, enable `{"id":"world_processes"}` in the project's `rules.json` `nativeModules`, and use the existing `StudioSession` map component authoring. Actual adapter: `aiverselib/world/WorldProcessesNativeModule.h` consumes `GameCore::matchMap()` + `mapPlaced()`.
- Shared standalone/linked map schemas: `aiverselib/world/WorldProcessesAuthoring.h`, `WorldProcessesLinkedAuthoring.h`, `WorldProcessesExtendedAuthoring.h`, `WorldProcessesExtendedLinks.h`. `WorldProcessesMapHydration.h` resolves live ECS owners with strict validation before installing/creating components.
- In Studio use existing Add Component, reference picker, Scene gizmos and native `MapDoc` Undo/Redo. For live observation use `WorldProcessesInspector.h`/`WorldProcessesExtendedInspector.h`, never writable runtime clones.
- After each implementation step add a new `docs/handoffs/world-processes-stageNN-*.md` describing code paths, API, ownership, branch/commit, actual tests/results, missing features and exact next action. Update agent guidance; do not merge unverified code into main.
- Current source is in draft PR #112 on `feature/world-processes-experimental`. New Stage11+ features are **source-only, not confirmed compiling or passing runtime tests**. The user deferred testing; do not describe old Stage05 successful tests as proof of these newer files.

## Stage 87: derived active rows and cell masks
`granular::settle(Field&,tick)` retains its existing contract. Optional three-argument overload accepts `SettlementActivity*` (sourceRows, activeRows, skippedRows, candidateCells, transfers, movedMass). Build two 64-bit occupancy words per row while validating current field; derive free destinations after the lower row has settled, dilate by one column to include diagonals across word boundaries, and visit candidate bits in original ascending x order. Empty or blocked/full rows skip transfer work. No persistent sleep state or new ECS fields; direct mass/obstacle edits, sources, enabled changes and rollback automatically affect the next call. Keep bottom-to-top traversal, one-row movement, parity direction, capacity and mass exactly. Disabled and quiet fields still stamp sampledTick. Activity is published only on success. Existing dense reference in linked test fixture compares 128 cases across 8 ticks; no tests executed. See Stage87 handoff.

## Stage 88P: optional PHYSIS gravity
Include `WorldProcessGravity.h` and choose `installGranularWithGravity` INSTEAD OF legacy install in a native host. Resolver returns optional local Vec3 acceleration from PHYSIS; source edits remain staged and unresolved placement fails the step. Down/up/left/right or zero gravity use canonical 2D settlement; out-of-plane component and magnitude-dependent fall rate are not simulated. Existing fields are vertical sections, +column east and +row down. Planet terrain/mesh/collider patch mapping and seam transfer still require integration; see planetary contract. Tests not run.

## Stage 92: planetary GRANULAR section and native gravity
`GranularSurfaceField.h` maps the existing vertical-section Field onto canonical Surface: columns along the origin east great circle, rows from high to low radial altitude, ground sampled from Surface::project (the actual planet height source). Bounded patch query returns common Fixed positions, radial basis, canonical body/cell and mass/solid for future view and physics consumers, not a mesh or collider. Native GameCore planet maps select advanceSurfaceFields instead of legacy settlement, sampling actual PHYSIS gravity at each field Transform and projecting into local frame. Host must configure PHYSIS fields and metre scale; no automatic radial field synthesis, no magnitude-dependent or per-cell gravity, no cross-field transfers. Zero/reverse/lateral gravity follows the existing cardinal 2D policy. Flat path and Field schema unchanged. Current TerraWeave Earthworks runtime mass transfer is XZ only; planetary conservative soil mutation requires a canonical terrain owner API, not editor sculpt or a second soil map. Planet mesh/collider publication and transfer remain open. Regression source added, not run. See Stage92 handoff.

## Stage 98: planetary GRANULAR soil-owner boundary

`GranularPlanetTerrainTransfer.h` exposes `planPlanetSoilTransfer` and `transferPlanetSoil` for native hosts. Explicit opt-in volume calibration: 1 mass unit = 1 cm³. A request supplies live Field entity, expected body ID, bottom-row column, cell size, direction, volume limit and expected terrain revision. Resolve actual Transform and existing Surface projection, reject airborne/underground base, stale sampled tick, disabled/solid cells, wrong body and zero budget. A bounded owner callback receives body/face/cell, ground, up and budget; the actual terrain owner computes conservative volume, including quantization and seam aliasing. Apply the owner's receipt and matching mass change to a staged World, publish by move only if valid, discard failures/no-op writes. The callback must not mutate external/shared mutable state, and may not alter the field or its position. Call only at a serialized fixed-tick barrier; no outstanding references survive publication. Host records requests for replay. Whole-World staging is not a per-grain hot path; profile before enabling frequent transfers.

**Integration blocked:** canonical Surface is immutable and existing TerraWeave Earthworks is planar. No planetary terrain owner adapter, terrain height edits, persistent soil volume, mesh, navigation or collider invalidation is supplied by this boundary. TerraWeave must provide a canonical atomic mutation path that updates the same ECS/persistence/geometry authority. Native fixture uses an explicitly test-only volume owner, not a real terrain simulation. Build/tests NOT RUN. See Stage98 handoff.

## Stage 107: live planetary GRANULAR diagnostic section

Enable the existing Live 3D overlay and Scene gizmos on a saved local Play current-map session. Select a nonzero GRANULAR planet diagnostic cell/depth (cm); zero disables it. This is an explicitly selected transient display scale, NOT a physical placement, terrain mutation, mass calibration, or collider setting. Existing Field schema has no metre scale; physical owner adapters still supply their own explicit placement. The diagnostic uses the live Transform and canonical Surface, square cell/depth, zero bottom clearance relative to ground, and canonical planetGranularTops at the completed tick. Wireframe tops are colored by actual fill fraction; no full volumetric mesh.

Stable packed-ID field order and row-major occupied 16x16 tile traversal; at most 16 fields, 64 occupied tiles, 512 output quads globally. Empty/solid cells do not emit geometry. Stale/invalid geometry is rejected and cap/rejection reported. Body-local coordinates and body IDs prevent cross-planet rendering. Native Scene guards and existing Live toggle apply. Browser polling reuses the planet snapshot while Live is enabled; the panel reports sampled counts and basic local tick controls instead of plotting spherical coordinates on a planar SVG. Flat ecology remains unchanged. No new ECS state, persistence, graph, collider or terrain owner.

Source fixtures extend the existing native geometry test for moon, fill mass, row, stale tick, disabled scale and output cap. No build, CTest, TypeScript typecheck, browser interaction, GPU captures or performance measurements run. Physical render/collider publication, actual planetary terrain transfer, per-field authored physical scale, full all-mechanic visualization and integration remain open. Next: live EVOLVE canonical SurfaceArea boundaries and state, then effective PHYSIS gravity arrows.

## Stage 120: seam-aware terrain mutation preflight

`PlanetTerrainFootprint.h` resolves the existing Surface cell's four canonical corner IDs, all seam/cube-corner aliases, exact signed heights in 1/16 metre units, and sorted unique affected grid cells on the same body. The existing GRANULAR soil transfer callback receives this footprint; inconsistent aliases fail before the callback. No second height store or volume solver. TerraWeave's inspected branches still lack the authoritative planetary mutation/publication API; actual height edits, conservation, persistence, navigation/render/collider invalidation remain blocked on that owner. Source fixture added; build/tests NOT RUN. See `docs/handoffs/world-processes-stage120-terrain-footprint-2026-11-24.md`.

## Stage 121: GRANULAR physical placement and body-local frames

`PlanetGranularPhysicalFrame.h` validates an explicit native cell/depth/clearance scale, completed field tick, ground anchor and canonical body identity. The existing native `preparePlanetGranularFrame` supports an optional selected body; `preparePlanetGranularBodyFrame` rejects other bodies and subtracts the artificial simulation anchor in Fixed before publication. Both existing Renderer3D/Box3D publishers consume identical quad data. Render draw supports explicit frame translation without moving physics. No new ECS authority, automatic host binding or persisted placement; PHYSIS remains acceleration owner, Studio diagnostic scale remains separate. Top-only collision; reset publishers on rollback/body switch. Tests and actual physical acceptance NOT RUN. See `docs/handoffs/world-processes-stage121-granular-physical-frame-2026-11-24.md`.
