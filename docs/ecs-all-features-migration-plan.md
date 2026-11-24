# AIVERSE — full ECS migration register and execution plan
> Inventory of `libraries/catalog.json` at branch `feature/universal-entity-inspector-fps`, 2026-11-18. Generated from source/catalog inspection; not an assertion that all packages are integrated or tested.

## Scope and definition
Goal: **every authoritative gameplay feature is an opt-in composition of reusable data-only ECS components, deterministic systems and independently reusable algorithms.** No gameplay code branches on FPS/RTS genre. A genre is a preset (system list/config/camera/input), not a class owning mechanics.

**Do not convert all 120 catalog entries to ECS**: 78 simulation / 21 presentation / 21 tools. Algorithms (math/graphs), packaging/editor/MCP, GPU/graphics/UI, network transport and persistent meta-service keep their own architecture. Presentation may consume read-only ECS projections. Meta-game/player-account data stays in an externally transactional service; only in-match mechanics use World components.

### Present state, grounded in source
- `engine/game/GameCore.cpp` registers 41 named ordered system stages, but module-aware systems still use `GameCore&`, `GameState` and `Unit`, with a large `applyCommand` dispatcher.
- `engine/actor/ActorModule.cpp` stages `actor.ecsHydrate -> fps -> fps.ecsMirror` when `actor.ecsAuthority` is on, with `ActorRuntime`, `ProjectileRuntime`, `LootRuntime` and `VehicleRuntime` storage. The `fps` tick itself still runs many algorithms through `State::actors` and dynamic vectors; legacy saves are v9.
- Generic public components/systems exist in three opt-in packages: `ecs-weapon-launch`, `ecs-projectile-systems`, `ecs-impact-damage`. These have not replaced real FPS/RTS gameplay. Native test success is **unverified**.
- `engine/core/ECS.h` supports deterministic sparse-set World and experimental COW; `engine/sim/CommonComponents.h` has `Transform`, `Owner`, `Parent`, `LocalTransform`, `WorldTransform`. ECS is the substrate, not evidence that a module is fully universal.
- In-match `GameCore` and `fps::Module` must become composition/adapters. `modules/rts` and `modules/fps` remain for template compatibility until snapshots/replays/network pass.

## Non-negotiable sequencing and guarantees
1. Freeze per-game golden inputs, snapshots, checksums, save bytes, authoritative network streams and relevant performance baselines **before** switching a mechanic.
2. Each migrated feature has (a) stable component IDs/schema and fixed-point/integer state; (b) validation/config; (c) neutral pure policy; (d) system with explicit reads/writes/resources and deterministic stage; (e) typed events/deferred mutations; (f) project-native public target; (g) two unlike hosts + combined-mechanics test; (h) skill and documentation.
3. One canonical writer per field. Legacy arrays may be snapshots/workspaces for an explicit adapter only. No continuous two-way ECS/legacy syncing. Remove old authority only after parity; version save/network explicitly if old bytes cannot be preserved.
4. Preserve RTS stage order and FPS same-tick order for reload, input, movement, hit validation, damage, death, loot, scoring and triggers. Inputs are Commands. RNG is consumed in stable order; deferred structural mutation at barriers. World-derived indices are reconstructed after load/rollback.
5. Generic subfeatures should be independently enableable: ballistic/hitscan/guidance; inventory/gear; locomotion/vehicle/ladder; health/status; unit/building/character/missile/ship. An Entity can mix any of them.
6. Never turn renderer, GPU batching/LOD, road mesh generation, meta payments, transport or the editor into gameplay ECS. Their state is either authoring, presentation, build-time or external service state.
7. Maintain the existing protocol, player behavior and tests. Every implementation commit is followed by a separate living-handoff commit with SHA, paths, outcomes, proof and next action.

## Waves and dependency graph
| Wave | Priority | Concrete migration target | Success gate |
|---|---|---|---|
| 0 | P0 | Baseline/contract, component registry+reflection, shared entity identity, deterministic scheduler barriers, ECS serializer/hash/snapshot, source ownership map; save v9/read compatibility | ECS suite, copy-on-write rollback, native app+CI, no performance regression |
| 1 | P0 | Commands and input intents; generic `Health`/`Damage`/`Armor`/`Death`; `Weapon`/`Reload`/`ProjectileFlight`/`Impact`/AoE/penetration/status; replace RTS `sysWeapons/sysProjectiles` and FPS `shoot/traceShot` behind parity gates | FPS/RTS mixed shooting, rollback/checksum/kill-credit/lag compensation |
| 2 | P0 | `Transform`, `Velocity`, movement/collision/nav/avoidance, stances, character/vehicle/space locomotion, navigation agent/path/target, world/voxel query interfaces | RTS/FPS/vehicle/nav/planet/streaming parity, deterministic path order |
| 3 | P1 | `Inventory`, `Item`, `Equipment`, `ResourceWallet`, `Producer`, `Construction`, `Crafting`, `Job`, `Squad`, `Interaction`, `Reservation`, skills | RTS economy/supply/build/sell + FPS loot/buy/loadout parity |
| 4 | P1 | Shared AI senses, memory, threats, behavior, bots, squad/stealth/traffic; authored modes, objectives, scores, respawn, survival, weather, raid/royale | Bots and modes in both hosts, stable multi-agent decision/RNG order |
| 5 | P2 | Turn/card/tactics, narrative/cinematic, puzzles, settlement/survival, progression, time/calendar, cooldown and status; independent optional systems | At least two hosts for each library and hybrid feature combinations |
| 6 | P1 | Editor ECS inspection/authoring/reflection, schema registry, entity prefab/data composition, native game project registry, project migration tools; remove legacy authorities | One editor Hierarchy/Inspector, hot reload, save/network migration + smoke builds |
| 7 | P0 | Full regression matrix and staged default switch, delete unused legacy code only after complete parity | CI Linux/macOS/Windows, native builds, sanitizers, golden replays, network test, headless 10k-tick checks |

**Dependency rule:** 0 -> 1 -> 2 -> 3/4 -> 5 -> 6 -> 7. A cross-cutting subsystem may migrate earlier if it has independent tests and no conflicting ownership. Do not parallel-edit `modules/rts` and `modules/fps` across agents; split work by reusable library and assign one integration owner per file.

## RTS registered tick stages (source order)
`economy -> fields -> production -> construction -> settlers -> lanes -> garrison -> waves -> neutrals -> dungeon -> orders -> mech -> abilities -> effects -> originals -> fidelity -> targeting -> fieldops -> movement -> transit -> weapons -> projectiles -> encounters -> commerce -> raid -> progression -> environment -> death -> capture -> creep -> gridfields -> vision -> environment3 -> modes -> victory -> triggers -> scripts -> matchstats -> traffic -> outcome -> gameplayClock`

Work from command dispatch, queues and phase ordering toward small neutral systems; do not mistake a named `sim.addSystem("economy", [this] ...)` callback for a genre-independent library.

## All 120 catalog entries, migration disposition
`ECS`: implement/finish component + system + binding to real game hosts (including existing partial ECS libraries). `POLICY`: keep algorithm/policy pure; provide optional ECS adaptation where a caller needs gameplay state. `ADAPTER`: consume ECS readonly events/entities in presentation only. `KEEP`: tool/renderer/network/meta/service code, no ECS ownership.

### P1 — 10 packages
| ID | Existing code | Disposition | Action |
|---|---|---|---|
| `abilities` | `libraries/include/aiverselib/gameplay/Abilities.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `gear-tiers` | `engine/systems/Gear.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `attribute-modifiers` | `libraries/include/aiverselib/systems/EngineMechanics.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `status-effects` | `libraries/include/aiverselib/systems/EngineMechanics.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `melee-combat` | `engine/mechanics/Melee.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `combat-actions` | `engine/mechanics/CombatActions.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `weapon-policies` | `engine/mechanics/Weapons.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `ecs-projectile-systems` | `engine/mechanics/EcsProjectileSystems.h` | ECS | Already generic; integrate legacy hosts and verify |
| `ecs-impact-damage` | `engine/mechanics/EcsDamageSystems.h` | ECS | Already generic; integrate legacy hosts and verify |
| `ecs-weapon-launch` | `engine/mechanics/EcsWeaponLaunchSystems.h` | ECS | Already generic; integrate legacy hosts and verify |

### P2 — 11 packages
| ID | Existing code | Disposition | Action |
|---|---|---|---|
| `spatial` | `engine/game/GameCore.h` | POLICY + ECS adapter | Inspect existing implementation; do not duplicate |
| `grid` | `libraries/include/aiverselib/Grid.h` | POLICY + ECS adapter | Extract state+systems; preserve and reuse existing policy |
| `navigation` | `docs/pathfinding.md` | POLICY + ECS adapter | Inspect existing implementation; do not duplicate |
| `navmesh` | `engine/bots/NavMesh.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `character-locomotion` | `engine/mechanics/Locomotion.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `zone-graph` | `libraries/include/aiverselib/strategy/ZoneGraph.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `physical-interactions` | `libraries/include/aiverselib/mechanics/PhysicalInteractions.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `vehicle-controllers` | `engine/mechanics/Vehicles.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `swept-shapes` | `engine/core/SweptShapes.h` | POLICY + ECS adapter | Extract state+systems; preserve and reuse existing policy |
| `city-traffic` | `engine/mechanics/Traffic.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `voxel-nav` | `libraries/include/aiverselib/bots/voxelnav/VoxelNav.h` | ECS | Extract state+systems; preserve and reuse existing policy |

### P3 — 15 packages
| ID | Existing code | Disposition | Action |
|---|---|---|---|
| `tasks` | `libraries/include/aiverselib/Tasks.h` | POLICY + ECS adapter | Extract state+systems; preserve and reuse existing policy |
| `logistics` | `libraries/include/aiverselib/Logistics.h` | POLICY + ECS adapter | Extract state+systems; preserve and reuse existing policy |
| `progression` | `engine/core/progression/Progression.h` | POLICY + ECS adapter | Extract state+systems; preserve and reuse existing policy |
| `objectives` | `libraries/include/aiverselib/Objectives.h` | POLICY + ECS adapter | Extract state+systems; preserve and reuse existing policy |
| `construction` | `libraries/include/aiverselib/gameplay/Construction.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `inventory` | `engine/gameplay/Inventory.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `squads` | `libraries/include/aiverselib/gameplay/Squads.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `tutorial` | `libraries/include/aiverselib/gameplay/Tutorial.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `object-interactions` | `libraries/include/aiverselib/systems/Interactions.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `reservations` | `libraries/include/aiverselib/systems/Reservations.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `action-sequences` | `libraries/include/aiverselib/systems/Sequence.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `loot-tables` | `engine/core/progression/Loot.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `offline-production` | `libraries/include/aiverselib/strategy/Offline.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `production-networks` | `libraries/include/aiverselib/strategy/Production.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `settlement-production` | `libraries/include/aiverselib/mechanics/Colony.h` | ECS | Extract state+systems; preserve and reuse existing policy |

### P4 — 32 packages
| ID | Existing code | Disposition | Action |
|---|---|---|---|
| `save-migrations` | `libraries/include/aiverselib/gameplay/Saves.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `tag-conditions` | `engine/systems/Tags.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `progression-curves` | `engine/systems/Curves.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `ai-perception` | `libraries/include/aiverselib/systems/Perception.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `bots-commander` | `libraries/include/aiverselib/bots/Rts.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `bots-combatant` | `libraries/include/aiverselib/bots/Fps.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `building-gen` | `libraries/include/aiverselib/systems/BuildingGen.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `stealth-senses` | `libraries/include/aiverselib/mechanics/Senses.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `cinematic-gameplay` | `libraries/include/aiverselib/mechanics/Cinematic.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `card-games` | `libraries/include/aiverselib/mechanics/CardGame.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `tactics-grid` | `libraries/include/aiverselib/mechanics/TacticsTurns.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `cards-turns-bots` | `libraries/include/aiverselib/bots/CardBot.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `action-bots` | `libraries/include/aiverselib/bots/GenreBots.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `match-rules` | `libraries/include/aiverselib/mechanics/Match.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `battle-royale` | `libraries/include/aiverselib/mechanics/royale/BattleRoyale.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `narrative-graphs` | `libraries/include/aiverselib/mechanics/Narrative.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `turn-scheduler` | `libraries/include/aiverselib/strategy/Turns.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `card-runtime` | `libraries/include/aiverselib/strategy/Cards.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `grid-puzzles` | `libraries/include/aiverselib/strategy/Puzzles.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `puzzle-solvers` | `libraries/include/aiverselib/mechanics/PuzzleSolvers.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `rhythm-timing` | `libraries/include/aiverselib/strategy/Rhythm.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `run-graph` | `libraries/include/aiverselib/strategy/RunGraph.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `puzzle-machine` | `libraries/include/aiverselib/strategy/PuzzleMachine.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `sports-rules` | `libraries/include/aiverselib/mechanics/Sports.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `survival-ecology` | `libraries/include/aiverselib/mechanics/SurvivalEcology.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `boss-encounters` | `engine/mechanics/Telegraphs.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `match-progression` | `engine/mechanics/Perks.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `world-calendar` | `libraries/include/aiverselib/systems/Calendar.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `voxel-world` | `libraries/include/aiverselib/systems/voxel/VoxelWorld.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `voxel-light` | `libraries/include/aiverselib/systems/voxellight/VoxelLight.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `voxel-gen` | `libraries/include/aiverselib/systems/voxelgen/VoxelGen.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `voxel-ticks` | `libraries/include/aiverselib/systems/voxelticks/VoxelTicks.h` | ECS | Extract state+systems; preserve and reuse existing policy |

### KEEP — 52 packages
| ID | Existing code | Disposition | Action |
|---|---|---|---|
| `geometry` | `engine/render/Geometry.h` | ADAPTER | Inspect existing implementation; do not duplicate |
| `math` | `engine/core/FixedMath.h` | POLICY + ECS adapter | Inspect existing implementation; do not duplicate |
| `collections` | `libraries/include/aiverselib/Collections.h` | POLICY + ECS adapter | Extract state+systems; preserve and reuse existing policy |
| `graphs` | `engine/core/Graphs.h` | POLICY + ECS adapter | Extract state+systems; preserve and reuse existing policy |
| `behavior` | `libraries/include/aiverselib/Behavior.h` | POLICY + ECS adapter | Extract state+systems; preserve and reuse existing policy |
| `rules` | `libraries/include/aiverselib/Rules.h` | POLICY + ECS adapter | Extract state+systems; preserve and reuse existing policy |
| `generation` | `engine/game/gen/MapGen.h` | POLICY + ECS adapter | Inspect existing implementation; do not duplicate |
| `presentation` | `libraries/include/aiverselib/Presentation.h` | ADAPTER | Extract state+systems; preserve and reuse existing policy |
| `ui-patterns` | `engine/ui/templates/README.md` | ADAPTER | Inspect existing implementation; do not duplicate |
| `verification` | `tests/check_headless_restore.py` | KEEP | Inspect existing implementation; do not duplicate |
| `input` | `engine/gameplay/Input.h` | ADAPTER | Extract state+systems; preserve and reuse existing policy |
| `camera` | `engine/gameplay/Camera.h` | ADAPTER | Extract state+systems; preserve and reuse existing policy |
| `feedback` | `libraries/include/aiverselib/gameplay/Feedback.h` | ADAPTER | Extract state+systems; preserve and reuse existing policy |
| `diagnostics` | `engine/gameplay/Diagnostics.h` | KEEP | Extract state+systems; preserve and reuse existing policy |
| `content-pipeline` | `tools/libraries/gameplay.py` | KEEP | Extract state+systems; preserve and reuse existing policy |
| `game-verification` | `tools/libraries/gameplay.py` | KEEP | Extract state+systems; preserve and reuse existing policy |
| `project-recipes` | `tools/libraries/gameplay.py` | KEEP | Extract state+systems; preserve and reuse existing policy |
| `configuration` | `engine/gameplay/Configuration.h` | KEEP | Extract state+systems; preserve and reuse existing policy |
| `ui-binding` | `libraries/include/aiverselib/app/Binding.h` | ADAPTER | Extract state+systems; preserve and reuse existing policy |
| `screens` | `libraries/include/aiverselib/app/Screens.h` | ADAPTER | Extract state+systems; preserve and reuse existing policy |
| `themes` | `libraries/include/aiverselib/app/Theme.h` | ADAPTER | Extract state+systems; preserve and reuse existing policy |
| `match-bridge` | `libraries/include/aiverselib/app/MatchBridge.h` | ADAPTER | Extract state+systems; preserve and reuse existing policy |
| `app-lifecycle` | `libraries/include/aiverselib/app/Lifecycle.h` | ADAPTER | Extract state+systems; preserve and reuse existing policy |
| `network-session` | `libraries/include/aiverselib/app/Session.h` | ADAPTER | Extract state+systems; preserve and reuse existing policy |
| `content-mods` | `libraries/include/aiverselib/app/Content.h` | KEEP | Extract state+systems; preserve and reuse existing policy |
| `balance-lab` | `libraries/include/aiverselib/app/Balance.h` | KEEP | Extract state+systems; preserve and reuse existing policy |
| `play-scenarios` | `libraries/include/aiverselib/app/Scenario.h` | KEEP | Extract state+systems; preserve and reuse existing policy |
| `performance-budgets` | `libraries/include/aiverselib/app/Performance.h` | KEEP | Extract state+systems; preserve and reuse existing policy |
| `template-kits` | `libraries/include/aiverselib/app/Genre.h` | KEEP | Extract state+systems; preserve and reuse existing policy |
| `change-planner` | `libraries/include/aiverselib/app/Planner.h` | KEEP | Extract state+systems; preserve and reuse existing policy |
| `native-scaffold` | `tools/libraries/native_codegen.py` | KEEP | Extract state+systems; preserve and reuse existing policy |
| `typed-state` | `tools/libraries/native_codegen.py` | KEEP | Extract state+systems; preserve and reuse existing policy |
| `typed-commands` | `tools/libraries/native_codegen.py` | KEEP | Extract state+systems; preserve and reuse existing policy |
| `reference-validation` | `tools/libraries/project_checks.py` | KEEP | Extract state+systems; preserve and reuse existing policy |
| `hot-reload` | `libraries/include/aiverselib/runtime/HotReload.h` | ADAPTER | Extract state+systems; preserve and reuse existing policy |
| `bounded-memory` | `libraries/include/aiverselib/runtime/Bounded.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `work-budget` | `libraries/include/aiverselib/runtime/WorkBudget.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `state-diff` | `libraries/include/aiverselib/runtime/StateDiff.h` | KEEP | Extract state+systems; preserve and reuse existing policy |
| `library-upgrades` | `tools/libraries/project_checks.py` | KEEP | Extract state+systems; preserve and reuse existing policy |
| `library-showcase` | `examples/libraries/showcase/main.cpp` | ADAPTER | Inspect existing implementation; do not duplicate |
| `decision-explanations` | `libraries/include/aiverselib/systems/Decisions.h` | ADAPTER | Extract state+systems; preserve and reuse existing policy |
| `bot-matches` | `examples/libraries/bots/main.cpp` | KEEP | Extract state+systems; preserve and reuse existing policy |
| `big-numbers` | `libraries/include/aiverselib/strategy/BigNumber.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `session-admission` | `libraries/include/aiverselib/net/Admission.h` | ADAPTER | Extract state+systems; preserve and reuse existing policy |
| `snapshot-delta` | `engine/net/SnapshotDelta.h` | ADAPTER | Extract state+systems; preserve and reuse existing policy |
| `physics` | `engine/physics/World.h` | ADAPTER | Inspect existing implementation; do not duplicate |
| `weather-timeline` | `libraries/include/aiverselib/systems/WeatherTimeline.h` | ADAPTER | Extract state+systems; preserve and reuse existing policy |
| `vehicle-fx` | `libraries/include/aiverselib/mechanics/vehicle-fx/VehicleLights.h` | ADAPTER | Extract state+systems; preserve and reuse existing policy |
| `derived-keys` | `libraries/include/aiverselib/runtime/DerivedKeys.h` | ECS | Extract state+systems; preserve and reuse existing policy |
| `rules-files` | `tools/rules/rules_io.py` | KEEP | Extract state+systems; preserve and reuse existing policy |
| `voxel-mesh` | `libraries/include/aiverselib/systems/voxelmesh/VoxelMesh.h` | ADAPTER | Extract state+systems; preserve and reuse existing policy |
| `voxel-replication` | `libraries/include/aiverselib/net/voxel/VoxelReplication.h` | KEEP | Extract state+systems; preserve and reuse existing policy |


## Mandatory implementation registry for each feature
Track `id`, `owner source path`, `legacy state`, `ECS canonical state`, `components`, `systems/order`, `native target`, `editor schema`, `save/network version`, `RTS integration`, `FPS integration`, `other-host integration`, `tests and platform evidence`, `merge SHA`. Statuses: `legacy`, `policy-only`, `ecs-library-unverified`, `host-integrated-unverified`, `parity-verified`, `retired`.

## Next concrete steps for agent
1. Implement component/system adapters and native cross-genre tests for reload, health/status and projectile options. **Do not prematurely call them migrated in FPS/RTS.**
2. Wire authoritative FPS firing or RTS weapon flight to the shared ECS weapon pipeline one stage per commit, preserving exact existing ticks/shot IDs, rewind, AoE and team filtering.
3. Replace FPS `State::actors` authority field by field (rather than keeping `ActorRuntime{Actor}` as the final design), then vehicles, loot, projectiles. No two mutable owners.
4. Split RTS `GameState` into world resources for match/global grids and Entity components for per-entity state; remove `GameCore&` dependency from shared mechanics.
5. Extend inspector/schemas and reflect from actual registered components; test mixed RTS/FPS/native entities.


## Incremental execution: queued ECS impact resolution (2026-11-18)

The original 120-entry inventory above is a **baseline snapshot**, not the
current mutable catalog count. New packages added on the working branch must
be checked against `libraries/catalog.json` before counting or marking work
complete.

**New independent, opt-in ECS system:** `ecs-impact-queue` (public
`AIVERSE::lib_ecs_impact_queue` and
`libraries/include/aiverselib/mechanics/EcsImpactQueueSystems.h`). It builds on
the shared synchronous `ecs-ordered-impact` pure policy. Weapon/projectile
adapters can enqueue `OrderedProjectileImpact` records into a bounded
rollback-owned `OrderedImpactQueue` World resource. A separately registered
`addOrderedImpactResolutionSystem` resolves direct hit, on-hit policy and
splash candidates in stable enqueue order, using injected faction/armor/
spatial callbacks rather than hardcoded genre branches. Callback-generated
requests wait for the next barrier; sequence ownership is in ECS World.

**Tests:** `tests/ecs_cross_genre_impact_queue_test.cpp` and CTest
`ecs_cross_genre_impact_queue_test` exercise shooter/strategy hosts,
callback ordering, friendly-fire filtering, nested submission and COW
rollback. The standalone experiment suite also references the fixture.
These fixtures exist in source; their native compilation, execution and
cross-platform reproducibility are **not yet verified**.

**Do not mark wave 1 complete:** built-in RTS uses immediate damage within
its projectile ECS traversal and programmed impacts may have different
on-hit/splash ordering. The queue system is not wired into that gameplay,
nor the FPS authoritative damage/hitscan path. Replacing immediate damage
with a global end-of-tick batch would change hit-shield/kill/script ordering.
Require real RTS/FPS golden replay, save, rollback, network and mixed-mechanics
tests before switching source ownership or system barriers.


## Migration checkpoint: shared integer HP and ECS damage queue (2026-11-18)

- The real `fps::Module::damage` and generic `addDamageResolutionSystem`
  now use one `applyIntegerHitPointDamage` fixed-integer HP subtraction
  policy from `EcsDamageSystems.h`. The existing FPS gear/armor,
  knockdown, kill credit, loot and event ordering are preserved by calling
  the shared helper at the prior immediate HP commit point. This is
  **shared-policy adoption**, not separate FPS `HitPointsComponent`
  authority: `ActorRuntime` remains canonical in `actor.ecsAuthority`.
- The generic `DamageQueue` now provides explicit v1 endian-independent
  `saveDamageQueue` / transactional `loadDamageQueue` /
  `hashDamageQueue` helpers with size bounds and generation-safe Entity
  fields. A GameModule must opt in to these helpers from its serialization
  and hashing hooks; the old FPS save v9 has not been changed.
- The independent `ecs_integer_health_commit_test` verifies 0..100 HP
  and 0..1000 incoming damage parity with original FPS arithmetic,
  nonpositive/maximum integer boundaries, queue byte roundtrip,
  truncation/corruption rejection, checksum sensitivity and bounds.
  CMake CTest and standalone ECS runner register it.
- **Unverified:** all native compiler executions, sanitizer, FPS/RTS
  scenario replay, save migration and network rollback. Do not mark
  combat wave complete or delete synchronous legacy damage logic.


## FPS programmed-projectile stage migration checkpoint (2026-11-18)

The actual FPS runtime now registers three consecutive stages in order:

```
fps.ecsHydrate              [only when ecsAuthority=true]
fps.compat.projectilePrograms
fps                        [tickSystems -> royale -> rewind capture -> actors -> vehicles -> outcomes]
fps.ecsMirror               [editor projection and ECS canonical commit]
```

The previously first statement of `Module::tick`, `tickProjectilePrograms`,
was moved into `actor.compat.projectilePrograms`, with the same winner guard.
This is an independently named `Simulation` stage and a **real extraction
from the monolithic game loop**, but it still uses legacy `State::projectiles`
and `State::actors` as compatibility workspaces. In ECS-authority mode
these are hydrated from `ProjectileRuntime` and `ActorRuntime` before
the stage, then committed at the established mirror barrier.

No projectile solver, collision policy, RPG ability, save v9 wire field, or
inter-tick ordering was changed in this step. It intentionally retains
immediate damage/side effects before the rest of the FPS tick; moving
impacts to `addOrderedImpactResolutionSystem` is **not** equivalent without
proving same-tick order.

**Acceptance fixture:** `ecsProjectileProgramStageParity` in
`tests/fps/projectiles.inc`, called by `aiverse_fps_tests`, exercises a
real programmed fly-through shot, compares default/ECS authority checksums
and health after every tick, cross-loads v9 byte saves in both directions,
restores a pre-flight `SimSnapshot` and replays, both with deep and COW
World snapshots. This fixture has been committed but **not compiled or run**.

**Next ownership cutover:** extract per-projectile state and the full
spawn/branch/collision/damage ordering from the State workspace into
independent components and explicit stages. The staged program executor
should then read/write `ProjectileRuntime` (or a decomposed public
`ProjectileFlight` + `ProjectileProgram` composition), and share the
common `ProjectileProgram` math across FPS/RTS. Rebuild ordered legacy
views only for save v9 and network compatibility, never as a second
writable authority. No default switch until FPS/RTS replay/save/transport
and editor integration tests pass natively.


## FPS direct ProjectileRuntime flight ownership checkpoint (2026-11-18)

The live `actor.compat.projectilePrograms` stage has now been updated on the
`actor.ecsAuthority=true` path: the **simulation of programmed projectile
flight, collision, immediate impact and branch spawn mutates the canonical
`ProjectileRuntime` ECS component**, not `State::projectiles`.
Original source order is restored from the `ProjectileRuntime.order` plus
existing projectile IDs, so a swap-remove in the ECS pool does not reorder
gameplay. Expirations are staged until after stable traversal and removed
via the World entity allocator; spawned children become new
`ProjectileRuntime` entities with stable `DynamicEntityIndex` keys.

The default FPS path still loops the legacy `State::projectiles` vector.
**Both call one shared `Module::stepProgrammedProjectile` solver** for
physical flight programs; the existing public
`selectSweptProjectileContact`, projectile modifiers and synchronous
damage/impact policies retain the same ordering. This is a *real ECS
source-of-truth cutover for the programmed-projectile stage*, not just
another ECS projection.

**Remaining compatibility limitation:** after the ECS projectile stage
finishes, it creates an ordered `State::projectiles` read/projection for
legacy grenade, round/mode and weapon spawning code in the following `fps`
stage; these existing consumers can still modify the vector, which is
mirrored back into ECS at the end-of-tick barrier. Full exclusive
`ProjectileRuntime` authority for all FPS projectile operations requires
converting these remaining consumers and spawn paths next. Do not remove
`actor.ecsHydrate`/`actor.ecsMirror` until that work, v9 binary save
compatibility, network prediction, replay and Editor tests all pass.

**Verification source:** `tests/fps/projectiles.inc` adds live FPS
`ecsProjectileProgramStageParity` and
`ecsProjectileBranchAuthorityParity`, called by `aiverse_fps_tests`.
These tests compare deep/COW World snapshots and tick checksums, real
fly-through/branch damage, the in-place `ProjectileRuntime` owner,
stale legacy projections, `DynamicEntityIndex` mapping, child creation
and mixed nonprogrammed objects. They also replay snapshots and cross-load
save v9 between legacy and ECS storage configurations.

**Not yet verified:** native compilation/execution of this branch and
all assertions, load/replay across networking, performance and editor
regression. The code stays behind existing default-OFF
`actor.ecsAuthority`. Next wave is to convert remaining nonprogrammed
grenade/mode updates and direct in-tick spawns before making the entire
frame ECS-only.


## Live FPS projectile spawn and round-reset ECS ownership (2026-11-18)

Following the direct `ProjectileRuntime` flight-stage migration, both
**actual accepted FPS weapon projectiles** (`Module::shoot`) and
**actual Frag, Smoke and Flash throwable actions** (`Module::action`)
now call one `Module::spawnProjectile(Simulation&,Projectile)`. On the
`actor.ecsAuthority` path, that helper creates a canonical live ECS
`ProjectileRuntime` Entity immediately at the *source action*, with
source ID, stable `order`, `DynamicEntityIndex` mapping and Editor
`ProjectileEntity` projection, before synchronizing the same-tick
`State::projectiles` compatibility view. With the gate OFF it appends
the original State vector, preserving its default source behavior.

The **bomb-round reset** `Module::tickSystems` now invokes
`Module::clearProjectiles`. In ECS authority mode this destroys all
`ProjectileRuntime` entities in source-ID order and clears projectile
index keys at the reset barrier (while preserving loot/vehicle keys);
the legacy/default mode retains the original vector clear. The ECS
stage is no longer waiting for the final `actor.ecsMirror` to materialize
new weapon/grenade projectiles or retire projectile entities on reset.

**Remaining transitional constraint:** other FPS systems still read and
may mutate `State::projectiles` before the final mirror, and the
player/weapon systems still use `State::actors` workspaces. Do not claim
exclusive native ECS authority for all FPS gameplay. The public reusable
projectile flight, impact and collision policies are still integrated
through existing FPS compatibility adapters. No save v9 or wire schema
change was made. Reordering World Entity creation/deletion can alter
allocator reuse; verify generation-safe lookups and rollback.

**Added built-in FPS test fixtures:** `tests/fps/projectile_spawn.inc`,
included in `aiverse_fps_tests`, drives actual shooting plus
Frag/Smoke/Flash Commands in legacy and ECS modes and checks every
source projectile's ECS Runtime/entity ID/order, save v9 cross-loading
and replay. `ecsProjectileRoundResetParity` drives real bomb-round
reset with grenade in flight and confirms direct ECS Runtime destruction,
source-ID cleanup, deterministic respawn after reset, and deep/COW
snapshot replay. Source tests have been written and registered, but
**none of the native C++ assertions have yet been executed**.

**Follow-up:** finish converting all secondary projectile State writers
and readers, including smoke/cloud visibility and other round-mode
consumers, to ECS components and structured read projections; then
retire `actor.ecsMirror` as a writeback source for projectiles. Carry
through native compiler/CTest, FPS networking, Editor lifecycle and RTS
shared-mechanics integration before any default enablement.


## FPS projectile end-of-tick one-way ECS mirror (2026-11-18)

The live `actor.ecsMirror` in `engine/actor/ActorModule.cpp` now calls
`syncDynamicEntities(sim,config_.ecsAuthority)` instead of always
importing `State::projectiles` into `ProjectileRuntime`.

- **ECS authority ON:** the end-of-tick mirror enumerates actual
  `ProjectileRuntime` components, validates unique nonzero projectile
  source IDs and `DynamicEntityIndex` mapping, sorts by canonical
  `order`/ID, refreshes `ProjectileEntity` editor projections and
  reconstructs `State::projectiles` as a **one-way compatibility view**.
  It never assigns a projectile Runtime from the State vector.
- **Default legacy mode:** the old State → derived ECS projection behavior
  remains unchanged.
- **Legacy setup and save v9 load:** the original
  `syncDynamicEntities(Simulation&)` API is retained as an overload
  forwarding to the State → ECS restore mode, including its historical
  C++ symbol for native callers. Restoring an old save still constructs
  canonical Runtime values from the decoded State snapshot; the new
  ECS → State direction applies only during the live mirror barrier.

All known built-in FPS projectile **producers and mutators** have now
been routed to ECS when `actor.ecsAuthority` is enabled: accepted weapon
shots, Frag/Smoke/Flash actions, programmed flight, ordinary ballistic
bullets/grenade bounce/smoke expiry, and bomb-round projectile cleanup.
The one shared physical solver and synchronous impact/damage ordering
remain in place. Code must not introduce a new direct
`State::projectiles` write that expects the mirror to publish it into
ECS; use a native Runtime command/producer instead.

**Regression source:** `tests/fps/projectile_spawn.inc` deliberately
sets `ProjectileRuntime.value.damage=17` while poisoning the
State compatibility vector with damage 777, invokes the new ECS mirror
mode, and verifies that Runtime remains authoritative and that the State
view is repaired. Its real weapon/grenade/round-reset fixtures also
compare default and ECS modes, v9 saves, deep/COW rollback and stable
IDs. These tests are **registered but not natively executed** yet.

**Remaining migration scope:** ActorRuntime and loot/vehicle Runtime
still have legacy state reconciliation; editor/save/network projections
still exist; physical and nonphysical projectile policies are not yet a
fully independent generic scheduled `addProjectileFlightSystem` for
both built-in genres. Do not mark all ECS migration waves complete and
do not turn the FPS gate ON by default until CMake/CTest, playable FPS,
RTS compatibility, same-tick order, editor, network and rollback gates
have concrete passing evidence.
