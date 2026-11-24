# AIVERSE — full feature inventory and ECS migration program

**Date:** 2026-11-18  
**Working branch:** `feature/universal-entity-inspector-fps` (PR #7, actively edited by other agents)  
**Audited revision:** PR #7 head `0957e24d6ac4b26534e69cbb3fd6f2e9f16405a9` at the beginning of the review; later concurrent commits may exist.  
**Status:** PLANNING AUDIT ONLY. No gameplay or runtime semantics changed. No native compilation or CTest executed.  
**Related engineering contracts:** [AGENTS.md](../AGENTS.md), [ecs-mechanic-composition.md](ecs-mechanic-composition.md), [editor-unification-handoff.md](editor-unification-handoff.md), [ecs-agent-handoff.md](ecs-agent-handoff.md).

## Goal, scope and non-goals

Build games by composing independent, genre-neutral **components (authoritative state) + systems (behavior)**. RTS, FPS, racing, survival, RPG, board game and future genre presets only configure the same systems; a system must not require `GameCore&`, `fps::Module&` or a genre enum.

This is a **catalogue-based inventory plus a source inspection of the main RTS/FPS integration points**, not proof that every package was exhaustively line-reviewed or tested. Read `libraries/catalog.json` and referenced implementations when starting each implementation epic; update this document as actual integration is verified.

The catalog contains **120 packages: 78 simulation, 21 presentation, 21 tools.** These counts are categories, not ECS migration completion percentages. Classification of all 120 is recorded in Appendix A.

**Not everything belongs in ECS.** ECS owns mutable deterministic gameplay state and per-tick gameplay behavior. Keep pure geometry, math, search, procedural generators, GPU renderers, platforms, UI, offline tools and account-level meta transactions as their respective services. Expose them to ECS by narrow interfaces/resources/adapters rather than migrating their internals into entity pools.

### Target codes for the complete inventory

- **S** — runtime gameplay needs shared ECS components, systems and generic policies; some policies already exist, but **real engine host integration is not automatically complete**.
- **S*** — shared ECS components/systems already published in a library; FPS/RTS authoritative path integration and parity still outstanding.
- **A** — independent pure algorithm/policy/utility. **Keep it**; add ECS systems that call it when gameplay demands.
- **D** — derived navigation/spatial/world/procedural caches or generators. Keep algorithms as services/world resources; sync authoritative changes through ECS systems and invalidation.
- **P** — presentation / host / UI read-only simulation projection; no authoritative ECS gameplay writes.
- **T** — tooling, packaging, tests, offline generation or non-simulation services; do not move into ECS.

Snapshot totals by proposed destination: `P` 21; `A` 20; `D` 9; `S` 46; `T` 21; `S*` 3.

## Current integration reality — evidence, not aspiration

1. **ECS infrastructure:** `engine/core/ECS.h` implements sparse-set entity pools, generation-safe entity handles and opt-in COW. `engine/sim/Simulation.h` manages deterministic commands, RNG, ordered systems, World snapshots/checksums and restored hooks. `engine/sim/CommonComponents.h` provides shared `Transform`, `Owner`, `Parent`, `LocalTransform`, `WorldTransform`. This is common infrastructure; it does not make individual genre algorithms universal by itself.
2. **RTS:** `engine/game/GameCore.cpp::setup` registers systems into `Simulation` in a specified deterministic order, including `economy`, `fields`, `production`, `construction`, `settlers`, `lanes`, `garrison`, `orders`, `abilities`, `targeting`, `movement`, `weapons`, `projectiles`, `death`, `capture`, `vision`, `victory`, `triggers`, `traffic` and more. Many are closures on `this` or delegate to genre-specific objects. `GameState` also holds substantial state as a World resource. **Registered with the ECS scheduler ≠ independent ECS mechanic.**
3. **FPS:** `engine/actor/ActorModule.h` has monolithic `Actor`, `State::actors/projectiles/loot/vehicles/history` and optional `actor.ecsAuthority`. `ActorRuntime` / `ProjectileRuntime` / `LootRuntime` / `VehicleRuntime` can be canonical between ticks; `Module::tick` still processes legacy workspaces, `Module::shoot` constructs its own shots and traces, and `Module::moveActorEcs` copies motor fields across an adapter. These are transition scaffolds, **not final feature-sized ECS components**.
4. **Shared projectile pipeline:** `engine/mechanics/EcsWeaponLaunchSystems.h`, `EcsProjectileSystems.h`, `EcsDamageSystems.h` provide genre-neutral launch → spawn → flight → impact/damage stages and independent cross-genre fixtures. This pipeline **is not yet the authoritative FPS/RTS weapon path**. Reload, hitscan, spread/recoil, AoE, armor, factions and status effects require follow-on component/policy composition.
5. **Existing policies:** `engine/mechanics/Weapons.h`, `Locomotion.h`, `Melee.h`, `CombatActions.h`, `VehicleData.h`, `Senses.h` and the other catalogued C++ packages should be **reused**, not replaced with freshly duplicated algorithms.
6. **Editor:** `apps/editor/EditorEntityAdapter.h` and `SceneRenderable.h` already unify some document identity, selection and scene submission, but `docs/editor-unification-handoff.md` explicitly reports incomplete native ECS reflection/writeback, transform gizmo and visual/build validation.
7. **Meta:** `engine/meta/Meta.h` has transactional profile/offer/currency/track/daily policies external to the deterministic match World. They must remain account/server authority; only match-local gameplay state and typed result events belong in ECS.

## Main RTS mechanic inventory — actual subsystem registrations

The following table inventories real `GameCore::setup` system names and groups their migration destinations. Do not move the order of effects (especially death/kill/trigger/victory) until replay parity is established.

| Existing RTS systems / paths | Shared component families | Proposed independent ECS systems / owners |
|---|---|---|
| `economy`, `fields`, `gridfields`, `fieldops` | PlayerWallet/ResourceStock, ResourceSource, Extractor, FieldCell (World resource), Repair/Reclaim | ResourceRegen, CapacityAccounting, Gathering, ResourceFieldUpdate, Repair, Reclaim, SelfDestruct |
| `production`, `construction`, `settlers` | ProductionQueue, RecipeProgress, BuildSite, Builder, PlacementRules, Storage, Roads | Production, Construction, PlacementValidation, BuildProgress, RecipeConsume, LogisticsTransfer, RoadBuild |
| `orders`, `movement`, `transit`, `lanes` | OrderQueue, NavigationAgent, PathRequest/PathFollower, Locomotion, VehicleMotion, OrbitTransit, LaneFollower | CommandValidation, OrderDispatch, PathPlanning, Steering, Locomotion, Transit, Formation |
| `garrison`, `capture`, `creep`, `neutrals` | Garrison, TransportSeats, CaptureZone, TerritoryInfluence, NeutralBehavior | GarrisonEnterExit, Capture, TerritorySpread, NeutralSpawn |
| `abilities`, `effects`, `originals`, `fidelity`, `mech` | AbilitySlots, CastIntent, Cooldowns, StatusInstance, Stats/Modifiers, MechStance | AbilityCast, StatusTick, ModifierRecalc, SkillEffects, Stance/Retreat/Guard |
| `targeting`, `weapons`, `projectiles`, `death` | Target, WeaponConfig/State, FireIntent, ProjectileFlight, DamagePayload, Health/Armor | AcquireTarget, shared WeaponLaunch → ProjectileFlight → DamageResolve, Death/Rewards/Despawn |
| `vision`, `environment`, `environment3` | Sight, SensorEmitter, Stealth, VisibilityGrid (World resource), Weather/Time | Visibility, Detection, Jamming, WeatherGameplay, FogReveal |
| `encounters`, `raid`, `waves`, `progression` | SpawnSchedule, EncounterPhase, Threat, XP/Level, LootTable | EncounterDirector, SpawnWave, XPGrant, RewardRoll, RaidObjective |
| `commerce`, `traffic`, `gridfields` | ShopInventory, PriceRules, TrafficAgent, Route, FlowField | CommerceTransactions, RouteFollow, VehicleTraffic, FieldEffects |
| `dungeon`, `modes`, `victory`, `triggers`, `scripts`, `matchstats`, `outcome` | Needs, Room, MatchState, Score, Objective, TriggerState, ScriptState | NeedsTick, RoomWork, MatchRules, VictoryEvaluate, EventTrigger, ScriptCallback, Outcome |
| `gameplayClock` and time scaling | TimeDomain/TimeScale (World resource), SimulationClock | Deterministic clock/time-domain gate with exact old tick stage equivalence |
| Extra command routes in `applyCommand`: `topDown`, `driving`, `arsenal`, `career`, `prompts`, `sensors`, `routes`, `research`, `diplomacy` | InputIntent, VehicleSeats, WeaponInventory, Interaction, Senses, Research, Relations | Common input adapters + generic systems; **no genre-specific command fork** |

## Main FPS mechanic inventory — current monolith to feature-sized components

| Existing FPS state/behavior | Minimum feature component groups | Systems and reusable packages |
|---|---|---|
| Input, sequence validation, prediction | InputIntent, PlayerController, AuthorityOwner, LastInput | CommandDecode/Validate, InputApply; transport-independent |
| Actor lifecycle, pose, motor, stance | CharacterBody, Position/Velocity, MotorState, Stance, LocomotionInput, Respawn | CharacterMotor, StanceTransitions, Respawn; call `character-locomotion` policy where suitable |
| Shooting, ammo/reload, recoil, attachments | WeaponParameters, WeaponState, FireIntent, ReloadState, RecoilState, AttachmentSet | shared `ecs-weapon-launch`, `ecs-projectile-systems`, `weapon-policies`, recoil/reload/shot burst systems |
| Hitscan, rewind, materials, bullet/projectile/grenade | HitScanQuery, LagCompensationHistory, ProjectileFlight, DamagePayload, Explosion | collision query provider, weapon launch/flight/damage, AoE/penetration/persistence |
| Health, armor, downed, heal/revive, bleed | Health, Armor, Gear, Downed, StatusEffects, HealingAction | `ecs-impact-damage`, ArmorMitigation, Downed/Revive, StatusTick |
| Loot, bag, buying, drop/equip | LootPickup, Inventory, Equipment, PurchaseIntent, EconomyBalance | `inventory`, `gear-tiers`, Pickup/Drop, Transaction |
| Melee, block, lock-on | MeleeAttack, Hitbox/Hurtbox, Guard, Stagger, LockTarget | `melee-combat`, `combat-actions` systems |
| Interactions, vault, doors, bomb/defuse | InteractionTarget, InteractionIntent, ActionProgress, Door, BombObjective | `physical-interactions`, `action-sequences`, `match-rules` |
| Vehicles, seats, guns, driving | VehicleController, Wheel/BodyMotion, SeatSet, DriverInput, MountedWeapon | `vehicle-controllers` + same WeaponLaunch/Damage, not FPS-only car state |
| Bots, aim, nav/cover, threat | Perception, Blackboard, BotIntent, NavigationAgent, SquadRole | `bots-combatant`, `action-bots`, `ai-perception`, `navmesh` |
| Rounds, score, bomb, spectator, deathmatch | MatchState/Objective, TeamScore, RespawnTimer, SpectatorState | `match-rules`, Score, Victory, ObjectiveTick |
| Royale zone/air insertion/drop/standings | SafeZone, Skydiver, DropRequest, RoyaleStanding | `battle-royale` ECS systems, host HUD only reads projection |
| World stream, collision, large player counts | ChunkMembership, StreamingInterest, CollisionWorld (derived resource) | Streaming/Interest systems and authority snapshots; **renderer/streaming backends stay services** |
| State history and v9 saves | EntityId mapping, tick histories, versioned component schema | canonical ECS snapshots/rollback, legacy wire and save adapters until parity |

**Hard gate:** splitting `ActorRuntime { Actor value; }` into true feature components must happen **before** declaring FPS gameplay fully migrated. A huge `Actor` stored as one component is merely moving the monolith's storage.

## Common ECS state contracts and tick scheduling

**Entity components (examples):** `Position/Velocity` or existing compatible `Transform`; `Parent/LocalTransform/WorldTransform`; `Health`, `Armor`, `Team`, `Target`, `WeaponParameters`, `WeaponState`, `FireIntent`, `ProjectileFlight`, `Inventory`, `Equipment`, `AbilityState`, `StatusEffects`, `NavigationAgent`, `VehicleMotion`, `AIIntent`, `InteractionState`, `ProductionQueue`, `Builder`, `ConstructionSite`, `VisionEmitter`, `Loot`, `Needs`, `QuestProgress`, `MatchParticipant`.

**World resources:** match/global scores, time domains, map cells, collision/navigation snapshots, region graph, ordered event/damage/deferred-spawn queues, spawn ID generators, faction relations, rules config and deterministic RNG. A globally scoped resource is valid if it lives in the snapshot/checksum or is *derived* from snapshotted state with documented rebuild rules. Do **not** create one Entity per grid cell/card by default; use dense deterministic domain resources when that matches access patterns.

**Stage order** must be both explicit and configurable, not accidentally determined by registration:
1. Snapshot-safe Command intake + permission/sequence validation
2. Input/AI intent generation (read-only planning can parallelize)
3. Inventory/action/ability/economy reservations and resource validation
4. Nav/locomotion/vehicle + interaction progression
5. Targeting/reload/launch → deterministic deferred projectile spawn
6. Projectile/hitscan physics → ordered hit events
7. Armor/damage/status → downed/death/loot/reward
8. Objectives/economy/vision/time-dependent rules
9. Triggers/scripts/match result (preserve legacy same-tick event semantics)
10. Commit deferred structural changes; snapshot/checksum; publish read-only presentation/net/editor projections.

This is a **target dependency model**, NOT permission to reorder the current RTS tick. First record current exact order; introduce named barriers one at a time, using old-stage compatibility shims to preserve observables.

## Migration epics, order and hard dependencies

| Epic | Priority | Deliverables / scope | Depends on | Acceptance and rollback gate |
|---|---|---|---|---|
| **E00** Feature baseline and ownership | **P0** | Freeze source branch/PR heads, list every existing host call, golden RTS/FPS saves/replays and checksum traces; no simultaneous gameplay edits from separate agents | none | Golden outputs + ownership map archived before modifying gameplay |
| **E01** ECS runtime contract | **P0** | Public component registry/type IDs, schema/version, data-only state, explicit read/write/resource/structural sets, scheduler stages and command buffers | E00 | Determinism/COW/rollback/sanitizer; native project uses system without `GameCore`/`fps::Module` |
| **E02** Serialization and mapping | **P0** | Stable Entity references; old RTS IDs/FPS slot/id ↔ generation-safe Entity; serializer/hash per component/resource; network/save adapters; rollback history | E01 | FPS v9 + old RTS save load; zero lost entities, byte/checksum replay parity |
| **E03** Universal weapon pipeline | **P0** | Adopt existing `ecs-weapon-launch` → flight → damage in real FPS and RTS. Add orthogonal hitscan, recoil/spread, reload, penetration, splash, guidance, team policies and destruction | E01-E02 | Same weapon policy in two host genres + gun on vehicle; old FPS/RTS fire/hit/kill parity |
| **E04** FPS authority & actor split | **P0** | Remove gameplay dependence on tick-local `State::actors`: input, motor, health, gun/ammo, inventory, vehicles, loot, history, bots/match; retire `ecsHydrate/ecsMirror` only after parity | E01-E03 | Default vs `actor.ecsAuthority` deterministic parity; authoritative server, rewind, bot match, 100-player fixture |
| **E05** RTS shared combat + orders | **P0** | `RtsCombat` launch/damage/abilities; `RtsUnits` orders/movement; no direct global `GameCore&` for shared combat | E01-E03 | RTS skirmish, artillery, AoE, army pathing and canonical event order pass |
| **E06** Movement + navigation | **P1** | Common NavigationAgent/PathRequest/Steering/Locomotion + terrain, crowd, boats/air, orbit transfer, climb/swim/ladder; world nav remains derived | E01,E02 | FPS/RTS/third-party native actor take same system without genre branch |
| **E07** Vehicles, mounted weapons, traffic | **P1** | VehicleMotion/Seat/Passenger/Driver + TrafficAgent + attached shared WeaponLaunch; same code across FPS, RTS top-down and racing | E03,E06 | Exit/re-enter, seat authority, rollback, vehicle shooting, city traffic fixture |
| **E08** Economy, inventory, production, construction | **P1** | ResourceStore, Storage, Gatherer, Recipe, ProductionQueue, BuildSite, Crafting, Trade; share `construction`, `inventory`, `production-networks` algorithms | E01,E02,E05 | RTS supply/capacity/field economy and survival crafting produce unchanged deterministic results |
| **E09** Abilities, stats, melee, status, damage | **P1** | Ability cast/cooldown, resistance, equipment, hitboxes, parry, buffs/DoT, death, XP; reuse all relevant policies | E03,E08 | Cross-game healing/poison/magic/melee with identical shared damage owner |
| **E10** AI, perception, formation and bots | **P1** | Perception/Stealth/Blackboard, decision input, squad roles, navigation tasks, bot command producer; AI never directly mutates another subsystem | E05-E09 | RTS and FPS bots plus stealth NPC; deterministic RNG/decision traces |
| **E11** World gameplay: sight, fog, terrain, settlement | **P2** | Vision/Stealth/VisibilityGrid, territory, ecosystem, fields, dungeon needs, roads, city traffic and world weather/time; derived cache rebuild barriers | E06,E08,E10 | RTS vision and rebuild parity; changing terrain invalidates only correct derived caches |
| **E12** Match, turns, cards, puzzles, narrative | **P2** | MatchState/Objectives, TurnScheduler/CardState/Deck, Puzzle state, QTE/dialogue/scene and action sequences; stateful per-match World resources allowed | E01,E02,E09 | At least two hosts for reusable mechanic, save/rollback and hidden-info audit |
| **E13** Survival, production, voxel, large-world | **P2** | ECS gameplay bridges for survival ecology, factories/offline progress and voxel edit/ticks; keep voxel mesh/light/nav/render as derived services | E06,E08,E11 | Chunk unload/reload, deterministic edit history and interest-scoped state |
| **E14** Editor, presentation and content integration | **P1 cross-cutting** | Generic component schema/Inspector writeback; hierarchy/gizmo; render/audio/VFX event consumer; no custom genre inspector branches | E01,E02 and individual features | RTS/FPS and native modules inspect/edit same component types, undo/redo, selection and visual tests |
| **E15** Networking/meta and production cutover | **P0 release gate** | Match events → idempotent external meta transactions, client/server projections, integration suite, delete obsolete authority mirrors, activate ECS default by project with rollback flag | E03-E14 | Cross-platform replays/checksums, old save/network compatibility, CTest, sanitizer, editor launch, performance and multi-platform CI |

**Release sequencing:** P0 dependency-critical foundation → FPS + RTS shared combat → movement, economy and AI → long-tail mechanics → cutover. Epics are not estimates. Do not bulk-convert all packages or turn on unverified ECS parallel execution.

### Recommended first implementation increments

- **I1:** Baseline `actor.ecsAuthority=off/on` and RTS replay hashes, save files, event sequences. Produce a machine-readable migration status table (host → function → component authority → parity).
- **I2:** Split FPS input/motor from `ActorRuntime` into minimal real components; keep compatibility snapshot adapter; verify only this stage.
- **I3:** Replace FPS projectile launch and damage with shared `EcsWeaponLaunchSystems` / `EcsProjectileSystems` / `EcsDamageSystems`, preserve rewind/penetration and fire cadence. **Do not change FPS and RTS in the same implementation commit**.
- **I4:** Attach RTS `sysWeapons/sysProjectiles` to identical shared packages, retaining `WeaponDef` JSON and legacy events/saves via adapters.
- **I5:** Split RTS order/movement and FPS character motor to generic locomotion/nav interfaces; use temporary host adapters, not duplicated solvers.
- **I6:** Continue domain-by-domain using epics E07-E13; update the catalog and skills after each verified system.

## Mandatory per-feature definition of done

For each `S` or `S*` row in Appendix A, check ALL:

1. Data-only components, documented state ownership and versioned config; no authoritative copy in module vectors, editor or network presentation.
2. Shared systems with explicit `reads/writes/resources` and deterministic stage; command/event barrier; policy reusable via public C++ headers.
3. At least two distinct live hosts (not two differently named test tag types only), plus one combined-feature scenario showing mechanic composition on the same Entity.
4. Tests for component add/remove/despawn, generation reuse, save/load, rollback and COW, identical checksum/event order, rejection of invalid commands, deterministic RNG.
5. Real FPS/RTS integration where applicable with old save/wire/API parity and both opt-in/legacy paths checked before removing the gate.
6. Native game CMake target links without pulling in genre modules; matching AI skill, example rules, catalog entry and agent handoff updated.
7. CI proof separated into source checks, compiled tests, macOS/Metal/GL visual tests and runtime performance. **No native test claim without test logs.**

Maintain `migration-status.csv` or a machine-readable equivalent in follow-on work with columns:
`feature_id,source_path,existing_host,owner_before,components,systems,shared_policy,old_adapter,save_version,tests,real_hosts,parity_evidence,status,owner,commit`.
Valid statuses: `not-started`, `policy-only`, `ecs-library-only`, `adapter-active`, `parity-verified`, `legacy-removed`. Do not infer status from package names alone.

## Parallel-agent boundaries / known integration hazards

- **PR #7:** FPS runtime + Editor working branch. Use `docs/editor-unification-handoff.md`; log **every implementation** commit with a separate handoff documentation commit and exact SHA.
- **PR #8/ECS experiments:** some infrastructure has been integrated; do not silently enable new scheduler/COW or overwrite FPS/editor changes using stale branch content. See `docs/ecs-agent-handoff.md`.
- **PR #17:** renderer PCSS/TAA work, separate from ECS gameplay conversion. Avoid renderer collision, reconcile by diff/merge only when appropriate; it is **not** proof of ECS system compatibility.
- Assign one owner at a time to `engine/actor/ActorModule.cpp`, `engine/game/Combat.cpp` and shared `engine/core/ECS.h`; other agents can work on independent public libraries/tests/skills.
- Never reorder RTS `sim.addSystem` or FPS same-tick hit/death/score updates without **old vs new event sequence** evidence.
- Avoid silently overwriting `AGENTS.md`, Editor adapter or handoff changes made by concurrent agents.

## Scope explicitly excluded from gameplay ECS rewrites

These stay engine services, derived data or tools: GPU RHI (Metal/GL/Vulkan), render passes/PCSS/TAA, batching/LOD/HLOD, shadows/occlusion, audio mixer and visual particle effects, app/window/input acquisition, editor UI panels and serialized document layout, asset pipelines, build targets/store SDKs/importers, source-control workflows, offline generators and headless verifiers. ECS exposes **inputs/state/events** to them; they must not own authoritative simulation outcome.

Account-level `engine/meta` shop, balances, offers, payments, social and LiveOps are transactional services: bridge **match-result events → verified idempotent server transaction**, not a per-frame ECS entity. Within a deterministic match, inventory/currency/progression *are* eligible ECS components/resources.

## Source references for this audit

- `libraries/catalog.json` (120 packages and per-package sources)
- `AGENTS.md`, `docs/ecs-mechanic-composition.md`, `docs/mechanics-packages.md`
- `engine/core/ECS.h`, `engine/sim/Simulation.h`, `engine/sim/CommonComponents.h`
- `engine/game/GameCore.cpp`, `GameCore.h`, `GameData.h`
- `engine/actor/ActorModule.cpp`, `ActorModule.h`, `ActorSystems.h`
- `engine/meta/Meta.h`, `docs/editor-unification-handoff.md`, `docs/ecs-agent-handoff.md`, `docs/overview.md`

## Appendix A — complete package inventory (catalog snapshot)

Classification is a **migration destination**, not a claim of existing ECS integration. `catalog.status=cpp-subsystem` describes a published C++ package, **not** a production-ready ECS system. `S*` means a shared ECS stage exists, but published RTS/FPS paths are still legacy. No runtime tests were executed in this audit.

| Package ID | Layer / declared status | Target | Library source |
|---|---|---|---|
| `geometry` | presentation / engine-reference | **P** | `engine/render/Geometry.h` |
| `math` | simulation / engine-reference | **A** | `engine/core/FixedMath.h` |
| `collections` | simulation / cpp-foundation | **A** | `libraries/include/aiverselib/Collections.h` |
| `graphs` | simulation / cpp-foundation | **A** | `engine/core/Graphs.h` |
| `spatial` | simulation / engine-reference | **D** | `engine/game/GameCore.h` |
| `grid` | simulation / cpp-foundation | **A** | `libraries/include/aiverselib/Grid.h` |
| `navigation` | simulation / engine-reference | **D** | `docs/pathfinding.md` |
| `behavior` | simulation / cpp-foundation | **A** | `libraries/include/aiverselib/Behavior.h` |
| `tasks` | simulation / cpp-foundation | **S** | `libraries/include/aiverselib/Tasks.h` |
| `logistics` | simulation / cpp-foundation | **A** | `libraries/include/aiverselib/Logistics.h` |
| `rules` | simulation / cpp-foundation | **A** | `libraries/include/aiverselib/Rules.h` |
| `progression` | simulation / cpp-foundation | **A** | `engine/core/progression/Progression.h` |
| `objectives` | simulation / cpp-foundation | **A** | `libraries/include/aiverselib/Objectives.h` |
| `generation` | simulation / engine-reference | **D** | `engine/game/gen/MapGen.h` |
| `presentation` | presentation / cpp-foundation | **P** | `libraries/include/aiverselib/Presentation.h` |
| `ui-patterns` | presentation / engine-reference | **P** | `engine/ui/templates/README.md` |
| `verification` | tools / engine-reference | **T** | `tests/check_headless_restore.py` |
| `input` | presentation / cpp-subsystem | **P** | `engine/gameplay/Input.h` |
| `camera` | presentation / cpp-subsystem | **P** | `engine/gameplay/Camera.h` |
| `construction` | simulation / cpp-subsystem | **S** | `libraries/include/aiverselib/gameplay/Construction.h` |
| `abilities` | simulation / cpp-subsystem | **S** | `libraries/include/aiverselib/gameplay/Abilities.h` |
| `inventory` | simulation / cpp-subsystem | **S** | `engine/gameplay/Inventory.h` |
| `gear-tiers` | simulation / cpp-subsystem | **S** | `engine/systems/Gear.h` |
| `squads` | simulation / cpp-subsystem | **S** | `libraries/include/aiverselib/gameplay/Squads.h` |
| `feedback` | presentation / cpp-subsystem | **P** | `libraries/include/aiverselib/gameplay/Feedback.h` |
| `tutorial` | simulation / cpp-subsystem | **S** | `libraries/include/aiverselib/gameplay/Tutorial.h` |
| `save-migrations` | simulation / cpp-subsystem | **A** | `libraries/include/aiverselib/gameplay/Saves.h` |
| `diagnostics` | tools / cpp-subsystem | **T** | `engine/gameplay/Diagnostics.h` |
| `content-pipeline` | tools / tool-workflow | **T** | `tools/libraries/gameplay.py` |
| `game-verification` | tools / tool-workflow | **T** | `tools/libraries/gameplay.py` |
| `project-recipes` | tools / tool-workflow | **T** | `tools/libraries/gameplay.py` |
| `configuration` | tools / cpp-subsystem | **T** | `engine/gameplay/Configuration.h` |
| `ui-binding` | presentation / cpp-subsystem | **P** | `libraries/include/aiverselib/app/Binding.h` |
| `screens` | presentation / cpp-subsystem | **P** | `libraries/include/aiverselib/app/Screens.h` |
| `themes` | presentation / cpp-subsystem | **P** | `libraries/include/aiverselib/app/Theme.h` |
| `match-bridge` | presentation / cpp-subsystem | **P** | `libraries/include/aiverselib/app/MatchBridge.h` |
| `app-lifecycle` | presentation / cpp-subsystem | **P** | `libraries/include/aiverselib/app/Lifecycle.h` |
| `network-session` | presentation / cpp-subsystem | **P** | `libraries/include/aiverselib/app/Session.h` |
| `content-mods` | tools / cpp-subsystem | **T** | `libraries/include/aiverselib/app/Content.h` |
| `balance-lab` | tools / cpp-subsystem | **T** | `libraries/include/aiverselib/app/Balance.h` |
| `play-scenarios` | tools / cpp-subsystem | **T** | `libraries/include/aiverselib/app/Scenario.h` |
| `performance-budgets` | tools / cpp-subsystem | **T** | `libraries/include/aiverselib/app/Performance.h` |
| `template-kits` | tools / cpp-subsystem | **T** | `libraries/include/aiverselib/app/Genre.h` |
| `change-planner` | tools / cpp-subsystem | **T** | `libraries/include/aiverselib/app/Planner.h` |
| `native-scaffold` | tools / tool-workflow | **T** | `tools/libraries/native_codegen.py` |
| `typed-state` | tools / tool-workflow | **T** | `tools/libraries/native_codegen.py` |
| `typed-commands` | tools / tool-workflow | **T** | `tools/libraries/native_codegen.py` |
| `reference-validation` | tools / tool-workflow | **T** | `tools/libraries/project_checks.py` |
| `hot-reload` | presentation / cpp-subsystem | **P** | `libraries/include/aiverselib/runtime/HotReload.h` |
| `bounded-memory` | simulation / cpp-subsystem | **A** | `libraries/include/aiverselib/runtime/Bounded.h` |
| `work-budget` | simulation / cpp-subsystem | **A** | `libraries/include/aiverselib/runtime/WorkBudget.h` |
| `state-diff` | tools / cpp-subsystem | **T** | `libraries/include/aiverselib/runtime/StateDiff.h` |
| `library-upgrades` | tools / tool-workflow | **T** | `tools/libraries/project_checks.py` |
| `library-showcase` | presentation / engine-reference | **P** | `examples/libraries/showcase/main.cpp` |
| `attribute-modifiers` | simulation / cpp-subsystem | **S** | `libraries/include/aiverselib/systems/EngineMechanics.h` |
| `status-effects` | simulation / cpp-subsystem | **S** | `libraries/include/aiverselib/systems/EngineMechanics.h` |
| `tag-conditions` | simulation / cpp-subsystem | **A** | `engine/systems/Tags.h` |
| `object-interactions` | simulation / cpp-subsystem | **S** | `libraries/include/aiverselib/systems/Interactions.h` |
| `reservations` | simulation / cpp-subsystem | **S** | `libraries/include/aiverselib/systems/Reservations.h` |
| `action-sequences` | simulation / cpp-subsystem | **S** | `libraries/include/aiverselib/systems/Sequence.h` |
| `loot-tables` | simulation / cpp-subsystem | **A** | `engine/core/progression/Loot.h` |
| `progression-curves` | simulation / cpp-subsystem | **A** | `engine/systems/Curves.h` |
| `ai-perception` | simulation / cpp-subsystem | **S** | `libraries/include/aiverselib/systems/Perception.h` |
| `decision-explanations` | presentation / cpp-subsystem | **P** | `libraries/include/aiverselib/systems/Decisions.h` |
| `bots-commander` | simulation / cpp-subsystem | **S** | `libraries/include/aiverselib/bots/Rts.h` |
| `bots-combatant` | simulation / cpp-subsystem | **S** | `libraries/include/aiverselib/bots/Fps.h` |
| `navmesh` | simulation / cpp-subsystem | **D** | `libraries/include/aiverselib/bots/NavMesh.h` |
| `building-gen` | simulation / cpp-subsystem | **D** | `libraries/include/aiverselib/systems/BuildingGen.h` |
| `bot-matches` | tools / tool-workflow | **T** | `examples/libraries/bots/main.cpp` |
| `character-locomotion` | simulation / cpp-subsystem | **S** | `engine/mechanics/Locomotion.h` |
| `melee-combat` | simulation / cpp-subsystem | **S** | `engine/mechanics/Melee.h` |
| `combat-actions` | simulation / cpp-subsystem | **S** | `engine/mechanics/CombatActions.h` |
| `stealth-senses` | simulation / cpp-subsystem | **S** | `libraries/include/aiverselib/mechanics/Senses.h` |
| `cinematic-gameplay` | simulation / cpp-subsystem | **S** | `libraries/include/aiverselib/mechanics/Cinematic.h` |
| `card-games` | simulation / cpp-subsystem | **S** | `libraries/include/aiverselib/mechanics/CardGame.h` |
| `tactics-grid` | simulation / cpp-subsystem | **S** | `libraries/include/aiverselib/mechanics/TacticsTurns.h` |
| `cards-turns-bots` | simulation / cpp-subsystem | **S** | `libraries/include/aiverselib/bots/CardBot.h` |
| `weapon-policies` | simulation / cpp-subsystem | **A** | `libraries/include/aiverselib/mechanics/Weapons.h` |
| `action-bots` | simulation / cpp-subsystem | **S** | `libraries/include/aiverselib/bots/GenreBots.h` |
| `match-rules` | simulation / cpp-subsystem | **S** | `libraries/include/aiverselib/mechanics/Match.h` |
| `battle-royale` | simulation / cpp-subsystem | **S** | `libraries/include/aiverselib/mechanics/royale/BattleRoyale.h` |
| `narrative-graphs` | simulation / cpp-subsystem | **S** | `libraries/include/aiverselib/mechanics/Narrative.h` |
| `turn-scheduler` | simulation / cpp-subsystem | **S** | `libraries/include/aiverselib/strategy/Turns.h` |
| `card-runtime` | simulation / cpp-subsystem | **S** | `libraries/include/aiverselib/strategy/Cards.h` |
| `grid-puzzles` | simulation / cpp-subsystem | **S** | `libraries/include/aiverselib/strategy/Puzzles.h` |
| `puzzle-solvers` | simulation / cpp-subsystem | **A** | `libraries/include/aiverselib/mechanics/PuzzleSolvers.h` |
| `big-numbers` | simulation / cpp-subsystem | **A** | `libraries/include/aiverselib/strategy/BigNumber.h` |
| `offline-production` | simulation / cpp-subsystem | **S** | `libraries/include/aiverselib/strategy/Offline.h` |
| `production-networks` | simulation / cpp-subsystem | **S** | `libraries/include/aiverselib/strategy/Production.h` |
| `zone-graph` | simulation / cpp-subsystem | **S** | `libraries/include/aiverselib/strategy/ZoneGraph.h` |
| `rhythm-timing` | simulation / cpp-subsystem | **S** | `libraries/include/aiverselib/strategy/Rhythm.h` |
| `run-graph` | simulation / cpp-subsystem | **S** | `libraries/include/aiverselib/strategy/RunGraph.h` |
| `puzzle-machine` | simulation / cpp-subsystem | **S** | `libraries/include/aiverselib/strategy/PuzzleMachine.h` |
| `session-admission` | presentation / cpp-subsystem | **P** | `libraries/include/aiverselib/net/Admission.h` |
| `snapshot-delta` | presentation / cpp-subsystem | **P** | `engine/net/SnapshotDelta.h` |
| `physical-interactions` | simulation / cpp-subsystem | **S** | `libraries/include/aiverselib/mechanics/PhysicalInteractions.h` |
| `vehicle-controllers` | simulation / cpp-subsystem | **S** | `engine/mechanics/Vehicles.h` |
| `sports-rules` | simulation / cpp-subsystem | **S** | `libraries/include/aiverselib/mechanics/Sports.h` |
| `settlement-production` | simulation / cpp-subsystem | **S** | `libraries/include/aiverselib/mechanics/Colony.h` |
| `survival-ecology` | simulation / cpp-subsystem | **S** | `libraries/include/aiverselib/mechanics/SurvivalEcology.h` |
| `boss-encounters` | simulation / cpp-subsystem | **S** | `engine/mechanics/Telegraphs.h` |
| `match-progression` | simulation / cpp-subsystem | **S** | `engine/mechanics/Perks.h` |
| `physics` | presentation / engine-reference | **P** | `engine/physics/World.h` |
| `world-calendar` | simulation / cpp-subsystem | **S** | `libraries/include/aiverselib/systems/Calendar.h` |
| `weather-timeline` | presentation / cpp-subsystem | **P** | `libraries/include/aiverselib/systems/WeatherTimeline.h` |
| `vehicle-fx` | presentation / cpp-subsystem | **P** | `libraries/include/aiverselib/mechanics/vehicle-fx/VehicleLights.h` |
| `swept-shapes` | simulation / cpp-foundation | **A** | `engine/core/SweptShapes.h` |
| `derived-keys` | simulation / cpp-subsystem | **A** | `libraries/include/aiverselib/runtime/DerivedKeys.h` |
| `city-traffic` | simulation / cpp-subsystem | **S** | `engine/mechanics/Traffic.h` |
| `rules-files` | tools / tool-workflow | **T** | `tools/rules/rules_io.py` |
| `voxel-world` | simulation / cpp-subsystem | **D** | `libraries/include/aiverselib/systems/voxel/VoxelWorld.h` |
| `voxel-light` | simulation / cpp-subsystem | **D** | `libraries/include/aiverselib/systems/voxellight/VoxelLight.h` |
| `voxel-mesh` | presentation / cpp-subsystem | **P** | `libraries/include/aiverselib/systems/voxelmesh/VoxelMesh.h` |
| `voxel-gen` | simulation / cpp-subsystem | **D** | `libraries/include/aiverselib/systems/voxelgen/VoxelGen.h` |
| `voxel-ticks` | simulation / cpp-subsystem | **S** | `libraries/include/aiverselib/systems/voxelticks/VoxelTicks.h` |
| `voxel-nav` | simulation / cpp-subsystem | **D** | `libraries/include/aiverselib/bots/voxelnav/VoxelNav.h` |
| `voxel-replication` | tools / cpp-subsystem | **T** | `libraries/include/aiverselib/net/voxel/VoxelReplication.h` |
| `ecs-projectile-systems` | simulation / cpp-subsystem | **S*** | `engine/mechanics/EcsProjectileSystems.h` |
| `ecs-impact-damage` | simulation / cpp-subsystem | **S*** | `engine/mechanics/EcsDamageSystems.h` |
| `ecs-weapon-launch` | simulation / cpp-subsystem | **S*** | `engine/mechanics/EcsWeaponLaunchSystems.h` |

## Appendix B — verification status

This work is **an architecture and source inventory**, not a completed migration. Connector file retrieval and catalog JSON parsing succeeded. No repository was cloned locally; this environment did not run CMake, native C++, sanitizer, Metal/GLES, FPS gameplay, RTS gameplay, replay parity, GitHub Actions or editor UI. Any recommendation to remove a legacy path depends on future evidence from those checks.
