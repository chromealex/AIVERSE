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
| match-rules | Warmup/live/overtime/intermission/finished, team score events, best-of rounds, draws and capture points | Native extension policies; existing built-in victory modes stay owned by their modules. No bomb/payload/extraction implementation implied. |
| narrative-graphs | JSON/localization-key graphs, conditional choices, fact set/add transactions, visits, command deduplication and endings | Project UI/cinematics consume committed nodes. No automatic scene playback, audio, QTE or online reward grant. |

## Native/host integration

Include `<aiverselib/genre/Configured.h>`, the needed policy header and `genre/Persistence.h`.
Executable hosts link `AIVERSE::genre`; registered OBJECT modules call
`aiverse_native_use_genre(aiverse_native_<id>)` after `aiverse_native_module(...)`. The helper puts collision
backend dependencies on the owning aiverse_game target, avoiding object-library linkage cycles.
For header-only policies without projectile collision, `AIVERSE::gameplay_native` is sufficient.

See `examples/native/libraries/GenreConsumer.cpp` for validated settings and NativeState storage.
The library_demo native module now initializes these records in setup (module VERSION 4: locomotion modes, combat-actions and weapon records added).
`tests/libraries/genre_tests.cpp` runs trajectories, combos, world collision, round transitions,
choice transactions and restoration checks. No engine source edit is required in a consuming project.

External input must arrive as validated Commands. Use these helpers inside the owning authoritative
system/handler, not from presentation. Keep immutable definitions separately from dynamic state.
Use integer ticks and Fixed dt. Save every outcome-affecting state and do not update the same controller
twice per tick. Motion, melee and match policies reject repeated/backwards ticks. Recoil/projectile
steppers are per-step primitives; their owner is responsible for exactly one update per simulation tick.

## Configuration

Scalar defaults are in `libraries/spec/gameplay.json` (configuration package 0.3.2): sections
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
