# Cross-genre C++ gameplay packages: first batch

These are reusable deterministic policies for project-native/host code. They compose with engine
mechanics; they do not silently replace existing FPS movement, RTS abilities/damage or match rules.

| Package | Implemented API | Boundaries |
|---|---|---|
| character-locomotion | MotionConfig/State/Input, stepMotion: ground/air speed, acceleration/braking, gravity, jump buffer, coyote time, bounded extra jumps, ceiling response. LocomotionModes.h (0.2.0): stepLocomotion mode machine Ground/Air/Ledge/Ladder/Swim/Climb/Mantle with slopes, ledge grab/shimmy/climb-up, ladders, buoyant swimming, stamina-limited wall climbing and timed mantle/vault | Returns velocity, mode and events; collision owner reports grounded/ceiling, ground normal, ledges, ladders, water depth, climbable walls and obstacles, and applies movement. No physics queries, root motion or animation controller. |
| melee-combat | AttackSet/State, phase evaluation, input buffering, allowed combo cancels, authorization callback, once-per-swing hit bookkeeping, evade/parry/block outcome | Geometry/target validation and actual damage/stamina belong to the existing combat owner. No parallel health/status system. |
| combat-actions | Frame-keyed hitboxes + hurtboxes (sphere/capsule/yaw box) with overlap tests, self/team filtering and once-per-swing hits; dodge i-frames + cooldown; directional guard arc + perfect-parry window with re-arm; poise damage/break/recovery and hit/block/parry stun; deterministic lock-on acquire/switch/break | Composes melee-combat. Returns contacts/outcomes; damage, stamina, health and statuses stay with the existing combat owner. No per-tick sweep, animation-driven boxes, ragdoll or camera presentation. |
| weapon-policies | Deterministic recoil recovery, seeded spread, swept projectiles with gravity, lifetime and bounded ricochets; 0.2.0: material-layer penetration, radial explosions with owner LOS, ammo feeds (magazine/belt/per-shell/energy + overheat), charge tiers, attachment stat stacking, ballistic launch and lead solving (see [Weapons and ballistics](#weapons-and-ballistics-weapon-policies-020)) | Existing FPS ammo/reload/fire cadence remain authoritative for built-in FPS weapons. No automatic replacement of hitscan, no damage/health owner, no presentation. |
| genre-bots | Melee bot (spacing band, phase-aware openings, hit-confirmed combos, telegraph dodge/parry timing, lock-on), shooter bot (turn-limited aim, seeded aim error, solveIntercept lead, recoil bursts, reload/overheat, cover choice), squad roles + formations, objective choice hook (see [Genre bots](#genre-bots-genre-bots-010)) | Returns player-equivalent inputs only; perception, cover, objectives and navigation come from owners. No pathfinding, match rules, senses or sports/racing/card bots. |
| stealth-senses | Sight cones (near/focus/peripheral) with owner light/cover/stance/visibility multipliers and LOS callback, hearing with occlusion callback and noise/query budgets, decaying footprint/scent markers, per observer-target suspicion meters (Unaware/Suspicious/Searching/Alert), delayed ally alerts sharing last known positions, deterministic search points with give-up, disguises, hiding spots (see [Stealth and senses](#stealth-and-senses-stealth-senses-010)) | Perception records and events only. Owner supplies poses, multipliers, raycasts/occlusion and search candidates and moves agents. Not RTS fog of war. No sound propagation, navmesh routes or presentation. |
| physical-interactions | Pick up/carry (hold point, mass limit, carry speed penalty, drop when blocked), charged throws with an arc preview from the same integrator and swept landing, push/pull (free or grid, friction/mass resistance, cooperative pushing, blocking), XPBD ropes with a fixed iteration count, attach/detach and breaking force, hinged/sliding doors (locks/keys, one-way, timing, auto-reverse), waypoint moving platforms with per-rider carry deltas, breakables (damage types, thresholds, stages, debris requests) (see [Physical interactions](#physical-interactions-physical-interactions-010)) | Authoritative kinematic policies over owner collision (engine/world). Box3D stays presentation-only (debris). No rigid-body stacking, rotation of props/platforms, rope-world collision or rendering. |
| match-rules | Warmup/live/overtime/intermission/finished, team score events, best-of rounds, draws and capture points. MatchObjectives.h (0.2.0): capture/hold zones with contest rules and decay, king of the hill, payload, elimination with lives/respawn waves, extraction, bomb/defuse, ordered route gates, contested overtime clock, spectator transitions (see [Match objectives](#match-objectives-match-rules-020)) | Native extension policies returning events/scores for the match owner; existing built-in victory modes stay owned by their modules. Presence, deaths, pickups and gate crossings come from the authoritative owner. No cart physics, bomb/HUD presentation or network spectating. |
| narrative-graphs | JSON/localization-key graphs, conditional choices, fact set/add transactions, visits, command deduplication and endings; 0.2.0 social scenes: typed facts, relationship meters, knowledge, AND/OR/NOT conditions, flags, chapters/acts, deferred consequences, speakers, cue requests, barks, VN backlog/skip-read/auto-advance (see [Narrative scenes](#narrative-scenes-narrative-graphs-020)) | Project UI/cinematics consume committed nodes, CueRequest records and bark lines. No scene playback, audio, QTE or online reward grant. |

| narrative-graphs | JSON/localization-key graphs, conditional choices, fact set/add transactions, visits, command deduplication and endings | Project UI/cinematics consume committed nodes. No automatic scene playback, audio, QTE or online reward grant. |
| cinematic-gameplay | Integer-ratio time-scale domains with stacked priority requests and hit-stop/slow-motion presets, scene timelines of tracked cues with play/pause/skip/seek, sync waits on host-reported line/marker/in-position facts with timeouts, QTE press/mash/hold/sequence with difficulty and accessibility, interaction staging (marks, teleport fallback, input locks), checkpoint requests and resume at sync points, narrative cue triggers (see [Cinematic gameplay](#cinematic-gameplay-cinematic-gameplay-010)) | Authoritative scene flow only. Audio/animation/camera playback, navigation and teleports are host work reported back as tick-stamped input. No renderer, cutscene editor, lip sync or root motion. |

## Native/host integration

Include `<aiverselib/genre/Configured.h>`, the needed policy header and `genre/Persistence.h`.
Executable hosts link `AIVERSE::genre`; registered OBJECT modules call
`aiverse_native_use_genre(aiverse_native_<id>)` after `aiverse_native_module(...)`. The helper puts collision
backend dependencies on the owning aiverse_game target, avoiding object-library linkage cycles.
For header-only policies without projectile collision, `AIVERSE::gameplay_native` is sufficient.

See `examples/native/libraries/GenreConsumer.cpp` for validated settings and NativeState storage.
The library_demo native module now initializes these records in setup (module VERSION 6: adds locomotion modes, combat-actions, weapons, match objectives, senses, genre-bot, narrative scene, physical interaction and cinematic records).
`tests/libraries/genre_tests.cpp` runs trajectories, combos, world collision, round transitions,
choice transactions and restoration checks. No engine source edit is required in a consuming project.

External input must arrive as validated Commands. Use these helpers inside the owning authoritative
system/handler, not from presentation. Keep immutable definitions separately from dynamic state.
Use integer ticks and Fixed dt. Save every outcome-affecting state and do not update the same controller
twice per tick. Motion, melee and match policies reject repeated/backwards ticks. Recoil/projectile
steppers are per-step primitives; their owner is responsible for exactly one update per simulation tick.

## Configuration

Scalar defaults are in `libraries/spec/gameplay.json` (configuration package 0.3.3): sections
`locomotion`, `melee`, `projectile`, `matchRules`. Run generate_gameplay.py after spec changes.
Load through `readGameplaySettings`, then `motionConfig`, `locomotionModeConfig`, `attackConfig`, `projectileConfig`,
`matchConfig`. The locomotion mode keys are optional (`maxSlopeMilliDegrees`, `ledge*`, `ladder*`, `swim*`,
`buoyancyMilli`, `climb*`, `mantle*`, `vault*`); relations such as swimExit <= swimEnter <= swimFloat <= swimDive
and mantleMin <= vaultMax <= mantleMax are rejected with readable errors. Metres/seconds use integer milli-units in JSON; C++ converts directly to Fixed.
Tick durations are literal ticks at the project's tick rate. Existing settings remain optional.

`examples/libraries/genre/settings.json` configures a double-jump controller, attack timing,
a ricocheting projectile and best-of rounds. Attack definitions are typed AttackSet entries:
set stable positive IDs, allowed combo IDs and cancelFrom explicitly. Graph validation rejects
unknown combo edges. `attackConfig(settings,id)` cancels into combos from recovery start by default.
RecoilConfig is a typed public settings struct; angles use turns and recovery is per simulation step.

Narrative JSON uses `readStory` and the example `examples/libraries/genre/story.json`. Every node and
choice has a localization key. Choice `next:0` ends the graph; conditions reference integer fact IDs.
Missing facts fail their conditions. Define an explicit ending choice even for a final dialogue node.
Unknown fields, duplicate IDs, broken edges and invalid integer values are rejected during loading.

## Locomotion modes

`genre/LocomotionModes.h` adds `LocomotionModeConfig/State/Input/Result` and `stepLocomotion`. It wraps
`stepMotion` for Ground/Air (behaviour is bit-identical on flat ground) and adds modes driven only by
`LocomotionEnvironment`, the per-tick report of the collision owner:

- **Slopes:** ground normal with `normal.y >= cos(maxSlope)` is walkable and the velocity follows the
  plane; steeper ground slides along the downhill tangent (slideAcceleration, capped at slideSpeed),
  with no jump. Events SlideStart/SlideEnd.
- **Ledge:** while falling (or on grab) next to a reported ledge whose top is ledgeMin..ledgeMax above
  the feet: hang, shimmy with wish.x along cross(up, normal), jump/up climbs (timed LedgeClimb phase),
  drop/down releases. A released ledge is ignored for ledgeRegrabTicks.
- **Ladder:** grab attaches, vertical*ladderSpeed; ladderTop + up runs a timed exit phase onto the top,
  ladderBottom/grounded + down exits to Ground; jump pushes off along the ladder normal.
- **Swim:** immersion >= swimEnterDepth enters (not while rising), < swimExitDepth leaves. Buoyancy
  drives the body to floatDepth; vertical input dives/surfaces (Dived/Surfaced at diveDepth); a jump
  with the head above water leaves the water at waterJumpSpeed.
- **Climb:** grab on a climbable wall (climbEnabled); moves along the wall, consumes stamina per tick
  (built-in counter or the owner's pool through `stepLocomotionWithStamina`), wall-jumps, reaches a
  reported ledge at the top or falls with StaminaDepleted.
- **Mantle/vault:** jump next to an obstacle mantleMin..mantleMax high starts a timed phase: rise for
  the first half, move over obstacleDepth for the rest. On the ground, obstacles up to vaultMaxHeight
  taken at >= vaultMinSpeed vault in vaultTicks and keep momentum. A ceiling during the rise aborts.

The owner keeps dt constant (phase velocities are computed at entry), calls once per tick, and moves the
body with swept collision. Invalid config/state/input, dt outside (0,1] and repeated/backwards ticks
return nullopt with the state untouched. All state (including the nested MotionState and mantle phase)
is persisted by `genre::save/load` (state kind 20) and hashed by `locomotionHash`; bind
`locomotionConfigHash` in the save envelope. Presentation climbing/IK (surface-locomotion.md) reads the
mode and events; it does not decide them. Network prediction/rollback is the owner's concern: restore
the saved record and replay the same inputs.

## Runtime contracts

**Motion:** `jumpPressed` is an input edge, not a held button. The physics owner must provide the actual
collision result on the next tick. Walking off a ledge consumes the ground jump once the coyote window
expires. A buffered press may fire on landing before expiry. The policy clamps terminal falling speed
at 1000 units/second and accepts dt in (0,1]. It does not add extra jumps to the FPS motor automatically.

**Melee:** one active attack owns a sorted list of already-hit target IDs. Use stable packed entity IDs
that fit positive int64 (or project-owned IDs). `recordMeleeHit` records only active-window contacts;
call it after the authoritative spatial/team check. `stepMelee` calls authorization only when an attack
can start; make spending atomic in that callback and issue resulting effects through the existing owner.
`resolveMeleeContact` prioritizes invulnerability, then parry, then block, then normal hit. It does not
compute another damage formula. Input buffering expires; no hit may be emitted twice for one swing.

**Weapons:** use `sim.rng()` with `sampleSpread`; invalid spread consumes no RNG. Sampling uses uniform
integer yaw/pitch offsets, not a uniform solid-angle cone. Persist recoil if it changes authoritative aim.
Projectile movement uses the world's real swept AABB collision (radius as equal half-extents), avoiding
fast-projectile tunneling through boxes. One impact is handled per tick; leftover travel after bounce is
discarded. Every impact is returned for the damage/effects owner to process. Lifetime counts completed
steps, so lifetime=1 moves once. Projectiles leaving +/-100000 world bounds are clamped and terminated.
No physics position, velocity, ammunition or damage is silently copied into the existing FPS actor state.

**Matches:** initialize unique positive team IDs with makeMatch. `awardScore` accepts nonnegative points
only during live/overtime, with strictly increasing event IDs per team. Persist those IDs across rounds;
never reuse them while delayed commands can arrive. Step evaluates score/clock transitions. Overtime
ends on a sole leader or its limit; tied rounds may draw. Match win uses round wins. Capture points
freeze while contested, decay progress by one per empty tick, and commit ownership at the threshold.
The caller validates presence and awards associated score/rewards. Presence counts are not trusted input.

**Narrative:** available choices are stable by ID. `chooseStory` rechecks conditions, applies all fact
changes to a copy, rejects integer overflow, commits the next node and records the command ID atomically.
One explicit choice produces one transition; loops are allowed without recursive auto-execution.
Only project-owned story facts are mutated. For rewards/inventory, use an existing transactional owner
and an idempotent event from the committed story transition; do not treat a fact increment as a wallet.

## State and verification

`genre::save/load` provides version-1 Archive records for MotionState, MeleeState, RecoilState,
ProjectileState, MatchState, CaptureState and StoryState. Each subsystem needs its own key;
records include a state-type discriminator. Load validates limits and canonical encoding, rejects
truncation/trailing data without replacing destination. Bind the exact immutable configuration and
definition versions in the project save envelope. `storeBytes/loadBytes` reuse runtime/NativeBytes.h
to bridge records to NativeState arrays. Bot persistence uses the same byte bridge.

Run catalog.py check, generate_gameplay.py --check, tests/libraries/run.py and native_consumer.py.
Run root `aiverse_library_genre_tests` and the standalone test under ASan/UBSan. For engine wiring, add
real gameplay acceptance and replay/save checks. Current tests exercise the C++ policies and real
collision; they are not a claim that a complete platformer/fighting/story UI is shipped. No rendering
changes in this batch. Windows, Metal presentation, mobile and web runtime paths remain unverified.

The broader proposed scope is in [genre-mechanics-roadmap.md](genre-mechanics-roadmap.md).

## Verified integration snapshot (2026-11-11)

AIVERSE namespace/targets/includes were used for the final integration. All 5771 engine checks,
9 standalone library suites, native/export consumer builds and all three 200-tick native restore
modes passed. Separate RTS/FPS controller tests compare 300 ticks after a fresh restore. Real
library-controlled matches completed: skirmish at tick 8081 (217 commands), FPS arena at tick 264
(2640 commands, 58 shots, 5 deaths). All 30 templates ran without process errors; fourages reached
its 1800-second limit, and the four FPS reference-template runs were 30-second smoke tests without
winners. ASan/UBSan passed the genre and navmesh regression scenarios. Cross-platform runtime and
rendered UI remain outside this verification.

## Combat actions (combat-actions 0.1.0)

Header `genre/CombatActions.h` (+ `CombatConfigured.h` settings adapters, `CombatData.h` hitbox JSON).
Target `AIVERSE::lib_combat_actions` (depends on `lib_melee_combat`, `aiverse_engine`); native OBJECT
modules call `aiverse_native_use_libraries(target combat-actions)`. Skill: `skills/aiverse-combat-actions`.

**Shapes and hits:** Fixed metres; yaw in turns around +Y (yaw 0 faces +Z, +X is right). `CombatShape` is a
sphere (centre a), capsule (segment a..b) or box (centre a, half extents, yaw-only rotation, radius 0).
`overlaps` is exact for sphere/box/box-box (SAT) and uses a deterministic integer ternary search over the
segment parameter for capsules; touching counts. `HitboxDef{id,from,to,shape}` is live for attack ages
`[from,to)` which must lie inside the attack's active window. `collectMeleeHits` transforms hitboxes by the
attacker pose and hurtboxes by each target pose, skips self, same nonzero team (unless friendlyFire) and
targets already hit this swing, and records hits through `recordMeleeHit`. Contacts are sorted by target ID
and name the first overlapping hitbox/hurtbox by ascending IDs. Invalid data or duplicate target IDs return
nullopt without touching state. Overlaps are sampled per tick (no sweep between ticks).

**Defense:** `stepDodge` (authorize callback, cooldown counted from the dodge end) and `dodgeInvulnerable`
(`[invulnFrom,invulnTo)` from the start). `stepGuard` raises/lowers; raising opens a `parryTicks` window
unless the previous raise was less than `rearmTicks` ago. `insideGuardArc` compares the attacker bearing with
the defender yaw (`halfArc` turns each side). `resolveCombatContact` feeds Melee.h `resolveMeleeContact`
(invulnerable > parry > block > hit); `unblockable` skips the guard.

**Poise:** integer milli-units. `stepPoise` once per tick (initializes to maximum, refills
`recoveryPerTick` after `recoveryDelay` since the last poise damage, resets to full after a break stun).
`applyPoiseHit` on the same tick: Hit drains full damage + hit-stun, Blocked drains `blockScaleMilli/1000`
+ block-stun, Parried/Evaded drain nothing. Reaching 0 breaks poise (`breakStunTicks`); while broken further
hits add nothing. `applyParryStagger` stuns the parried attacker; `interruptMelee` cancels the swing.

**Lock-on:** `lockScore = distance/range*distanceWeight + |angle|/halfAngle*angleWeight`; lower wins,
ties by smaller ID. `stepLockOn`: break on missing/untargetable/beyond `breakRange`; toggle releases or
acquires; switch picks the nearest angular neighbour on that side among scorable candidates (no wrap),
then distance, then ID. Candidate visibility (`targetable`) comes from the owner's authorized senses.

**Settings:** optional `combat` section in `libraries/spec/gameplay.json` (configuration 0.3.2):
dodgeTicks/dodgeInvulnFrom/dodgeInvulnTo/dodgeCooldown, blockHalfArcMilli (milli-turns), parryTicks,
parryRearmTicks, poiseMilli, poiseRecoveryMilli, poiseDelayTicks, blockPoiseMilli, hit/block/break/parry
stun ticks, lockRangeMilli/lockBreakMilli (milli-metres), lockHalfAngleMilli (milli-turns), lock weights.
The loader rejects `dodgeInvulnFrom > dodgeInvulnTo`, `dodgeInvulnTo > dodgeTicks` and
`lockRangeMilli > lockBreakMilli`. Hitboxes: `readHitboxes(json, attacks)`, see
`examples/libraries/genre/hitboxes.json`; unknown keys/attacks, frames outside the active window, bad shapes
and non-ascending IDs fail with path-qualified messages.

**State:** `CombatActorState{dodge,guard,poise,lock}` and each member are `genre::save/load` records
(kinds 32..36 reserved for this package), canonical and validated on load. `combatHash` / `hashCombatState`
hash the canonical bytes for the simulation checksum. All steppers reject repeated/backwards ticks.
`GenreConsumer.cpp` runs one swing into a defender, resolves the contact, applies poise and stores
`genre.combat`, `genre.combat_swing` and `genre.combat_hash` in NativeState. Verified only with g++ 11
(Linux VM): header syntax checks, `aiverse_library_genre_tests` sources built by hand (also ASan/UBSan) and
the consumer function in a standalone driver. Full engine build, aiverse_tests, CMake isolation tests,
native module export and headless restore matches were not run for this package.

## Weapons and ballistics (weapon-policies 0.2.0)

New headers compose `Weapons.h` without changing it: `genre/Ballistics.h` (penetration, explosions,
aiming), `genre/WeaponMods.h` (ammo feeds, charge, attachments), `genre/WeaponData.h` (settings
adapters, weapons JSON) and `genre/WeaponPersistence.h` (save/load, hashing). All math is Fixed/integer;
invalid input returns `std::nullopt` (or `false`) without calling owner callbacks or mutating state.

**Penetration.** `traceLayers(world, start, delta, materialOf)` returns every collider crossed by the
segment in (entry, collider id) order with geometric thickness (box entry/exit from two swept casts;
triangles are zero-thickness sheets). Overlapping colliders are separate layers. `resolvePenetration`
walks the layers with a `MaterialTable`: each layer costs `resistance * thickness` power
(thickness at least `minThickness`), damage and speed are multiplied by `1 - falloff * thickness`
(floored at 0). It stops on unknown/impenetrable material, exhausted power, `minDamage`, `minSpeed`
or `maxLayers`, reporting `PenetrationStop` and per-layer `damageIn` (damage for that layer's occupant)
and exit values. Applying damage to occupants remains the combat owner's job.

**Explosions.** `resolveExplosion(config, center, targets, exposure)`: full effect inside `innerRadius`,
linear falloff to `edgeScale` at `outerRadius`, nothing beyond. Distance is to the target's bounding
sphere (radius). Targets need unique positive IDs. The owner-supplied `exposure(target)` (LOS/cover, 1
visible, 0 occluded, clamped) is called once per in-range target in (distance, id) order, never for
out-of-range ones, and stops at `maxTargets` exposed hits. Occluded targets are omitted. Results carry
damage, impulse magnitude and direction (up for a coincident target), in the same order regardless of
input order.

**Ammo feeds.** `stepFeed(config, state, tick, {fire, reload, cancel}, draw)` is a per-tick state machine
(one call per weapon per tick, strictly increasing ticks). Kinds: Magazine (whole reload, optional longer
empty reload), Belt (each shot draws from the owner's reserve), PerShell (one round per `shellTicks` after
`reloadStartTicks`), Energy (no ammunition). Optional heat on every kind: overheat at `heatCapacity`,
locked at least `overheatTicks` and until `heat <= recoverHeat`; cooling `coolPerTick` per elapsed tick.
`intervalTicks` and `spinupTicks` (held-trigger ticks before the first shot) are optional cadence rules.
`draw(n)` must atomically remove up to n rounds from the owner's reserve (inventory, meta wallet, ...)
and return the granted count; it is only called after validation, so the step always commits. Order per
tick: cooling/recovery, reload cancel/interrupt, reload start (explicit or auto on dry fire), reload
progress, fire. Firing with rounds left interrupts an interruptible reload: magazine progress is lost,
inserted shells stay. These records are separate from `modules/fps` actor ammo: never mirror one magazine
in both owners.

**Charge modes.** `stepCharge(config, state, tick, {held, cancel})` (level input, one call per tick).
Release before `minTicks` cancels (or fires tier 0 with `fireBelowMin`); held ticks are capped at
`maxTicks`; `tier` counts reached thresholds, `fraction` is min-to-max progress, `scale` is the tier's
scale (or `baseScale`). `autoReleaseTicks` fires automatically and latches until the trigger is released.

**Attachments.** `WeaponFrame` (stats with base/min/max, allowed slots) + `AttachmentSet` (one slot each,
add/mul modifiers). `equipAttachment` validates slot/ID and returns the replaced attachment;
`resolveStats` applies base, then every Add, then every Mul (slot id ascending, authored order,
sequential Fixed rounding), then clamps. Equip order never changes the result. Intermediates are clamped
to +/-1e10 so no data overflows.

**Aiming.** `solveLaunch(delta, speed, gravity)` returns 0, 1 or 2 launch solutions (low/high arc,
gravity along -Y as in `ProjectileConfig`). It solves for flight time, so the analytic trajectory passes
through the target up to Fixed rounding and |velocity| approximates `speed`; a power-of-two scaled
discriminant keeps large ranges inside Q47.16. `solveIntercept(config, origin, target, velocity, ...)`
leads a constant-velocity target with at most `maxIterations` solves; `converged` reports consecutive
flight times within `tolerance` seconds. `found=false` when the predicted point becomes unreachable.

**Configuration.** Generated sections `penetration`, `explosion`, `ammoFeed` (`kind`: 0 magazine, 1 belt,
2 perShell, 3 energy), `charge` (`tiers` evenly spaced from minTicks to maxTicks, scales from minScale
to maxScale) and `aimSolver`, all milli-units/ticks and optional. Adapters: `penetrationConfig`,
`explosionConfig`, `feedConfig`, `chargeConfig`, `aimConfig`; `weaponSettingsErrors` reports readable
cross-field problems (ammoPerShot vs capacity, shellTicks, recoverHeat, autoRelease, tier spacing).
Tables use `readWeaponDefinitions` (`examples/libraries/genre/weapons.json`): `materials`, `frame`,
`attachments`; unknown keys, duplicates, unknown slots/stats, bad ops and ranges throw
`JsonError("weapons.<path>: ...")`. Example settings: `examples/libraries/genre/weapon-settings.json`.

**State.** `FeedState`, `ChargeState` and `LoadoutState` use `genre::save/load` (version 1, kinds 16-18;
canonical encoding, invalid records rejected). Store each in its own NativeState key and bind the
immutable configs/definitions in the project save envelope. `hashWeapon(hasher, value)` / `weaponHash`
fold the canonical encoding of states and of per-call outcomes (penetration, explosion hits, launch,
feed/charge steps) into the owner's checksum. `GenreConsumer.cpp` validates settings and definitions,
runs each policy once and stores initial feed/charge/loadout records.

Verified: header syntax checks (g++ 11, `-Wall -Wextra -Werror -pedantic`), `genre_tests.cpp` with g++
(also under ASan/UBSan) covering penetration chains, explosion ordering/falloff/occlusion, feed/reload/
overheat sequences, charge tiers, attachment stacking, aim accuracy, save/restore and invalid data, and a
standalone run of the GenreConsumer function. Not verified in that change: CMake/full engine build,
`aiverse_tests`, headless matches, MSVC/clang, platform runtimes.

## Match objectives (match-rules 0.2.0)

New headers compose `Match.h` without changing it: `genre/MatchObjectives.h` (policies, scoring bridge,
persistence), `genre/MatchObjectivesConfigured.h` (settings adapters) and `genre/MatchObjectivesData.h`
(layout JSON). Every stepper takes integer ticks, rejects repeated/backwards ticks and invalid config/state/
input with `std::nullopt` without mutating state, and returns `ObjectiveEvents` (`kind`, `team`, `subject`,
`value`; team 0 is informational, e.g. a draw). Distances are Fixed metres. The owner supplies only
validated facts: `Occupants` (team -> bodies present, e.g. counted with `insideZone`), cart pushers/defenders,
deaths, bodies inside an extraction zone, bomb carrier/defusers/pickups and gate crossings (`crossesGate`).

- **Zones / KotH.** `stepZone`: one present team captures at `min(bodies, maxCapturers)` per tick; another
  team first drains existing progress; empty zones decay by `decayPerTick`; the owner's presence reverts enemy
  progress at its own rate. `ContestRule::Freeze` freezes on any second team, `Majority` advances the strict
  majority by the body difference (ties freeze). `neutralizeFirst` makes an owned zone neutral at the threshold
  before it can be captured. `ZoneContested` fires when contest starts; `state.contested` is the current flag.
  Hold scoring: `ZoneHoldTick` every `holdScoreInterval` ticks while the owner controls the zone (or merely owns
  it with `holdRequiresPresence=false`); the capture tick counts. `kingOfTheHill(captureTicks, interval)` is the
  preset; `activeHill(HillRotation, start, tick)` gives the rotating hill index (reset the zone on rotation).
  Match.h `stepCapture` is unchanged (simple variant: resets instead of drains).
- **Payload.** `makePayloadTrack(points, checkpoints)` (polyline, strictly ascending checkpoints inside the
  length), `payloadPoint` maps progress to a position. `stepPayload(config, track, state, attackers, defenders,
  tick)`: pushers move the cart at `pushSpeed * (1 + extraPusher * (n - 1))`, n capped by `maxPushers`;
  defenders block when `contestBlocks`; after `reverseDelayTicks` without attackers it rolls back at
  `reverseSpeed`, never behind a reached checkpoint with `checkpointLock`. `PayloadCheckpoint` carries
  `checkpointBonusTicks` for `extendClock`; `PayloadDelivered` is terminal.
- **Elimination.** `makeElimination(config, roster)`; `stepElimination(config, state, tick, deaths)` (unique
  living players only). `lives` counts the first life (0 = unlimited). Respawn at the first wave
  (`start + k * respawnWaveTicks`) not earlier than death + `respawnDelayTicks` (wave 0: individual timer).
  A team with no lives left (or every member dead at once with `wipeEliminates`) emits `TeamEliminated`; when
  at most one team remains, `LastTeamStanding` (team 0 and `lastStanding = -1` for a mutual wipe).
- **Extraction.** `stepExtraction(config, state, tick, inside)`: from `opensAtTicks` after the first step,
  each body carrying `requiredItems` progresses one tick, frozen while another team is inside
  (`contestBlocks`); leaving resets (`resetOnLeave`) or freezes progress. `Extracted` once per player.
- **Bomb / defuse.** `makeBomb(attackers, defenders, carrier)`; `stepBomb` with `BombInput{plantSite,
  defusers, pickups, carrierDown}`. Only the carrier plants (switching site restarts); releasing interrupts
  (`plantResets`); a downed carrier drops the bomb; the lowest attacker ID among pickups takes it. Once
  planted (`BombPlanted.value` = explosion tick) the current defuser keeps priority, else the lowest defender
  ID; a kit uses `kitDefuseTicks`; switching/releasing interrupts (`defuseResets`). The fuse is checked first,
  so a defuse finishing on the explosion tick loses. `bombPending` reports a planted bomb for overtime.
- **Route gates.** `makeRoute(racers)`; `stepRoute(config{gates, laps}, state, tick, crossings)` applies
  crossings per racer in given order and ignores out-of-order gates: `GatePassed`, `LapCompleted`,
  `RouteFinished` (value = place). `routeStandings`: place, then lap/gate progress, earlier last gate, ID.
- **Clock and overtime.** `startClock(config, tick)` (limit = `matchRules.roundTicks` via
  `objectiveClockConfig`), `extendClock` (bounded by `maxDeadlineTicks` from the start; leaves overtime),
  `stepClock(config, state, tick, contested)`: at the deadline a contested objective (owner-defined:
  `zone.contested`, attackers on the cart, `bombPending`, ...) starts overtime, which ends `graceTicks` after
  the last contested tick or at `maxOvertimeTicks` (0 = uncapped) with `OvertimeEnded` + `TimeExpired`.
- **Spectators.** `spectatorDied(config, state, killer, tick)` then `stepSpectator(config, state, tick,
  candidates, cycle)`: kill cam on a living killer for `killCamTicks` (cycling ends it), then follow: keep the
  current target while eligible, else the first eligible after it in the order teammates by ascending ID then
  enemies by ascending ID (`allowEnemies`), wrapping; nobody eligible -> `Free`. `cycle` +1/-1 walks the order.
- **Scores.** `awardObjectiveEvents(match, scoring, events, nextEvent)` awards points per event kind through
  `awardScore` with the owner's strictly increasing event counter; all awards commit or none. 
  `awardObjectiveWin(config, match, team, nextEvent)` makes the team the sole leader with at least
  `scoreToWin`, so the next `stepMatch` closes the round. Built-in victory modes keep their own owners.

**Configuration.** Optional generated section `objectives` (configuration package 0.3.2 spec): zone*,
holdScoreInterval/holdRequiresPresence, hillRotateTicks/hillCount, payload* (milli-metres per tick),
eliminationLives/respawnDelayTicks/respawnWaveTicks/wipeEliminates, extraction*, bomb*, overtimeEnabled/
overtimeGraceTicks/overtimeMaxTicks/clockMaxTicks, spectator*, routeLaps. The loader rejects
`zoneDecayPerTick > zoneCaptureTicks` and `bombKitDefuseTicks > bombDefuseTicks`; `objectiveSettingsErrors`
reports hill rotation without a period, clockMaxTicks below roundTicks, waves with a single life, grace
beyond the overtime cap and kit defuses that cannot beat the fuse. Adapters: `zoneConfig`, `hillRotation`,
`payloadConfig`, `eliminationConfig`, `extractionConfig`, `bombConfig`, `objectiveClockConfig`,
`spectatorConfig`, `routeConfig(settings, gates)`. Layouts: `readObjectiveLayout` (zones with kind
capture/hill/extraction/bombSite, payload path + checkpoints, route gates); unknown keys, duplicate IDs, bad
kinds, short/repeating paths, checkpoints outside the path, non-ascending gate IDs and short normals throw
`JsonError("objectives.<path>: ...")`. Examples: `examples/libraries/genre/objectives.json`,
`examples/libraries/genre/objective-settings.json`.

**State.** `ZoneState` 40, `PayloadState` 41, `EliminationState` 42, `ExtractionState` 43, `BombState` 44,
`RouteState` 45, `ObjectiveClockState` 46, `SpectatorState` 47 (kinds 40..47 reserved for this package;
version 1, canonical, validated on load). Store each in its own NativeState key; bind settings and layout
versions in the save envelope. `hashObjective(hasher, state)` / `objectiveHash` fold the kind + canonical
bytes into the checksum; `hashObjective(hasher, events)` hashes per-tick outcomes. `GenreConsumer.cpp`
(`libraryMatchObjectivesConsumerExample`) validates settings and a layout, steps a zone, cart and clock
once, awards events through the match and stores six records.

Verified with g++ 11 only (Linux VM): header syntax checks (`-Wall -Wextra -Werror -pedantic`),
`genre_tests.cpp` built by hand and run (also under ASan/UBSan) covering every mode, contest/decay/
interruption edge cases, a multi-mode 80-tick match restored mid-way with identical hashes, invalid
records/settings/layouts, and the GenreConsumer chain in a standalone driver. Not verified: CMake/full engine
build, `aiverse_tests`, native module export, headless matches, clang/MSVC, platform runtimes.

## Stealth and senses (stealth-senses 0.1.0)

Headers `genre/Senses.h` (policy), `genre/SensesConfigured.h` (settings adapters), `genre/SensesData.h`
(disguise/hiding JSON) and `genre/SensesPersistence.h` (save/load, hashing). Target `AIVERSE::lib_stealth_senses`
(depends on `aiverse_engine`); native OBJECT modules call `aiverse_native_use_libraries(target stealth-senses)`.
Skill: `skills/aiverse-stealth-senses`. This is a generic per-agent perception policy for guards, monsters and
bots; RTS fog of war stays in `modules/rts` (RtsVision). Long-term memory/threat ranking can compose `ai-perception`.

**Tick.** `stepSenses(config, defs, state, tick, input, los, occlusion)` once per authoritative tick, strictly
increasing ticks; invalid config/definitions/state/input (duplicate or non-positive IDs, out-of-range
multipliers 0..4000, unknown disguise/spot IDs, bad noise radius) or a repeated/backwards tick return nullopt and
leave the state untouched. Input lists may arrive in any order; they are processed by ascending ID. Order inside a
tick: expire markers, deliver due alerts, disguise equip/blow, hiding exits, sight, hiding entries, hearing,
trail detection, marker deposits, suspicion, alert broadcasts, search, records. Observers absent from the input
are frozen (no decay, no search progress); alerts addressed to them are dropped (`AlertDropped`).
`stepSensesOpen` uses always-visible / unoccluded callbacks (tests, open fields).

**Sight.** Fixed metres; yaw in turns around +Y (yaw 0 faces +Z). Eye = position + eyeHeight. Zones: Focus
(|bearing| <= halfAngle, distance <= range; strength nearMilli inside nearRange falling linearly to farMilli at
range), Near (any bearing within nearRange, nearMilli) and Peripheral (|bearing| <= peripheralHalfAngle,
distance <= peripheralRange, peripheralMilli); the strongest applies. Strength is then multiplied by the
target's light/cover/stance/visibility and the observer's sightMilli (milli, capped at 1000). Only samples
>= `sight.minMilli` reach `los(agent, target)`, called in (observer id, target id) order, at most
`sight.maxQueries` per tick (`losSkipped` counts the rest). Self and same nonzero team are never sensed.

**Hearing.** `NoiseEvent{id, source, team, position, radius, loudnessMilli}`; source 0 = anonymous disturbance
(capped at Searching). Strength = loudness * (radius - distance)/radius * occlusion/1000 * hearingMilli/1000.
At most `hearing.maxNoises` events per tick are processed, loudest first then smaller ID (`NoiseDropped`
events for the rest). `occlusion(agent, noise)` (milli 0..1000, clamped) runs in that noise order then listener
ID, only for listeners whose unoccluded strength reaches `thresholdMilli`, at most `hearing.maxQueries` per tick.
Same-team noises are ignored; hidden targets still make noise.

**Trails.** `SenseTarget::leaveTrail` deposits a marker (strength trailMilli) after this tick's detection.
Markers decay linearly to zero over `trail.lifetime` and are removed then; beyond `maxMarkers` the oldest goes.
Observers sense the strongest marker per owner within `trail.range` (scaled by distance and trailMilli,
>= minMilli; ties by smaller marker ID) without LOS.

**Suspicion.** One `AwarenessRecord` per (observer, target): gain per tick = (sight*sightGain + hearing*hearingGain
+ trail*trailGain)/1000, scaled by an active disguise. Levels: >= alertAt Alert, >= searchingAt Searching,
>= suspiciousAt Suspicious (1 <= suspiciousAt < searchingAt < alertAt <= maximum). Without stimulus the meter
decays by decayPerTick per tick starting decayDelay ticks after the last stimulus (exact across skipped ticks),
never below searchingAt for the active search target; records at 0 are dropped. The last known position follows
sight > hearing > trail; alerts update it when newer.

**Alerts.** A sensed transition into Alert (or any transition with `alert.relay`) schedules `PendingAlert`s to
allies of the same nonzero team within `alert.radius` of the sender that are not already Alert about that
target, delivered `alert.delay` ticks later (bounded by `maxPending`). Receipt raises the receiver's meter to
`receivedMilli` and shares the sender's last known position and its tick.

**Search.** When the strongest Searching/Alert record is not sensed this tick, the observer starts one search:
points = last known position, then owner `SearchCandidate`s within `candidateRadius` by (distance, ID), up to
`maxPoints`. `searchGoal(state, observer)` is the current point; reaching it (horizontal distance <=
pointRadius, one point per tick) emits `SearchPointReached`. A new stimulus (or a newer shared position)
re-plans and extends the deadline; seeing the target at Alert ends with `SearchFound`. Visiting every point or
`giveUpTicks` without contact ends with `SearchGaveUp` and drops the meter to `suspiciousAt`.

**Disguises.** `DisguiseDef{id, factions, zones, blownBy, scaleMilli, trespassScaleMilli}`: observers of the
listed teams scale gains from the wearer by scaleMilli inside the zones (empty = everywhere), by
trespassScaleMilli elsewhere. Any action in blownBy blows it (`DisguiseBlown`) for everyone until the owner
equips again (`equipDisguise` > 0; 0 removes).

**Hiding.** `enterHiding` succeeds within `hiding.enterRange` of the spot and below its capacity, otherwise
`HidingRefused`. Observers whose LOS succeeded on the entry tick keep seeing the target (`revealedTo`); all other
observers cannot see it (no LOS call) until their search reaches a candidate carrying that spot
(`HidingRevealed`). `exitHiding` or moving beyond enterRange exits (`HidingExited`, extra 1 when automatic).

**Outputs for bots.** `PerceivedTarget{observer, target, level, suspicion, lastKnown, lastKnownTick, lastSense,
sensedNow, visibleNow, strengthNow, searching}` sorted by (observer, target), one per record of present
observers. This is the contract for genre bot policies: chase/engage on `visibleNow && level==Alert`, investigate
`lastKnown` or `searchGoal` when Searching, idle otherwise. `SenseEvent{kind, observer, target, value, extra}`
lists transitions in tick order. Callers convert records to `ai-perception` observations if they need TTL memory.

**Settings.** Optional `senses` section (configuration 0.3.3): sight* / peripheral* (milli-metres, milli-turns,
milli strengths, sightQueryBudget), hearingThresholdMilli, noiseBudget, occlusionBudget, noiseMaxRadiusMilli,
trail*, suspicion thresholds/gains/decay, alert*, alertRelay, search*, hideEnterRangeMilli. The generated loader
rejects relation violations (`senses.sightNearMilli: must not exceed sightRangeMilli`, ...);
`sensesSettingsErrors` adds strict threshold order. Example: `examples/libraries/genre/senses-settings.json`.
Definitions: `readSenseDefinitions` (`examples/libraries/genre/senses.json`); unknown keys, duplicate IDs,
empty factions, bad positions/capacities/scales throw `JsonError("senses.<path>: ...")`.

**State.** `SensesState` (kind 48) holds observers (records + search), markers, pending alerts, disguises and
hiding; `ObserverState` 49, `SearchState` 50, `DisguiseState` 51, `HidingState` 52 are also records. Kinds 48..55
are reserved for this package. Records are canonical and validated on load. `sensesHash` hashes states and
reports; `sensesConfigHash` binds the config. `removeSenseAgent` drops a destroyed entity everywhere.
`GenreConsumer.cpp` (`librarySensesConsumerExample`) runs one tick for a guard pair and an intruder and stores
`genre.senses`, `genre.senses_hash` and `genre.senses.config`.

Verified only with g++ 11 (Linux VM): header syntax checks (`-Wall -Wextra -Werror -pedantic`),
`genre_tests.cpp` built by hand (also under ASan/UBSan) covering cones/ranges, LOS order and budget, noise
occlusion/budget, suspicion thresholds/decay, alert delay/relay, search planning/give-up/re-plan, disguises,
hiding/reveal, trails, save/restore determinism and invalid data, and the GenreConsumer chain in a standalone
driver. Not run: CMake/isolation, `aiverse_tests`, native module export, headless restore matches, clang/MSVC.

## Genre bots (genre-bots 0.1.0)

Headers under `bots/`: `GenreCommon.h` (difficulty, perception record, target pick), `MeleeBot.h`, `ShooterBot.h`,
`ObjectivePolicy.h`, `SquadRoles.h`, `GenreBotPersistence.h`, umbrella `GenreBots.h`, settings adapters
`GenreBotConfig.h`. Target `AIVERSE::lib_genre_bots` (depends on `combat-actions`, `weapon-policies`,
`configuration`); native OBJECT modules call `aiverse_native_use_libraries(target genre-bots)`. Skill:
`skills/aiverse-genre-bots`. Namespace `aiverse::lib::bots`; the policies are independent of genre labels.

**Contract.** Each policy is a pure decision step: one call per bot (or squad) per strictly increasing tick
(repeated/backwards ticks and invalid input return nullopt with state untouched). It reads an owner-supplied
snapshot and returns the same input a player produces; the owner applies it through its ordinary authorization
(`stepMelee`/`stepDodge`/`stepGuard`/`stepLockOn`, `stepFeed`/`stepRecoil`, navigation, `Command`s). Bots never
write health, poses, ammunition, cooldowns or match state, and difficulty never changes stats. Run them on the
authoritative host only; replays record the resulting commands. Randomness only through the passed `Random&`
(`sim.rng()` or an RNG saved with the simulation); draws happen only for chances strictly inside (0,1000) and for
aim error > 0, so the stream is reproducible.

**Perception.** `PerceivedTarget{id,team,position,velocity,yaw,visible,guarding,vulnerable,healthMilli,threat}` is the
minimal hand-off from the owner's senses (LOS/FOV/hearing). Only visible hostile targets are engaged (same nonzero
team and the bot itself are skipped); the current lock/target is kept while visible, else nearest by distance then id.
`meleeThreat(attacks,meleeState,tick,reach)` derives the telegraph (attack id, start, windup, active, reach) from the
visible attack phase. A senses package can adapt its output to this struct without changing the policies.

**Difficulty.** `BotDifficulty{reactionTicks,decisionTicks,aimError,misreadMilli}`; presets `difficultyPreset(0..2)`
(easy 20/8/1/40 turn/600, normal 5/4/1/200/150, hard 2/2/1/2000/0). Reaction delays acting on a newly perceived
target and reading a telegraph (counted from the swing start); the decision interval re-plans movement and
re-samples aim error; misreads ignore a read swing.

**Melee bot.** `stepMeleeBot(config, attacks, dodge, guard, state, tick, self, seen, rng) -> MeleeInput`.
Spacing band `[minRange,maxRange]`: approach beyond, retreat inside, strafe within (side flips every strafeTicks;
outside `attackRange` the strafe also closes in). Openings: when idle and within `attackRange` it requests `opener`
with `openMilli` chance per tick (avoids mirrored trades), never into a raised guard when `respectGuard` unless the
target is `vulnerable` (recovery/stagger: always punished). Combos: only after a hit confirm on this swing, requested
when `age + bufferTicks >= cancelFrom` so stepMelee starts the follow-up at the cancel frame. Defense: a swing in
reach is read once after the reaction delay; the answer is Dodge (start at `activeStart - invulnFrom`, so the
i-frames cover the first active frames), Guard (raise at `activeStart - parryTicks/2`, perfect-parry window straddles
the first active frame; hold until the swing ends), Ignore (misread) or Retreat (no dodge ready and unblockable).
`dodgeMilli` biases dodge vs guard. Lock-on: toggles to acquire once reacted. Stagger/committed swings hold position.

**Shooter bot.** `stepShooterBot(config, feed, state, tick, self, seen, cover, rng) -> ShooterInput`. View turns at
most `turnPerTick` per axis towards the solution plus seeded aim error, minus `recoilCompensationMilli` of the current
recoil. Hitscan aims at the reported position; projectile weapons (`projectileSpeed > 0`) lead through
`genre::solveIntercept` with `aim` (AimConfig) and do not fire when no solution exists. Fire (held trigger level) only
when aligned within `fireTolerance`, recoil settled under `recoilTolerance` (burst control), in `maxRange`, round
available and the feed Ready. Reload when the magazine cannot fire and reserve remains, or with no target below
`reloadIdleMilli`; never while engaging with rounds left. Heat hysteresis: stop at `heatStopMilli`, resume at
`heatResumeMilli` of capacity (or while Overheated). Cover: while hurt (`retreatHealthMilli`), reloading or cooling
with a target, choose the reachable candidate with the best `protectionMilli - distance*coverDistanceWeight` (ties by
id), keep it until the next decision, move towards it (still firing when possible). Band: approach beyond
`preferredRange`, retreat inside `minRange`, strafe between. Decisions Idle/Engage/Reload/Cool/Cover are diagnostics.

**Squad roles.** `stepSquad(config, state, tick, objective, stance, members) -> SquadPlan`. Counts per role from the
stance mix (`offense`/`defense` weights, largest remainder, ties by role index). Reassignment on the first call,
when the alive composition (ids, capability masks, stance) changes, or every `intervalTicks`; otherwise stored roles
and heading keep goals stable. Fill order Support, Flanker, Defender, Attacker; per slot the unassigned member with
the smallest key (cannot take the role, keep: same previous role < new or surplus role < other, squared distance to
the role anchor, id), so survivors keep roles after a death when counts allow. Goals: attackers in `attackShape` at
the objective, defenders on a ring of `defendRadius`, support in a line `supportDistance` behind, flankers in a
column `flankDistance` to the right of the heading. Orders are goals for each member's own controller/navigation.

**Objective hook.** `chooseObjective(weights, team, from, objectives, current) -> ObjectiveChoice{id,score,stance}`
over owner-supplied `ObjectiveInfo{id,kind(Capture/Hold/Escort/Attack/Defend),position,owner,valueMilli,progressMilli,
allies,enemies,active}`. Integer score: value, minus distance cost, plus neutral/enemy-owned bonus and this team's
progress for points it does not hold, threatened/ownedIdle for its own points and Defend objectives, escort + progress
for its own payload, contested bonus, stickiness for the current choice; ties by smaller id. Stance Defense for own
points/payloads and Defend objectives. Feed `ObjectiveInfo` from the match/objective owner (capture state, payload
progress); the policy does not evaluate any rules itself.

**Settings.** Optional `genreBots` section (configuration 0.3.3): reaction/decision ticks, `aimErrorMicro` (micro-
turns), misread, melee band/attack range/opener/strafe/dodge preference/use flags, shooter band, projectile speed and
gravity (milli), turn/fire/recoil tolerances (micro-turns), recoil compensation, reload/heat/retreat thresholds,
cover distance weight, squad interval, offense/defense role weights, formation spacing/support/flank/defend radius,
`attackShape` (0 line, 1 wedge, 2 column, 3 ring) and objective weights. The loader enforces the relations
`meleeMin <= meleeMax`, `shooterMin <= shooterPreferred <= shooterMax`, `heatResume <= heatStop`;
`genreBotSettingsErrors` adds readable cross-field errors (attack range below the band, projectile speed outside
0 or 63..1000000, gravity without speed, all-zero role weights). Example: `examples/libraries/genre/bot-settings.json`.

**State.** `MeleeBotState`, `ShooterBotState` and `SquadState` are `genre::save/load` records (version 1, kinds 56,
57, 58; 59..63 reserved for later bot adapters), canonical and validated on load; `genreBotHash` /
`hashGenreBot` fold them into the simulation checksum. Store each in its own NativeState key and bind configs,
attack sets and weapon definitions in the save envelope. `GenreConsumer.cpp` (`genreBotConsumerExample`) runs one
tick of each policy and stores `bots.genre.melee`, `bots.genre.shooter`, `bots.genre.squad` and `bots.genre.hash`.

**Verification.** `tests/libraries/genre_tests.cpp` (`genreBots`) runs a melee bot-vs-bot duel over combat-actions
(hitboxes, dodge, guard/parry, poise, lock-on) and a hitscan duel over the weapon feed/recoil/spread with a small owner
simulation inside the test: reruns give equal hashes, a mid-fight save/restore continues bit-identically, and hard
beats easy over seeds with swapped sides. Unit checks cover spacing, reaction delay, dodge/parry timing, hit-confirmed
combos, lead, reload, heat hysteresis, burst control, cover, role stability, formations, objective scoring,
persistence and invalid settings/data. Verified only with g++ 11 (Linux VM): header syntax checks, the genre tests
built by hand (also ASan/UBSan) and a standalone run of the GenreConsumer function. Not verified: CMake/full engine
build, `aiverse_tests`, CMake isolation, native module export, headless restore matches, MSVC/clang, platform runtimes.

**Perception from stealth-senses.** `bots/SensesPerception.h` `perceivedFromSenses(records, observer, facts,
filter)` converts `genre::PerceivedTarget` (stealth-senses) into bot `PerceivedTarget`. A target that is
`visibleNow` at `engageFrom` (default Alert) is engageable and gets the owner's `TargetFacts` (team, pose,
velocity, guard, telegraphed attack); a target at `rememberFrom` (default Searching) or above is kept at its
`lastKnown` position with `visible=false`, so bots approach but never shoot or lock it. Anonymous disturbances
(target 0) are skipped; steer search with `genre::searchGoal`. Results are sorted by target id. `facts` returning
nullopt drops a target. This makes `stealth-senses` a package dependency of `genre-bots`.

## Narrative scenes (narrative-graphs 0.2.0)

Headers `genre/NarrativeScenes.h` (policy), `genre/NarrativeCues.h` (the `CueRequest` record shared with cinematic owners),
`genre/NarrativeSceneData.h` (JSON), `genre/NarrativeScenePersistence.h` (save/load, hashing) and `genre/NarrativeConfigured.h`
(settings). Target `AIVERSE::lib_narrative_graphs` (now declares `aiverse_engine`). The legacy `Story` / `readStory` /
`chooseStory` / `StoryState` API and record kind 7 are unchanged; `readNarrativeWorld` also loads the legacy
`examples/libraries/genre/story.json` (without `facts`, referenced keys become persistent int facts over the full int64
range starting at 0; without `scenes`, scene 1 enters the first node; legacy add changes clamp instead of rejecting overflow).
Example: `examples/libraries/genre/story-social.json`.

**Definitions.** All IDs are positive integers, all text is localization keys. `facts`: `int` (min/max/initial), `bool`
or `enum` (`values` names; JSON uses the names, C++ the index), `persistent` (world state kept across scenes; others reset
to `initial` at every StartScene). `characters`: `nameKey` and `meters` `affinity`/`trust`/`fear` with min/max/initial
(defaults from `narrative.meter*`). Character 0 is the player/narrator (speaker and knower only). `flags`: one-shot flags
(set once, never cleared by effects; `persistent:false` flags clear at StartScene). `chapters`: `act`, `titleKey`;
`initialChapter`; a scene's `chapter` is entered at StartScene. `scenes`: `entry` node. `nodes`: `textKey`, `speaker`,
`participants`, `cues`, `onEnter` effects and either `choices` or `next` (a linear line; `next:0` ends the scene) plus
optional `autoTicks`. `choices`: `textKey`, `next` (0 ends the scene), `once`, `conditions`, `effects`, `cues`.
`consequences`: `conditions` (checked when due), `effects`, `cues`. `barks`: `trigger`, `speaker`, `textKey`, `priority`,
`cooldown` (default `narrative.barkCooldownTicks`), `once`, `conditions`, `cues`.

**Conditions** (a list is AND): `{"all":[...]}`, `{"any":[...]}`, `{"not":{...}}`; `{"fact":id,"op":..,"value":..}` (int:
eq/ne/lt/le/gt/ge/between with min/max; bool/enum: eq/ne; a bool fact without value means true);
`{"meter":"trust","character":id,"op":"ge","value":n}` (default op ge: relationship thresholds); `{"knows":fact,"character":id}`
(default 0 = player); `{"flag":id}`; `{"visited":node}` (default ge 1); `{"chapter":id}` / `{"act":n}` (with op);
`{"chose":choice,"node":node}`; legacy `{"key","minimum","maximum"}`. Depth <= 16, <= 256 terms per list.
**Effects:** `{"set":fact,"value":v}`, `{"add":fact,"value":d}` (int facts, clamped), `{"meter":m,"character":id,"add"|"set":n}`
(clamped to the bounds), `{"reveal":fact,"to":[ids]}`, `{"forget":fact,"from":[ids]}`, `{"flag":id}`, `{"chapter":id}`,
`{"schedule":consequence,"afterTicks":n | "afterScenes":n}` (n >= 1), `{"cancel":consequence}`; legacy `{"key","value","add"}`.
**Validation:** unknown keys, wrong types, duplicate IDs, unknown characters/facts/flags/chapters/nodes/consequences, out-of-domain
values, broken edges and every line that cannot reach an ending (cycles without exit, graph-wise ignoring conditions) throw
`JsonError("narrative.<path>: reason")`. Parse errors use JSON array indexes (`narrative.nodes[0].choices[1]: unknown key 'nxt'`),
definition checks use IDs (`narrative.nodes[id=4].choices[id=2].next: unknown node 99`); `narrativeWorldError` returns the same
ID paths for C++-built worlds.

**Tick.** `beginNarrative(world, config, overrides)` initializes every fact and meter. `stepNarrative(world, config, state,
tick, input)` once per authoritative tick with strictly increasing ticks (repeated/backwards ticks, invalid config, duplicate
or zero command/request IDs return nullopt and leave the state untouched). Order: tick-delayed consequences due at this tick
(by due tick, then scheduling order), commands by ascending ID, skip-read / auto-advance, bark requests by ascending ID.
Commands: `StartScene(scene)` (resets scene-local facts/flags, enters the scene chapter, fires scene-delayed consequences,
enters the entry line), `Advance` (linear line), `Choose(choice)` (re-checks conditions and `once`), `SetAuto(0/1)`,
`SetSkip(0/1)`. Each command is a transaction on a copy; an ID <= the last accepted one or an invalid command emits
`CommandRejected` (extra 1 for duplicates) and changes nothing. Effects of a choice apply before its next line's onEnter.

**Cues.** `CueRequest{sequence, cueId, nodeId, choiceId, sceneId, speaker, barkId, consequenceId, tick, delayTicks, source}`
with `source` NodeEnter / Choice / Consequence / Bark. Order inside a tick: consequence cues, then per command the choice
cues before the next line's enter cues, then bark cues. `sequence` is persistent and strictly increasing; the cinematic owner
(playback, timing, skipping, QTE) deduplicates by it. This package never plays or waits for a cue.

**Barks.** For each `BarkRequest{id, trigger, speakers}` the candidates are barks of that trigger whose speaker is listed
(empty = any) and not already used by an earlier request this tick, not `once`-spent, past their own cooldown and the
speaker cooldown (`speakerCooldownTicks`), with passing conditions. Highest priority wins, then never-spoken, then least
recently spoken, then the smaller ID. Output `BarkLine{request, bark, speaker, textKey}` + `BarkSpoken`, or `BarkNone`.

**Visual novel.** Every entered line is appended to `state.backlog` (`node, speaker, choice taken, tick, scene`, newest
`backlogLimit` kept). `state.read` counts reads across scenes; `lineWasRead` / `canSkipLine` / `lineRead` expose skip-read
detection. Skip mode advances up to `skipLinesPerTick` read linear lines per tick and stops (and turns off) at an unread
line or a choice (`SkipStopped`, extra 1 for a choice). Auto mode advances a linear line `autoTicks` (or
`autoAdvanceTicks`) after it was shown; `autoAdvanceTick` reports when.

**Settings.** Optional `narrative` section (configuration 0.3.4): `autoAdvanceTicks`, `skipLinesPerTick`, `backlogLimit`,
`barkCooldownTicks`, `speakerCooldownTicks`, `maxPendingConsequences` (beyond it `ConsequenceDropped`), `maxBarkRequests`,
`meterMinimum <= meterInitial <= meterMaximum`. Example: `examples/libraries/genre/narrative-settings.json`.

**State.** `NarrativeState` is record kind 64 (`BarkMemory` 65); kinds 64..71 are reserved for this package. The record
body starts with format 1 for future migrations. `genre::load` checks structure and canonical bytes; then call
`narrativeStateError(world, config, state)` to bind it to the definitions. `narrativeHash` hashes states and reports;
bind `narrativeWorldHash` and `narrativeConfigHash` in the save envelope. `GenreConsumer.cpp`
(`narrativeSceneConsumerExample`) runs one tick of a tiny social scene and stores `genre.narrative`, `genre.narrative.legacy`,
`genre.narrative.world`, `genre.narrative.config` and `genre.narrative.cue`.

Verified only with g++ 11 (Linux VM): header syntax checks (`-Wall -Wextra -Werror -pedantic`), `genre_tests.cpp` built by
hand (the narrative-scene tests also standalone under ASan/UBSan) covering legacy loading, relationship thresholds and clamping, knowledge-gated and once choices, scene- and
tick-delayed consequences, scene-local reset vs persistent world state, chapter/act, cue ordering and sequences, bark
priority/cooldowns/once/tie-break, backlog limits, skip-read and auto-advance, save/restore mid-scene with identical
continuation, rejected ticks/commands and invalid data paths, and the GenreConsumer chain in a standalone driver.
Not run: CMake/isolation, `aiverse_tests`, native module export, headless restore matches, clang/MSVC.

## Physical interactions (physical-interactions 0.1.0)

Headers under `genre/`: `PhysicalInteractions.h` (umbrella + `InteractionDefinitions`), `Carry.h`, `Push.h`, `Ropes.h`,
`Doors.h`, `Platforms.h`, `Breakables.h`, shared `PhysicalCommon.h` (events, `WorldCollision`, swept move) and
`InteractionConfig.h`, settings adapter `InteractionConfigured.h`, JSON `InteractionData.h`, records
`InteractionPersistence.h`. Target `AIVERSE::lib_physical_interactions` (depends on `configuration`, `aiverse_engine`,
`aiverse_world`); native OBJECT modules call `aiverse_native_use_libraries(target physical-interactions)`. Skill:
`skills/aiverse-physical-interactions`. Separate from `object-interactions` (action menus and built-in engine
interaction commands): a preview there never authorizes these mutations.

**Determinism and network model.** These policies are the authoritative gameplay state for interactions. They use
Fixed math, integer ticks and ascending-ID order, call only owner-supplied pure collision queries and keep every
outcome-affecting value in versioned records. Run them on the authority (server or lockstep peers) from validated
Commands, once per tick each; clients predict by running the same steps and roll back by restoring the records and
replaying inputs. The optional Box3D module (`docs/physics.md`) is not deterministic across builds/CPUs and is
**non-authoritative** here: it may simulate debris from `DebrisRequest` (seeded) or cosmetic motion, but nothing is
read back from it. If a game makes Box3D authoritative instead, it needs its own command/save/network integration.

**Collision.** Any object with `sweep(start,delta,half,ignore) -> InteractionHit` and
`overlapping(box,ignore) -> ascending ids` works; `WorldCollision(spatialWorld)` adapts `engine/world::SpatialWorld`
(sorted candidates, ties by smaller id). Moves use `physSweep`: the box shrunk by `skin` is swept over the move extended
by `skin` and stops so the real box touches the contact. Touching bodies can slide or separate; pressing into a
surface reports a hit without creeping; starting inside blocks. Overlap tests are strict (touching faces do not block).

**Common contract.** Each step validates config, definitions, state and input first; invalid data, dt outside (0,1]
or a repeated/backwards tick return nullopt with the state untouched. Inputs may arrive in any order; they are
processed by ascending ids. Outputs are the updated state plus `InteractionEvent{kind, object, actor, value, collider,
position}` (value = reason code, impulse/tension in milli-units, stage id, ...) and per-mechanic status lists. No
rendering, audio or animation. Units: metres, kilograms, seconds, newtons; yaw/pitch in turns (yaw 0 faces +Z).

**Carry/throw.** `stepCarry` processes flights first (ascending object), then carriers (ascending id): drop, throw
when the `charge` level falls, pickup, hold update. Pickup is refused (`PickupRefused`: TooHeavy above `maxMass`,
OutOfReach beyond `reach` from the hold anchor = feet + `holdHeight`, Busy when held/flying). The held object follows
`carryHoldPoint` (anchor + forward*`holdDistance`) at most `followSpeed` per second with a swept move ignoring itself
and the carrier; blocked and farther than `breakDistance` from the hold point -> `DroppedBlocked` (collider set).
`CarrierStatus::speedMilli = max(minSpeedMilli, 1000 - mass*speedPenaltyMilli)` is the carrier speed multiplier for
the movement owner. Throws: impulse grows linearly from `throwMinImpulse` to `throwMaxImpulse` over `chargeTicks`;
velocity = forward*impulse/mass + carrier velocity. `stepFlight` (v.y -= g*dt, swept move by v*dt) runs once per tick
from the tick after the throw; `previewThrow` runs the identical step, so `points[k]` equals the flight position k+1
ticks after the throw and the landing (`Landed`, collider/normal) matches exactly. `maxFlightTicks` ends with
`FlightExpired`. Free objects are the owner's (it reports their poses for pickup); `releaseCarried` forgets a prop.

**Push/pull.** `addPushable` registers a body. Per object the forces of all commands add up (horizontal part only);
motion needs |F| > friction*mass*g (`PushResisted` TooWeak on the rising edge; pulling a non-pullable object ->
NotPullable). Free mode: a = (|F| - resistance)/m along F, capped at `pushMaxSpeed`; without enough force kinetic
friction (friction*g) slows it; the swept move slides along walls (`PushBlocked` on the rising edge, `PushStarted` /
`PushStopped`). Grid mode: one cell along the dominant axis after a swept clearance test, linear over `gridMoveTicks`,
`PushArrived` exactly on the cell; commands during a move are ignored. Pushables never fall: the owner keeps them on
the ground. Pull = the actor gives the direction toward itself; its own collider is not ignored, so step it back first.

**Ropes.** `makeRope(def, a, b)` creates `segments+1` particles. Ends: Free, Point (world point), Body (follows the
owner pose in `RopeInput::bodies`, keeps the last pose when missing) and Load (a hanging mass simulated by the rope).
Each tick: attach/detach commands (`RopeAttached`/`RopeDetached`), gravity + damping prediction, then
`ropeIterations` Gauss-Seidel XPBD passes over the distance constraints (stretch only, `compliance` in m/N, 0 = rigid).
Tension = max segment force (lambda/dt^2); with a fixed iteration count a heavier load stretches the rope slightly
more, and more iterations converge closer to the rest length. Above `breakForce` (0 = unbreakable) the rope breaks
(`RopeBroken`, value = tension in milli-newtons) and stops simulating. `RopeStatus::pullA/pullB` is the force on a Body
end for its own controller. dt must be >= 1/1000 s. No rope-vs-world or self collision.

**Doors.** `addDoor` from `DoorDef` (closed panel box, Hinged: vertical pivot + `openAngle` in turns, Sliding:
`slide` offset). Opening takes `openTicks`, closing `closeTicks` (exact integer progress), `autoCloseTicks` after
`DoorOpened` start closing. Each tick the next pose (conservative AABB for hinged doors) is tested with `overlapping`
ignoring the door's collider: a blocked closing door reverses (`DoorReversed`, autoReverse) or holds (`DoorBlocked`),
a blocked opening door holds. Locked doors refuse opening (`DoorRefused` Locked) unless the actor's `keys` contain
`key` (then `DoorUnlocked` + opening); Lock/Unlock need the key (NoKey); one-way doors open only from `oneWay` side of
the panel's thinner axis (WrongSide); `scripted` commands bypass keys and sides. Update the door collider from
`DoorStatus::bounds` after every tick; give the door query the bodies (characters, props), not its static frame.

**Moving platforms.** `addPlatform`; constant `speed` along `waypoints` (Loop, PingPong, Once), stationary for exactly
`waits[i]` ticks after arriving at waypoint i (`PlatformArrived`/`PlatformDeparted`, value = waypoint index); the rest
of the travel in an arrival tick is discarded; `PlatformCommand` starts/stops. Translation only. The collision owner
reports `RiderContact{rider, platform}` from its ground test (the same contact that sets `LocomotionEnvironment::
grounded`); the report lists `RiderCarry{rider, platform, delta, velocity}` in ascending rider id
(`RiderBoarded`/`RiderLeft`). Owner order per tick: contacts -> `stepPlatforms` -> add `delta` to the rider (swept) ->
`stepLocomotion` with `env.grounded` -> move by the locomotion velocity -> update the platform collider. The
locomotion velocity stays relative to the platform; add `velocity` once on leaving for momentum inheritance.
Platforms never stop for obstacles; crushing is the owner's rule.

**Breakables.** `addBreakable`; `DamageInput{id, object, type, amount, source, position}` in ascending (object, id).
Scaled damage = amount * scaleMilli/1000 for the type (`defaultScaleMilli` otherwise; 0 = immune), ignored below the
type's `threshold`, repeated/older ids and broken objects (`DamageIgnored` with reason). Stages (ascending ids,
decreasing health) fire `StageReached` once when health drops to them; health 0 -> `Broken`. Each stage / the break
emits `DebrisRequest{object, stage (0 = break), kind, count, position, seed}`; seeds hash (object, stage, damage id).

**Settings.** Optional `interaction` section (configuration 0.3.3): gravity, skin, carry* (mass, reach, hold
distance/height, follow speed, break distance, speed penalty/minimum), throw impulses/charge, flight ticks, push
speed / grid ticks, rope iterations/damping (milli-units). The loader enforces throwMin <= throwMax;
`interactionSettingsErrors` adds `interactionConfigError`. Example `examples/libraries/genre/interaction-settings.json`.
Definitions: `readInteractionDefinitions` (`examples/libraries/genre/interactables.json`), errors such as
`interaction.platforms[0].waitTicks: expected one value or one per waypoint`.

**State.** Records (version 1): `CarryState` 80, `PushState` 81, `RopeState` 82, `DoorState` 83, `PlatformState` 84,
`BreakableState` 85; kinds 80..87 are reserved for this package (narrative takes 64..71, cinematic 72..79). Load checks
structure; each step re-checks the records against the definitions. `interactionHash` folds canonical bytes into the
checksum, `interactionConfigHash` / `interactionDefinitionsHash` bind the immutable data in the save envelope.
`GenreConsumer.cpp` (`physicalInteractionConsumerExample`) runs one tick of carry, door, platform and breakable steps
and stores `genre.interaction.{carry,doors,platforms,breakables}` plus hashes.

**Verification.** `tests/libraries/genre_tests.cpp` (`physicalInteractions`): throw preview points/landing equal the
simulated flight tick by tick, mid-flight restore continues bit-identically, mass/reach refusals and carry speed
penalty, drop when carried into a wall; cooperative push, friction refusal, blocked free push without creeping, grid
move/arrival/blocking and restore; hanging rope tension within 10% of the weight, less stretch with more iterations,
breaking, detach/attach to a body with pull output, restore; door key/lock/scripted rules, auto-reverse on a body,
hold without auto-reverse, one-way side, hinged bounds, restore; platform rider deltas fed through `stepLocomotion`
for 90 ticks (exact offset when idle, relative walking), ascending rider order, waits, restore; breakable thresholds,
immunity, duplicates, stages, debris seeds; invalid JSON/settings/inputs. Verified only with g++ 11 (Linux VM): header
syntax checks, the genre tests built by hand, the physical tests also under ASan/UBSan, and the full GenreConsumer
chain in a standalone driver. Not verified: CMake/isolation, `aiverse_tests`, native module export, headless restore
matches, MSVC/clang, platform runtimes, any Box3D debris presentation.

## Cinematic gameplay (cinematic-gameplay 0.1.0)

Headers `genre/CinematicTime.h` (time-scale domains), `genre/CinematicQte.h` (QTE judging),
`genre/CinematicScene.h` (definitions, state, validation), `genre/CinematicRunner.h` (`stepScene`), umbrella
`genre/Cinematic.h`, `genre/CinematicData.h` (scene JSON), `genre/CinematicConfigured.h` (settings adapters) and
`genre/CinematicPersistence.h` (save/load, hashing). Target `AIVERSE::lib_cinematic_gameplay` (depends on
`aiverse_engine`); native OBJECT modules call `aiverse_native_use_libraries(target cinematic-gameplay)`. Skill:
`skills/aiverse-cinematic-gameplay`. Composes with the animation system (clips/markers are referenced by ID and
reported back; `docs/animation-graphs.md` stays the playback owner) and with `narrative-graphs` (cue triggers).

**Determinism model.** Authoritative, saved and hashed: time-scale requests and per-domain accumulators
(`TimeState`), timeline position, cue emission bookkeeping, wait and QTE outcomes, staging status, input locks,
queued triggers and QTE options (`SceneState`). Host/presentation, never authoritative: audio and animation
playback progress, camera, navigation, teleport execution. Host facts enter only as `SceneInput` stamped with an
authoritative tick (`LineReport`, `MarkerReport`, `ParticipantPose`/`arrived`, `QteInput`), validated against the
cue's emission tick and the current tick, so replays and peers see the same inputs and outcomes. Every sync point
and staging move has a timeout in authoritative ticks (never domain steps), so a slow device, a missing report or
a frozen domain cannot stall the simulation. Integer math only (Fixed for mark positions), strictly increasing
ticks; invalid input or repeated/backwards ticks return nullopt with state untouched; stale command sequence
numbers are ignored (`CommandIgnored`). Presentation reads `SceneEvent`s; it never writes into either state.

**Time-scale domains.** `TimeConfig` lists domains (default world 1, player 2 with parent world, ui 3,
cinematic 4; custom ids allowed, parent id < child id). Scales are `TimeRatio{num,den}` with 0 <= num/den <= 16
and den dividing 720720 (all denominators 1..16 are exact); values are held in 1/720720 step units and each domain
keeps an integer accumulator, so a domain advances exactly floor(N*num/den) whole steps over N ticks (tested over
300000 ticks). Requests (`TimeRequest{tag, domain, scale, priority, duration, blend}`) are applied per domain in
(priority, id) order: `Override` replaces the running value, `Multiply` scales it (rounded down in step units when
not exact); a child domain is multiplied by its parent. `duration` is authoritative ticks from the start tick
(0 = until cancelled), so a hit-stop (scale 0) always ends. `stepTime` order at a tick: cancel ids, cancel tags,
expire, add (ids assigned in input order), evaluate, accumulate; tick gaps are integrated exactly with expiries
inside the gap. `DomainTick{steps, units, local}` per domain; at most 256 active requests (`Rejected` beyond).
Presets: `hitStop(domain, ticks)`, `slowMotion(domain, ratio, ticks)`, library `timePresets` used by `timeScale`
cues (tagged with the scene id and cancelled on skip/seek/resume/end).

**Scene timelines.** A scene advances by the steps of its time domain (`domain` 0 = one step per tick). Cues
(`dialogue`, `animation`, `camera`, `sound`, `event`, `timeScale`, `move`, `inputLock`, `inputRelease`) are sorted
by (at, track, id) and emitted in that order when the position reaches `at`. Essential cues (events by default,
always move/input) fire exactly once per scene run, also while skipping (`flags` 1); presentation cues fire once
per pass and are recorded as skipped when skipped. Sync points and QTEs ("stops") hold the timeline at their
position before cues at that position; held time is not banked. Commands (`SceneCommand{seq, kind, value,
bindings}`): `TriggerScene`, `TriggerCue`, `Pause`/`Unpause` (all deadlines shift by paused ticks), `Skip` (to the
next sync point, or the end), `SkipScene`, `Seek` (forward = skip semantics and requires `skippable`; backward
re-arms stops and presentation cues), `Abort`, `ResumeCheckpoint`. Skipping teleports movers and resolves QTEs in
between as `Skipped` without branching. Order in a tick: options, commands by seq, reports, then unless paused:
start a scene triggered on an earlier tick, staging, QTE judging, sync waits, timeline advance. Call every tick:
with tick gaps a released hold consumes the whole gap's steps.

**Sync waits.** `SyncDef{id, at, timeout, checkpoint, waits}`; waits: `lineFinished{cue}` (dialogue/animation cue
reported finished), `marker{cue, marker}` (animation marker reported, or the clip finished), `inPosition{participants}`
(empty = all; InPosition or Teleported). Waited cues must start before the sync point. A wait on a skipped cue is
satisfied. Release when all are met or at `timeout` ticks (`SyncReleased` value 1 = timed out; play continues).

**QTE.** `QteDef` kinds: `press` (window [open, close] ticks after the prompt, optional `perfect` sub-window, early
press fails), `mash` (`count` presses before close), `hold` (press before close, keep down `hold` ticks, early
release fails), `sequence` (ordered `steps`, each window from the previous success). Wrong buttons are ignored
unless `strict`. Judged on `QteInput.tick` in (prompt tick, current tick]. `QteOptions{windowScaleMilli
(difficulty), accessibleWindowMilli (extended windows, 1000 = off), mashScaleMilli, holdScaleMilli, autoSucceed}`
scale windows symmetrically around their centre (>= 1 tick, opening at >= 1) and are applied at the prompt;
`autoSucceed` resolves `AutoSuccess` at the prompt. Outcomes (Success, Perfect, Fail, AutoSuccess, Skipped) are
stored in `SceneState::results` and emitted as `QteResolved`. Branches: `onSuccess` must jump forward to a sync
point (skip semantics), `onFail` may jump back to retry; `failAborts` aborts the scene.

**Staging.** `MarkDef{id, position (milli-metres in JSON), toleranceMilli}`, `ParticipantDef{id, mark, lockInput,
timeout, fallback teleport|proceed|abort}`. On start, locked participants get `InputLocked`, participants with a
mark get `MoveToMark` and stay `Moving` until a pose within tolerance or an `arrived` report (`InPosition`), or
their timeout: `TeleportRequested` (the host teleports; counted as in position), `StagingLate`, or abort. `move`
cues re-stage mid-scene. Play starts when nobody is moving. `inputLocked(state, participant)` /
`lockedEntities(state)` gate player commands; every end path (finish, skip, abort) releases all locks. Trigger
bindings map participants to entities (events carry both).

**Checkpoints and narrative.** `checkpointBefore` emits `SnapshotRequested` value 0 on the trigger tick while the
scene is `Pending` (it starts on the next tick), checkpoint sync points emit value 1 when reached, `checkpointAfter`
value 2 on completion. After loading any save, send `ResumeCheckpoint`: the timeline rewinds to the last sync point
reached (or the start), re-arms its wait and replays presentation cues from there; essential cues never repeat.
A narrative `CueRequest{cueId,...}` maps to `cueTrigger(seq, cueId, bindings)`; a scene's `trigger` cue id is
indexed in `SceneLibrary::triggers` (`indexTriggers`, `sceneForCue`), unknown cues give `TriggerIgnored`, triggers
during a scene are queued (8). This package does not include the narrative header; MAIN bridges the event type.

**Data and settings.** `readSceneLibrary(json)` (`examples/libraries/genre/scene.json`): unknown keys, wrong types,
out-of-range integers, unsorted/duplicate IDs, unknown marks/participants/presets/domains/cues, waits on later cues,
backward `onSuccess`, timeouts of 0 and duplicate triggers throw `JsonError("cinematic.<path>: ...")` with index
paths for syntax (`cinematic.scenes[0].cues[2].kind: unknown value 'x' ...`) and id paths for references
(`cinematic.scene 10.sync 1.wait[0].cue: unknown dialogue/animation cue 9`). Optional `cinematic` settings section
(configuration 0.3.4): `hitStopTicks` (>= 1), `hitStopPriority`, `slowMotionNumerator`/`Denominator` (<= 1),
`slowMotionTicks`, `slowMotionPriority`, `qteWindowScaleMilli`, `qteAccessibleWindowMilli`, `qteMashScaleMilli`,
`qteHoldScaleMilli`, `qteAutoSucceed`; adapters `qteOptions`, `hitStopRequest`, `slowMotionRequest`,
`cinematicSettingsErrors`. Example `examples/libraries/genre/cinematic-settings.json`.

**State.** `TimeState` kind 72, `SceneState` 73, `QteProgress` 74, `ParticipantState` 75; kinds 72..79 are reserved
for this package (narrative uses 64..71). Records are canonical and validated on load; `stepScene` rejects a state
whose scene id/version, cursor or stop indices do not match the library. `cinematicHash` hashes states, reports and
definitions; bind `sceneLibraryHash(lib)` in the save envelope. `GenreConsumer.cpp`
(`libraryCinematicConsumerExample`) starts a scene from narrative cue 42 through two ticks of time domains and stores
`genre.cinematic.time`, `genre.cinematic.scene`, `genre.cinematic_hash` and `genre.cinematic.library`.

Verified only with g++ 11 (Linux VM): header syntax checks (`-Wall -Wextra -Werror -pedantic`), `genre_tests.cpp`
built by hand covering time-scale exactness over 300000 ticks, gaps, nesting, stacked/cancelled/expiring requests,
cue order with skip/seek, sync timeouts/pause/frozen domains, QTE judging per kind with difficulty and
accessibility, in-scene branches, staging fallbacks and input locks, checkpoint resume, queued narrative triggers,
save/restore mid-scene on the example library and invalid data; the cinematic tests also under ASan/UBSan, and the
GenreConsumer function in a standalone driver. Not run: CMake/isolation, `aiverse_tests`, native module export,
headless restore matches, clang/MSVC.

**Narrative cues.** `genre/NarrativeCinematicBridge.h` connects narrative-graphs to scenes. Each tick the owner passes
the narrative report's `CueRequest`s to `queueNarrativeCues(queue, cues)` and calls `releaseNarrativeCues(queue, tick,
nextCommandSeq, bindings)` once before `stepScene`. Cues become due at `tick + delayTicks` and are released as
`TriggerCue` commands ordered by (due tick, narrative sequence), using the owner's monotonic `SceneCommand` counter.
Sequences at or below the last accepted one are ignored, so re-feeding a report after a restore never starts a scene
twice. Unknown cue IDs reach the runner and produce `TriggerIgnored`. `NarrativeCueQueue` is saved as record kind 76
(within this package's 72..79).

## Boss encounters (boss-encounters 0.1.0)

Headers under `genre/`: `Telegraphs.h` (ground areas + telegraphed attacks), `StatusEffects.h`, `BossEncounter.h`,
`BossSchedule.h`, `MobBehaviour.h`, `EncounterData.h` (JSON), `EncounterPersistence.h`, `TelegraphDecals.h`
(presentation), `EncounterScenario.h` (headless reference fight). Target `AIVERSE::lib_boss_encounters` (depends on
`combat-actions` for the yaw convention and `AttackPhase`); native OBJECT modules call
`aiverse_native_use_libraries(target boss-encounters)`. Skill: `skills/aiverse-boss-encounters`. Tool:
`aiverse_encounter validate|simulate <file>`. Built for the Drone Fall port (ME.BECS `DelayedAttackSystem`,
`TelegraphSystem`, `BossAttackUtils`, `MobsHuntSystem`, `MobAvoidanceJob`, `BossRotationUtils`, personal boss
requests); generic for any top-down action game.

**Contract.** Every step is a pure decision on Fixed math and integer ticks: one call per object per strictly
increasing tick (repeated/backwards ticks and invalid input return nullopt with the state untouched), targets and
neighbours are processed in ascending id, RNG only through the passed `Random&`. The owner applies damage through
its own health owner, applies statuses (this package's `StatusActorState` or the engine's), moves bodies, spawns
bosses/adds and draws views. Presentation reads `TelegraphView` / `StatusViewEvent` and never writes back.

**Areas** (`AreaDef` on the XZ plane, `Vec2{x,z}`, yaw in turns, 0 faces +Z, +X right). `areaHits(area, pose, p, r,
reach)` tests a target circle; touching counts. Circle: `|p-o| <= R+r`. Ring: overlaps `inner..R` (a body inside
the hole is safe unless it reaches the inner edge). Cone: apex inside the body, or within `R+r` and inside the half
angle, or touching an edge segment (as Drone Fall's `AttackAreaGeometry.IntersectsCone`). Line: rectangle
`0..length` forward and `±halfWidth`; `reach` shortens it for sweeps. `areaFalloffMilli` is 0 at the centre / apex /
line start and 1000 at the edge (Ring 0).

**Telegraphs.** `TelegraphDef`: area, anchor (`Source`: at the caster plus a caster-local `offset` [right, forward],
e.g. a fist slam; `Aim`: at the aimed point clamped to `[minRange, maxRange]`, e.g. a thrown rock 7..15 m),
`windup/active/recovery` ticks, `interval` (0: one strike at release; >0: burning ground strikes every interval),
`sweep` (line charges: reach grows over the active window, hit once per target, `sweepReach` tells the owner how far
to move the caster), `trackAim` (re-aim during windup; frozen at release), `falloff` from `damage` to `damageMin`,
`oncePerTarget`, `friendlyFire` (team 0 = unaffiliated), `cancelOnSourceDeath` (windup / sweep only: a released area
still resolves, as Drone Fall's circles), `cooldown` (read by the boss brain) and `statuses`. `beginTelegraph` freezes
origin and yaw; `stepTelegraph` returns events `Started, Released, Strike, Ended, Cancelled`, the hits of this tick
(target, damage, falloff, status ids) and a `TelegraphView` (`progressMilli` = windup fill, then 1000;
`activeMilli`). The strike lands on the first active tick; a step that jumps over the active window still strikes
once. `telegraphLocksMovement` holds the attack pose through windup and recovery (not during a sweep).

**Statuses.** `StatusEffectDef{kind Slow|Root|Stun|Burn, duration, tickInterval, speedMilli, damagePerTick,
stacking Independent|Refresh|Reject, maxStacks, anchor Ground|ModelTop}`. Defaults follow Drone Fall: burn
independent (several fires at once), stun reject (no re-stun while stunned) with a ModelTop visual, slow refresh.
Burn hits `ceil(duration/interval)` times, the first one interval after application, the last on expiry. Aggregate
per tick: lowest slow, 0 when stunned or rooted, `stunned` blocks attacks. `setStatusImmunity` by kind mask or ids
(bosses: stun/push immune, burn allowed). `applyStatus` returns Applied / Refreshed / Rejected / Immune / Invalid
and a view event; `clearStatuses` ends all (death). `rtsStatusRules` writes the same definition as a `rules.json`
status (+ the effect duration) for games on the RTS simulation, where the engine statuses stay authoritative.

**Boss brain.** `BossDef{phases, enrage, archetype}`. Phase 0 starts the fight; phase i>0 starts when
`hp <= hpBelowMilli/1000 * maxHp` or at `afterTicks` encounter age, entered in order even when HP skips several.
Patterns: `ByRange` (first entry whose `[minRange,maxRange]` band holds the target distance — list the closer bands
first, as cone <= 3 m / slam <= 7 m / rock <= 15 m), `Sequence` (cursor, waits for its entry), `Weighted` (one rng
draw when two or more entries are ready). `alternate` swaps every second use (left/right fists) and shares the
cooldown key. The owner reports `busy` while the attack runs; the attack's own cooldown and the phase's shared
`globalCooldown` start when it ends (Drone Fall: full animation, then 1 s). `transitionTicks` pause attacks after a
phase change. Multipliers (`damageMilli`, `speedMilli`, `cooldownMilli`) stack with enrage (`afterTicks` or
`hpBelowMilli`); `bossDamage(step, damage)` scales a hit. Add waves (`unit, count, maxAlive, every, radii`) emit
`SpawnAdds` on phase entry and every `every` ticks, capped by the living adds the owner reports.

**Schedule.** `ScheduleDef{windows, personal, rotation, archetypes}`; a window `{start, end, breather, variants,
excludePrevious, pauseWaves}`. When a window opens one variant is chosen for the whole map (weighted; rotation shows
every archetype once before repeating and never the last one twice, as `BossRotationUtils`; persist `BossRotation`
in the profile). Personal mode creates one request per participant (one success each, no respawn after an early
kill), shared mode one request. `Pending` events repeat until `markBossSpawned`; requests not placed by `end` expire.
`markBossDefeated` (kills only) starts the breather; `cancelBossRequests` (left / removed) is never a win.
`ordinaryWavesAllowed` is false while an own boss is pending or alive (pauseWaves) and during the breather;
`bossesDefeated` counts personal wins (trophies).

**Mob behaviour.** `stepMob(config, state, tick, self, candidates, neighbours, rng, walkable)`. Order: leash return
home (targets ignored until back), flee at `hpMilli <= fleeBelowMilli` from the nearest hostile within `fleeRange`
until `fleeRecoverMilli`, the timer or no threat; hunt the committed target (attack in progress) > `priority` target
(personal boss -> its hero) > current target within `loseRange` unless another is closer by `switchMargin` >
nearest within `aggroRange` (ties by id); stop at `stopDistance`; `movementLocked` keeps the target with a zero
direction. Otherwise wander: a point in the `[wanderMin, wanderMax]` ring around `anchor` that `walkable` accepts
(up to `wanderAttempts`, two `rng.unit()` draws each), leg ends on arrival, overshoot or `wanderLegTicks`, then
`wanderDelay`. Swarm pressure (`swarmCorrection`) is Drone Fall's crowd separation + look-ahead side-step: nearest
`maxNeighbours`, spacing `(r1+r2)*spacingMilli`, coincident pairs split by id without RNG, correction capped at
`correctionMilli` of the desired speed, final direction length <= 1. `MobDecision{mode, target, desired, goal,
approach}`: steer with `desired`, or issue a flow-field / navmesh move to `goal`.

**Data.** `readEncounterData` reads `{"ticksPerSecond", "statuses", "attacks", "bosses", "schedule", "mob"}` with
integer milli units (`radiusMilli`, `halfAngleMilli` in milli-turns, `offsetMilli: [right, forward]`) and tick
times; unknown keys, wrong shape keys, unknown status/attack/boss ids and inverted ranges fail with a path
(`attack 1.halfAngleMilli: required for this shape`). Example: `examples/libraries/genre/boss-encounter.json` — a
three-phase giant (cone + stun, alternating fist slams + slow with falloff, tracked rock + burn, sweeping charge,
ring shockwave, add waves, enrage at 150 s), a weighted boss with burning ground and a sequence boss with a timed
phase, three personal boss windows at 3:00 / 6:00 / 9:00 with rotation. Bind `encounterDataHash` in saves.

**Views.** `telegraphDecal(view, groundY, own, colors)` returns x, y, z, rgba triangles: background shape plus a fill
growing with `progressMilli` (circle / ring outward, cone from the apex, line from its start), own vs enemy colours
as Drone Fall's `TelegraphColorConfig`. Feed them to `RtsGroundCanvas` (`ViewGroundBatch`) or a Lua
`view.draw_ground` list. Status visuals: one view per `StatusViewEvent` serial (Started / Refreshed / Ended) at the
Ground or ModelTop anchor.

**State and checks.** Record kinds 112..119 (112 TelegraphInstance, 113 StatusActorState, 114 BossState,
115 ScheduleState, 116 MobState, 117 BossRotation); `encounterHash` folds canonical bytes into checksums.
`tests/libraries/encounter_tests.cpp` (`library_encounters`) covers boundary hit tests of every shape, strike ticks,
late release, cancel, aim clamp/tracking, periodic and sweep strikes, stacking/immunity/burn timing, phase entry by
HP and time, alternation, cooldowns from the attack end, transitions, enrage, add caps, weighted determinism, rotation
order, personal requests / breathers / expiry, every mob mode and swarm splitting, data errors, save/load of every
state and the reference fight. `aiverse_encounter simulate examples/libraries/genre/boss-encounter.json` runs
it: with the default 4-tick reaction the hero beats the giant through all three phases (tick 1897, 15 dodged
telegraphs); with `--reaction 100000` (no dodging) the hero dies — the telegraphs decide the fight.

**Not covered.** Projectiles in flight (weapon-policies), knockback/push physics, navigation, loot and rewards,
HUD banners, network authority, and wiring into RtsModule units (RTS games use the engine's statuses / areas).
