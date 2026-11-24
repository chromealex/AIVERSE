# Agent rules: ECS mechanics inventory, FPS ECS authority migration, ECS 2.0, native ECS state registration

Moved verbatim from AGENTS.md on 2026-11-21 (AGENTS.md keeps the cross-cutting rules; this file is
mandatory reading before working in this area). Newest entries are at the end.

## Genre-independent ECS mechanics contract (2026-11-18)

**Full feature-by-feature migration inventory:** `docs/ecs-all-features-migration-plan.md`
(baseline 120 catalog packages, RTS tick stage map, FPS authority backlog, migration
waves and acceptance checks). Keep the inventory and handoff updated as packages
are added. A generic public library is NOT equivalent to converting existing
gameplay; track library publication and actual RTS/FPS adoption separately.

New reusable opt-in ECS packages:
- `AIVERSE::lib_ecs_timed_mechanics` — reload/fire gate, health/energy regen,
  despawn by tick; `skills/gameplay/aiverse-ecs-timed-mechanics/SKILL.md`.
- `AIVERSE::lib_ecs_locomotion` — component-based input/state/output around
  existing shared fixed-point `stepMotion`; `skills/gameplay/aiverse-ecs-locomotion/SKILL.md`.
- `AIVERSE::lib_ecs_inventory` — ordered, transactional inventory add/remove/
  transfer using the shared inventory policies;
  `skills/gameplay/aiverse-ecs-inventory/SKILL.md`.
All have cross-genre CTest/standalone experiment runner entries but **native
results remain unverified** until the suite executes. Keep the legacy FPS/RTS
systems in place until explicit one-writer authority, save/network and replay
parity tests pass. Continue annotating every code stage in
`docs/editor-unification-handoff.md`.

AIVERSE is **a composable game-mechanics engine**, not a collection of
hard-coded RTS/FPS implementations. Every new gameplay feature, library or
mechanic MUST follow the **Components + Systems** model:

1. **ECS component contracts.** Define minimal, reusable data-only components
   describing state, inputs, configuration and outputs. Place shared components
   in engine/libraries rather than a genre module. Do not own authoritative
   gameplay state in duplicated per-genre arrays or custom singleton worlds.
   Components should have stable IDs and deterministic save/rollback semantics.
2. **Independent systems.** Implement behavior as small, configurable systems
   operating on declared ECS component read/write sets and resources, with
   explicit update order, event/command outputs and structural barriers.
   A mechanic can be enabled/disabled or composed with other mechanics by
   registering systems/components; no genre-specific engine forks.
3. **Reuse one shared mechanic across genres.** RTS bullet, FPS projectile,
   missile, rocket, grenade and spell should use the **same** trajectory,
   collision query, impact/penetration/splash, lifetime and damage primitives.
   Genre adapters provide components, target selection, input and tuning, not
   duplicate projectile solvers. Extend this rule to locomotion, vehicles,
   health/damage, inventories, economy, navigation, physics and AI.
4. **Configuration instead of inheritance by genre.** Prefer capability-based
   feature flags, typed rules/assets and composition to `isFPS`/`isRTS`
   branches. Different projects can combine any mechanics regardless of genre.
5. **Native C++ and agent reuse.** Libraries must be callable from the native
   game/project module via public headers, without modifying engine core.
   Deliver corresponding AI skill documentation, stable interfaces and usage
   examples; reference them here and in the relevant handoff to prevent agents
   regenerating equivalent mechanics.
6. **Determinism first.** Fixed-point sim math, stable iteration/command merge,
   explicit RNG ownership, versioned persistence, per-tick replay/rollback
   and multi-worker checksum tests. Keep presentation/float effects out of
   authoritative systems. Confirm parity between old and new integrations
   before removing a legacy path.
7. **Migration without parallel source-of-truth copies.** Transitional adapters
   may support old FPS/RTS saves/netcode but must state the owner of canonical
   data, the barrier where mirrors are updated and when compatibility buffers
   can be deleted. Move one system at a time; preserve gameplay ordering.
8. **Required artifacts for every mechanic:** component schemas, system
   implementation, registration/configuration sample (RTS+FPS or two distinct
   host games), tests (including native linkage and save/rollback), matching
   skill, and update to living handoff. Do not mark production-ready without
   compiling and running applicable checks.

**Required shared shooting package:** use
`AIVERSE::lib_ecs_weapon_launch` (public
`"engine/mechanics/EcsWeaponLaunchSystems.h"`), composed with the existing
`ecs-projectile-systems` and `ecs-impact-damage` packages, for new
weapon/shot/impact gameplay. Do not implement separate projectile
trajectory/collision/damage math in `modules/rts` and `modules/fps`.
The native shooter/artillery cross-genre fixtures are
`ecs_cross_genre_weapon_pipeline_test` and
`ecs_cross_genre_projectile_test`. Existing RTS/FPS gameplay has NOT
yet been fully redirected to this package: preserve legacy deterministic
save/replay until actual parity tests pass.

The ongoing universal FPS/RTS migration must gradually consume these generic
mechanic libraries, not create new genre-specific gameplay engines.

**Real integer damage migration checkpoint (2026-11-18):** `fps::Module::damage`
and `addDamageResolutionSystem` both call
`lib::genre::applyIntegerHitPointDamage` after host mitigation.
The generic World `DamageQueue` exports v1
`saveDamageQueue/loadDamageQueue/hashDamageQueue`, which native hosts must
wire into their own GameModule byte save/checksum, not just COW snapshots.
Use `ecs_integer_health_commit_test` plus actual FPS ECS ON/OFF,
save v9 and rollback parity. This does **not** make `HitPointsComponent`
canonical for live FPS: the canonical FPS owner remains `ActorRuntime`
until a versioned migration. Do not create parallel mutable health copies.
See `skills/gameplay/aiverse-ecs-impact-damage/SKILL.md`.


**Optional queued ECS impact barrier:** for games that deliberately separate
motion from damage resolution, use `AIVERSE::lib_ecs_impact_queue`
(`<aiverselib/mechanics/EcsImpactQueueSystems.h>`,
`skills/gameplay/aiverse-ecs-impact-queue/SKILL.md`).
`OrderedImpactQueue` and its monotonically increasing sequence are World
resources that MUST be saved/hashed/rollbacked; callbacks retain independent
armor/faction/shield rules, and queued impacts run in canonical submission
order. The queue swaps pending requests at the barrier: callback-generated
hits run on the next invocation. Existing RTS immediate projectile damage
and same-tick shield/script side effects MUST NOT be moved into a single
end-of-tick queue without a golden parity test. The native cross-genre
fixture is `ecs_cross_genre_impact_queue_test`; compilation and real
RTS/FPS integration remain pending.

**Implemented hitscan capability split (2026-11-18):** Use
`AIVERSE::lib_ecs_hitscan` (`EcsHitscanSystems.h`) to select the nearest
valid fixed-point ray hit over current or rewound poses. For games that can
stage hits after a source ECS traversal, compose
`AIVERSE::lib_ecs_hitscan_resolution`
(`EcsHitscanResolutionSystems.h`, `HitscanIntentComponent`,
`addHitscanResolutionSystem`) with
`AIVERSE::lib_ecs_impact_damage` and its `DamageQueue`/resolution barrier.
Skills: `skills/gameplay/aiverse-ecs-hitscan/SKILL.md`,
`skills/gameplay/aiverse-ecs-hitscan-resolution/SKILL.md`.
Test: `ecs_hitscan_resolution_test` exercises two independent GameModule hosts,
invalid/blocked fire requests, stable multi-shooter order, full ECS health
reduction and deep/COW snapshot replay. As of this entry native execution
has **not** been verified.

**Do not silently change live FPS timing:** `engine/actor/ActorSystems.cpp`
still resolves authoritative hits synchronously within the legacy tick,
including rewind, armor, head/limb multipliers and kill/loot hooks. It reuses
shared hitscan and ordered-impact policies, but the independent hitscan
resolution system stages hit events until after traversal. Switching the live
FPS to it requires same-tick damage ordering golden tests and network/save
checksum parity. The kinematic no-physics RTS path and physical-ballistic
path remain independently composable; no `isFPS`/`isRTS` motion flag.

**Actual shared projectile collision adoption (2026-11-18):** the live FPS
`ActorProjectiles.cpp` programmed-projectile stage now calls
`selectSweptProjectileContact` in
`AIVERSE::lib_ecs_projectile_systems` (`EcsSweepQuerySystems.h`).
Selects nearest dynamic target with fixed-point swept bounds, endpoint
fraction=1 and stable semantic-ID ties. It keeps the older wall, penetration,
damage and projectile-program side effects in the original order. Its
existing `tests/fps/projectiles.inc::ecsProjectileProgramStageParity` tests
legacy/ECS actor and projectile state plus save v9 and deep/COW snapshots;
`ecs_projectile_composition_test` additionally covers swept contact
selection in independent genre hosts. Neither has been executed in this
workspace.

`ActorSystems.cpp::traceShot` now synchronously calls the **same**
`resolveHitscanIntent` pure policy as the standalone
`addHitscanResolutionSystem`, and uses the generic ray selection policy for
vehicles too. The live FPS damage still commits immediately and uses
`applyIntegerHitPointDamage` after gear/armor and knockdown rules.
For any future conversion to queued impacts or scheduled FPS fire, first run
`tests/fps/tests.cpp::ecsSimultaneousHitscanRollbackParity` and network
rollback/parity tests. The standalone resolution system uses deferred callback
order and must not silently replace synchronous FPS multi-pellet execution.
Select physical sweep, non-physical pursuit or instant hitscan per *weapon
capability*, never per genre.

**FPS projectile state migration, current branch:** in
`actor.ecsAuthority=true`, `actor.compat.projectilePrograms` now runs
`Module::stepProgrammedProjectile` directly against authoritative
`ProjectileRuntime` components, sorting by recorded `order`/ID and applying
immediate hits before actor updates. The default FPS mode calls that **same**
solver from the legacy source vector. New branches/expiry are committed to
the ECS World at a structural barrier and projected into
`State::projectiles` for remaining legacy game mode/save/network readers.
**Live accepted weapon shots, Frag/Smoke/Flash actions, and bomb-round reset
now use direct `Module::spawnProjectile` / `clearProjectiles` ECS
Entity creation/deletion when `ecsAuthority` is enabled.**
The State array remains a compatibility workspace for other FPS systems;
do not reintroduce legacy-only spawns or a second solver. See
`tests/fps/projectile_spawn.inc` for deep/COW, v9 and reset parity.
**Do not switch a second projectile solver or reintroduce a writable State
source into this phase.** Mixed nonprogrammed objects, Entity-generation
recycling, COW, v9 saves, rewind and synchronous damage order must retain
parity; see `ecsProjectileProgramStageParity` and
`ecsProjectileBranchAuthorityParity` in `tests/fps/projectiles.inc`.
Native test execution is still pending.

**Live FPS ordinary projectile ECS stage (2026-11-18):**
When `actor.ecsAuthority=true`, both the programmed and ordinary
projectile flight stages now advance **canonical ProjectileRuntime ECS
components**. Ordering is `actor.ecsHydrate -> fps.compat.projectilePrograms
-> fps.compat.ordinaryProjectiles -> fps -> fps.ecsMirror`.
The ordinary-stage `stepOrdinaryProjectile` solver is reused by the
legacy mode and ECS mode, preserves exact instantaneous hit/fuse/smoke
order and uses shared `stepBouncingProjectile` from
`AIVERSE::lib_ecs_projectile_systems` for physical grenade motion.
RTS abstract missiles use separate kinematic motion without physics.

Do not treat the compatibility `State::projectiles` array as canonical
between ECS flight barriers. Other FPS round/actor systems can still
write that array in the main tick; the mirror reconciles it until those
systems migrate. Preserve FPS v9 save/net format and simultaneous-damage
ordering. Run `ecsOrdinaryProjectileAuthorityParity` (real FPS bullets,
grenades, timed smoke, ECS-only edits, deep/COW snapshots, cross-v9
reload), `ecsProjectileProgramStageParity`, `ecs_projectile_composition_test`,
`fps` and `fps_transport` before merging or enabling by default.
All these native results are currently unverified.

**Critical FPS projectile single-writer rule (2026-11-18):**
When `actor.ecsAuthority=true`, the **entire live FPS projectile lifecycle**
(accepted weapon and grenade spawn, programmed/ordinary flight, impacts,
smoke expiry and bomb-round reset) now writes canonical
`ProjectileRuntime` ECS components. `actor.ecsMirror` must use
`syncDynamicEntities(sim,true)` to derive `State::projectiles` as a
one-way legacy/save/network representation, **never** copying stale
State values back onto Runtime. Preserve the original
`syncDynamicEntities(Simulation&)` overload for legacy v9 load/setup,
which is intentionally State→Runtime. New projectile features must
write ECS components/commands, not the State vector. Native proof is
still required; `tests/fps/projectile_spawn.inc` includes a
Runtime=17 / stale-State=777 mirror regression plus deep/COW save and
round-reset fixtures.

## FPS ECS authority migration (PR #7, in progress)

**Current working branch:** `feature/universal-entity-inspector-fps`. PR #8
has been merged as an ancestor into PR #7; do not restore outdated FPS
`ActorModule.cpp` or Inspector sources from the standalone PR #8 branch.

**Mandatory living handoff:** `docs/editor-unification-handoff.md`,
latest FPS stages A–S. After EVERY implementation commit, append the exact
SHA, changed files, behavior, verification status and next action to the
handoff in a separate documentation commit before taking another action.

`actor.ecsAuthority` is an experimental default-OFF storage migration gate.
When ON, full `ActorRuntime`, `ProjectileRuntime`, `LootRuntime` and
`VehicleRuntime` components are canonical between ticks and define
checksum/save v9 entity payloads. `actor.ecsHydrate` creates the ordered
legacy tick workspace; old algorithms not yet migrated still read/write it.
`actor.ecsMirror` commits the tick results to ECS and refreshes read-only
Editor projections. Commands, actor motor and centralized damage already
have direct ECS code paths. **Do NOT call the migration complete.**

Next migrate direct gameplay systems one by one, verify same-tick side-effect
ordering and byte-identical checksums to the default mode, then remove the
temporary State arrays once all uses are gone. Legacy save v9 and network
format must remain compatible until an explicitly versioned migration.
Run `aiverse_fps_tests`, `fps_transport`, checksum replay, sanitizer and
universal Editor tests on the integrated branch before main merge.

## ECS 2.0 experimental work (PR #8)

**Parallel migration coordination (2026-11-18):** another agent is actively
moving RTS and FPS to a universal ECS architecture. Do not concurrently
rewrite RTS/FPS gameplay systems or change legacy world-space `Transform`
semantics from the ECS architecture PR. Keep `Parent`, `LocalTransform`,
`WorldTransform` and `SceneHierarchy` generic under `engine/sim`. Share
the integration contract in `docs/ecs-agent-handoff.md`, and merge/cherry-pick
only verified core changes into the genre migration. Both agents must avoid
overlapping edits to `modules/rts` and `modules/fps`.

For nested gameplay entities and native project object hierarchies, read
`docs/ecs-scene-hierarchy.md` and use `Parent` / `LocalTransform` /
`WorldTransform` with `SceneHierarchy`. Do not reinterpret RTS
`Transform` as local space, or feed float presentation transforms into
deterministic simulation. Keep hierarchy topology generation-safe; rebuild
the derived graph on save/load and update only dirty branches.

For any ECS storage, scheduler, rollback or parallelism change, **first read**
`docs/ecs-agent-handoff.md`. This is the continuously updated handoff for
`feature/ecs-architecture-benchmarks` and PR #8. It contains prior benchmark
results, verified tests, exact commits, limitations and remaining work.
Update it after **each** ECS development step, preserving a clear distinction
between code pushed and tests actually run. Production ECS remains deterministic
Sparse Set; new scheduler/guarded interfaces are opt-in experiments. Do not
enable arbitrary existing RTS/FPS gameplay systems in parallel or merge PR #8
without complete test and lockstep/replay validation.

## Native ECS state registration (2026-11-18)
Native authoritative ECS components/resources MUST register during setup via `Simulation::registerPersistentComponent<T>` / `registerPersistentResource<T>` with stable namespaced ID and positive schema version, plus portable ADL `io(Archive&, T&)`. Save v10 is enabled only when needed; v9 projects remain unchanged. Version mismatches fail closed. See `docs/native-ecs-persistence.md` and `tests/ecs_native_persistence_test.cpp`. Verify native builds and rollback before production adoption.

### Native schema upgrades and transactional load (2026-11-18)
Register exact-version legacy component/resource readers only through `registerPersistentComponentMigration<T>` and `registerPersistentResourceMigration<T>`. Native ECS migration validates the ORIGINAL source field stream against the recorded checksum; never bypass checksums for an upgraded save. Do not hash registered ECS data twice in GameModule::hashState. Disk deserialize now restores Simulation-owned World/RNG/commands/players on invalid input; game-module private derived caches must use `addStateRestoredHook` and remain outside authoritative simulation state. See `docs/native-ecs-persistence.md` and `tests/ecs_native_persistence_test.cpp`. Native execution still requires CI proof.

