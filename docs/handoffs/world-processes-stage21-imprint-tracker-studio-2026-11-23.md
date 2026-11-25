# AIVERSE World Processes — Stage 21: authored IMPRINT movement trackers and live CROWD routes

Date: 2026-11-23. Branch `feature/world-processes-experimental`, draft PR https://github.com/chromealex/AIVERSE/pull/112.
Main remains untouched. Implementation first, tests later (per user).

## What changed

1. `libraries/include/aiverselib/world/WorldProcessesExtendedLinks.h`, commit `17da958f437576215f9d548ebf1ac515b7747933`: added `ImprintTracker` to the stable-map-ID linked component schema.
   - Authored JSON fields: `source` (any stable map entity ID, including non-CROWD actors), `cell` (must target `ImprintCell`), `minimumDistance` (0..1,000,000 integer cm), `footprint/scent/residue` (0..100,000), `enabled` (default false).
   - Native apply creates the **real** `imprint::Tracker` ECS component. It does not create a transform/collision ownership duplicate. `source` may reference any currently live Entity; `cell` must reference the right component.
   - Enabling requires an assigned source and cell. Source/target ownership and generation are checked when map entities are spawned.

2. `WorldProcessesMapHydration.h`, commit `0e970d4409bade9f89643f3718333139c4fba8e7`: extended linked authoring preflight to 13 kinds, capped at `imprint::kMaxTrackers`, included IMPRINT suite auto-enable, and supports the generic `Entity` target class. Stable map IDs are resolved to generation-safe Entity handles before the trail simulation runs.

3. `apps/studio-native/StudioSession.cpp`, commits `59436a23e2e7ff7662e889bd331d14227f58bdd4` and `ec3686844bf02a1034e5a2a7ca1d4aaa6d991e17`: adds IMPRINT Tracker to normal Add Component, inspector and graph integrity guard. Source-picker choices include real stable map objects even if not CROWD agents, while the target-cell picker only includes authored `ImprintCell` entities. Existing setProperty/setProperties, MapDoc Undo/Redo, Save and reference-aware deletion behavior applies.

4. `libraries/include/aiverselib/world/ImprintCrowdBridge.h`, commit `a8896eb065e60066ec6e99546df4ba98274cb8b8`: automatic CROWD→IMPRINT producer now filters to trackers whose source is actually a `crowd::Agent`. Trackers driven by a native Box3D, ActorMotor, or project-specific movement host can use the same `imprint::sampleMotion` public API and are not falsely rejected by the CROWD adapter.

5. `WorldProcessesExtendedInspector.h`, commit `e8efd8864433b064f3da82e69323af6fcacd152c`: real read-only Studio Live Inspector cards for `CrowdRoute` and `ImprintTracker`. Route inspection exposes waypoint count, cursor, nav revision, active state, radius and last sampled tick. Tracker inspection exposes source and cell Entity refs, quantized prior/current coordinates, emission spacing, amounts, and clocks. The runtime card never changes gameplay.

## Architecture

`WorldProcessesSuite::installSuite` registers the deterministic update order:
- CROWD navigation route cursor, then CROWD local avoidance
- Optional CROWD→IMPRINT sampling, then IMPRINT trackers, then IMPRINT diffusion

`CrowdNavMeshBridge.h` uses existing `nav::Mesh` and TerraWeave Earthworks at the **authoritative route planning phase**, then stores integer waypoint state in `crowd::Route`; the navmesh itself is not cloned into ECS.

`ImprintTracker` is one persistent actor-to-world-cell trace emitter. A tracker records its first position without emission, then stamps when the source moves farther than the authored minimum distance. The IMPRINT ECS cell is canonical, and VFX/decal presentation may read it but cannot write it.

## Tests performed

**NONE — deliberately deferred by user.** GitHub commits mean implementation text has been saved, not compiled or executed. No CMake, sanitizer, Studio binary, browser TypeScript check, NavMesh parity, screenshot or merged-tree Mac quick acceptance has been run for Stage20/21. Historical prior Stage05 green cannot be reused as evidence for these changes.

## Known limitations, next action

- Authorable `CrowdRoute` is intentionally **not** a separate mutable map component: a real navmesh route is built from target/terrain by the existing nav::Mesh, then stored in persistent ECS at an authoritative barrier. The Live Inspector shows it read-only.
- No world-position→IMPRINT cell automatic spatial assignment yet; the actor's tracker must point at a compatible cell. Host authors can relocate its target as an explicit validated command.
- Scene VFX decals, footprint material shaders, acoustic/olfactory AI query event integration and final NavMesh refresh/replanning remain upcoming integrations.
- Next: CIRCUIT device and gameplay event bindings, followed by EVOLVE and SOCIETY native adapters. Maintain genre-neutral core and separate handoff files.
- Only after all requested functionality is delivered, enter the consolidated test/compile/replay/Studio acceptance phase and resolve compiler failures before considering merge.
