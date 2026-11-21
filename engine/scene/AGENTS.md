# AIVERSE scene runtime — local agent instructions

Scope: `engine/scene/`. Read `docs/scene-worlds.md`,
`docs/agent-handoffs/scene-worlds-stage1-2026-11-19.md`,
`docs/agent-handoffs/scene-worlds-stage2-2026-11-19.md`,
`docs/ecs-agent-handoff.md` and `docs/ecs-scene-hierarchy.md`
before modifying scene/world lifecycle code.

- One Scene = independently unloadable set of entities in ONE
  `aiverse::World`. Do not implement a new ECS allocator per level or
  reinterpret `world::WorldScene` (it streams visual/spatial chunks).
- One `Simulation` owns one `World` and one deterministic tick/RNG/command
  stream. `SimulationHost` holds multiple independent Simulations.
- Authoritative scene membership is the `SceneOwner` component. Registry,
  pending replacement, ID generator and states are the `SceneState` World
  resource. Register both through `registerScenePersistence` in native
  modules opting in to serialized state. Never store source-of-truth scene
  membership only in a renderer, IO manager or global map.
- Scene IDs are World-scoped and monotonic; cross-world Entity refs MUST use
  `WorldEntityRef` with generation-safe `WorldHandle`.
- `beginLoad` is only lifecycle registration. CPU asset preparation happens
  off-thread; ECS structural changes and `activate/unload` only at an owned
  deterministic structural/tick barrier, not directly in job callbacks.
- When unloading SceneHierarchy-tracked entities pass the hierarchy. Its
  `detach` implementation repairs foreign child links before destruction.
- Immutable render assets may be shared; no mutable gameplay World resources,
  physics bodies, hierarchy caches or RNG shared accidentally across Worlds.
- Keep migration opt-in; preserve existing RTS/FPS map/save/network semantics
  until native parity tests pass. Do not edit `engine/core/ECS.h`,
  `engine/sim/Simulation.cpp` or RTS/FPS gameplay without explicit
  coordination with the ongoing ECS agent.
- Maintain COW/rollback and native binary schema versioning. Update
  `docs/scene-worlds.md` and the stage handoff after EVERY implementation
  stage, recording branch/commit, changed files, tests actually run (or
  explicitly unverified), limitations and concrete next steps.
- Required regression checks: `ecs_scene_runtime_test`,
  `ecs_multiworld_test`, existing `ecs_scene_hierarchy_test`,
  `ecs_scene_hierarchy_simulation_test`, plus FPS/RTS save/checksum tests
  before production integration.

Do not claim asset/physics/navmesh/render scene streaming is implemented in
Stage 1; this stage supplies deterministic ECS ownership and lifecycle APIs.

## Stage 2 coordinator ownership (2026-11-19)

- `SceneAsset.h` is the strict canonical v1 scene manifest. Do not accept
  `Json::integer()` for IDs: it converts via Fixed and can truncate 1.5.
  `decodeId` validates the exact integer literal. Future schema changes
  need explicit version migrations and malformed-input tests.
- `SceneLoadCoordinator.h` is a **host-only bridge**, not an ECS resource.
  `ReadAsset` may execute on a worker; callbacks for resource acquire,
  release, prefab spawn, scene activation and hierarchy changes run only on
  the host simulation owner thread. `poll` never changes authoritative ECS.
- Load at request time, but instantiate/publish only from an explicit
  deterministic tick-barrier commit. A future finishing does NOT decide the
  gameplay tick. Use `GameModule::readyToStep` for required-content gating.
- Use `SceneLoadCoordinator::unload` to coordinate resource lease release
  with ECS scene ownership. Do not bypass it with direct `SceneRuntime`
  calls for managed scenes.
- On rollback/cold restore, discard all old async tickets via
  `invalidateAfterRestore` (it keeps leases of scenes still Active with the
  same asset and releases the rest), rebuild SceneHierarchy, and use
  `queueRestoredResidency` to reacquire non-authoritative dependencies of
  non-resident Active scenes and to reschedule full jobs for restored Loading
  records. Never respawn the restored gameplay entities.
- SceneResourceAdapter keys are immutable dependency identities. Refcounting
  is per coordinator; cross-world shared GPU assets require an external
  cache. Resource `release` must not throw. Bound the incoming byte stream
  in the Reader before accepting untrusted files.
- Native validation target: `aiverse_ecs_scene_load_coordinator_test`.
  Keep it green together with Stage 1 and hierarchy tests; actual native
  results are still outstanding. Do not describe Stage 2 as tested.
- Next stage should integrate existing world streaming and presentation
  facilities via adapters, *not* fork a second renderer/physics/terrain cache.


## Integrated Static prefab catalog (Stage 4, 2026-11-19)

- `engine/scene/PrefabStaticSceneBinding.h` is the actual native bridge
  between `SceneLoadCoordinator::SpawnPrefab` and
  `storage::PrefabStaticCatalog<T>` from `engine/sim/PrefabStaticCatalog.h`.
  A new SceneAsset instance stores only a 4-byte `StaticRef<T>`; the
  sealed config table is shared through `shared_ptr<const Catalog>` across
  additive scenes and independent ECS Worlds.
- Call `registerScenePersistence` and
  `registerPrefabStaticPersistence<T>` inside `GameModule::setup`, then
  `catalog->bindWorld(sim.world())`. Do not double-register native
  `StaticRef<T>` schemas.
- Supply `StaticPrefabSceneBinding<T>(catalog, optionalInitializer).callback()`
  to the actual `SceneLoadCoordinator`; `MissingPrefabConfig::Reject`
  rejects unrecognized prefabs and rolls back the *new* scene while old
  scenes survive. Optional domains can use `Skip` and
  `PrefabSceneSpawnChain` in a deterministic registration order.
- A scene unload destroys only scene-owned Entity refs; the immutable
  prefab catalog stays alive while owned by the game/preview/module
  or any other legitimate `shared_ptr` holder. Never mutate static
  config values or register a second authoritatively writable copy.
- After cold native state restore, validate
  `catalog->validateWorld(sim.world())`, rebuild `SceneHierarchy` and
  restore scene resource residency via `invalidateAfterRestore` /
  `queueRestoredResidency`. Instance Parent/LocalTransform persistence
  is still host-owned and must be explicitly registered; this adapter
  only installs the static prefab/scene schemas.
- Native opt-in CTest: `ecs_static_scene_integration_test`; run this
  plus all relevant source-branch ECS, scene, COW, save and lockstep tests
  before proposing a main merge. **Combined native targeted integration CTest passed 7/7 on Mac Studio
  (2026-11-19). Full ECS/COW/FPS/RTS and GPU checks remain outstanding.**
- See `docs/ecs-static-prefab-scene-integration.md` and
  `docs/agent-handoffs/ecs-static-scene-stage4-2026-11-19.md` for the
  exact integration contract/commit history, verification and next step.
- The additive `CollisionStream` static collider ID namespace is still
  unresolved; do NOT attach two independent collision streams to one
  `SpatialWorld` or disable its existing guard to make tests green.


## Stage 5 — multi-domain immutable prefab configs (2026-11-19)

- `PrefabStaticDomainRegistry` in
  `engine/scene/PrefabStaticDomainRegistry.h` composes one sealed catalog
  per distinct `StaticRef<T>` type. Domain/persistence IDs are explicitly
  stable and unique. Domain order is `std::map` sorted key order, never
  import order or unordered hash-map iteration.
- `addDomain<T>(domainKey,persistentSchemaId,catalog,Reject|Skip,version)`
  runs during configuration. `seal()` freezes the domain registry.
  `registerPersistence(sim)` and `bindWorld(sim.world())` run during
  GameModule::setup; do not register the same StaticRef<T> separately.
- Before creating per-entity StaticRefs, the callback preflights ALL domain
  bindings and required prefab keys. A missing required domain causes
  rejection, leaving the just-created Entity without partial static refs.
  After successful preflight, attach 4-byte refs, then create ordinary
  mutable instance components in an optional initializer.
- `PrefabStaticCatalog<T>::canBindWorld` validates existing content
  fingerprints without mutating ECS; registry bind preflights all domains
  before inserting any. Content values remain immutable across Worlds.
- New CTest: `ecs_static_scene_multidomain_test` tests 3 typed domains,
  optional inventory, deterministic registration-order persistence,
  rollback/scene unload and changed-alias rejection. Its native result is
  **pending**. Previous Stage 4 seven-test suite passed but is not a
  substitute for this new test.
- Use `bash tools/ecs-static-scene-validate.sh build/static-scene extended 8`
  after pulling branch in an isolated worktree. It logs every CMake
  build and selected ECS/COW/FPS/RTS CTest result. See Stage 5 handoff.


## Stage 6 — editor authoring and one-shot full validation (2026-11-19)

- `PrefabStaticAuthoring<T>` parses bounded versioned JSON v1
  into sealed `PrefabStaticCatalog<T>`. The owning native project
  supplies a STRICT typed decoder: reject unknown fields, floats for
  canonical integral config, overflows, wrong domain and duplicate IDs.
  Authoring import order must not change the immutable catalog ID.
- `PrefabStaticInspector<T>` generates read-only
  `components/componentSchemas` JSON for the already existing
  `NativeModule::inspectComponents` Editor hook. It never changes
  gameplay World or exposes native writeback of static T values.
  Live editor edits go to source assets and explicit revision migration
  (future work), NOT into immutable runtime tables.
- `ecs_static_scene_editor_contract_test` covers authoring and
  read-only Inspector schema; `aiverse_ecs_shared_static_rss_bench`
  samples actual process RSS separately for ordinary/shared/static
  and checksums rather than merely estimating payload sizes.
- User requested **complete feature work before any more native tests**.
  Do not run partial test cycles. After all Stage 5/6 code is committed
  with handoff, execute exactly ONE final Release gate:
  `bash tools/ecs-static-scene-validate.sh build/static-scene all 8 1000000`
  from existing isolated worktree. The `all` mode builds the full
  project, executes complete registered CTest once, and runs isolated
  1M-entity RSS read benchmark for 16/64/1024 unique configs.
  Record actual results, errors, exact SHA, platform and warnings;
  Stage 6 native run is not yet performed.

## Stage 7 — additive collision namespaces (2026-11-19)

- `SceneCollisionNamespaces` owns canonical, sorted instance-key
  reservations for the high half of `SpatialWorld`'s uint32 collider ID
  space. Each 65,536-ID slot allows 65,535 per-chunk colliders.
  Per-host namespaces are fixed before gameplay and leased through
  move-only RAII handles. Never assign by async completion order.
- `world::CollisionStream` remains the real SHA-256-verified async
  collision source. A new optional `CollisionIdRange` parameter
  assigns a disjoint range; the original three-argument call keeps
  its legacy guard. Do not bypass either reservation or occupancy
  validation.
- `SceneCollisionBridge` is an opt-in `SceneResourceAdapter`
  to `SceneLoadCoordinator`. Its `define` registers a stable
  resource key + scene instance key + existing WorldAssets manifest;
  `acquire` leases a range and creates the actual CollisionStream;
  `release` destroys it before returning the lease.
- Call `SceneCollisionBridge::prepareRegions` before Simulation::step
  for required physical regions, and hold `readyToStep` false until
  collision residency is complete. Never decide authoritative ticks
  based on worker wall-time readiness.
- Additive stream ID ranges are safe only when every stream in one
  SpatialWorld uses one host-owned namespace catalog. Multiple
  instances of the same scene asset require distinct instance keys.
  Neighboring terrain seam checks across separate streams and a
  global cross-stream publication budget are not yet provided.
- Native CTests:
  `ecs_scene_collision_namespaces_test`,
  `ecs_scene_collision_bridge_test`. Stage 7 code has NOT yet
  undergone native compilation or execution; user requested final
  one-shot full build/CTest after all features.
- See `docs/scene-collision-namespaces.md` and
  `docs/agent-handoffs/ecs-static-scene-stage7-2026-11-19.md`
  for contracts, limits, exact source/test status and continuation.


### Collision layout persistence after Stage 7

- Opt in to `SceneCollisionPersistence.h` from GameModule::setup:
  `registerCollisionNamespacePersistence(sim)` and
  `bindCollisionNamespaces(sim.world(),namespaces)`.
  Validate layout fingerprint after COW rollback or native disk load
  before allowing gameplay collision.
- Do not serialize leases, streams, worker queues, or GPU/physics
  pointers in the authoritative ECS. Only the stable layout
  fingerprint is a native v10 World resource.


## Stage 8 — repository skill catalog gate (2026-11-19)

- Skills introduced under `skills/<category>/<id>/SKILL.md` must be
  registered with the canonical generator in
  `tools/skills/catalog.py` (as a native package, meta policy or
  explicit standalone workflow/gameplay entry, as appropriate).
  Never update only the generated `skills/catalog.json`; run
  `python3 tools/skills/catalog.py generate` and verify with
  `python3 tools/skills/catalog.py check`.
- This branch's `aiverse-ecs-shared-static` is a native opt-in
  gameplay recipe, **not** a separate AIVERSE CMake library.
  It is registered with `runtime=none`, `domain=gameplay`,
  `target=null` and source
  `engine/sim/SharedStaticComponents.h`. Do not add a fictitious
  library alias to silence catalog validation.
- Full native CTest reported `skill_catalog` and `skill_routing`
  failing because this skill was absent from registration.
  See `docs/agent-handoffs/ecs-static-scene-stage8-2026-11-19.md`
  for the corrections and exact verification boundary.

## Native validation evidence (Mac Studio, 2026-11-19)

The final default Release one-shot validation in the isolated
`AIVERSE-static-scene` worktree has now completed successfully:
the script exited `SUCCESS: complete all run`, CTest elapsed
517.25 seconds, and all 9 separate-process RSS samples produced
matching checksums. Previous full run had 129 registered tests;
the successful snippet lacks its own aggregate count, so report
the source-proven successful *full gate*, not an invented test count.

For one million entities with 128-byte immutable config, Static
measured 36.21/38.77/39.05 MiB RSS deltas for 16/64/1024 unique
values compared to 272.77/276.60/280.48 MiB Ordinary; memory growth
saving ~86%, read speedup 2.1–2.6× in this synthetic benchmark.
This is process RSS delta, not whole-game RAM or GPU VRAM.

Do not rerun or invalidate the verified gate by changing production
code without a new full test. The only later branch changes so far
have been documentation. See Stage 9 handoff for complete native
evidence, exact sample numbers, tool limitations and merge checklist.
