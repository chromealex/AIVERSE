# Cross-genre C++ gameplay packages: first batch

These are reusable deterministic policies for project-native/host code. They compose with engine
mechanics; they do not silently replace existing FPS movement, RTS abilities/damage or match rules.

| Package | Implemented API | Boundaries |
|---|---|---|
| character-locomotion | MotionConfig/State/Input, stepMotion: ground/air speed, acceleration/braking, gravity, jump buffer, coyote time, bounded extra jumps, ceiling response | Returns velocity; collision owner reports grounded/ceiling and applies movement. No climbing, swimming, mantle or animation controller yet. |
| melee-combat | AttackSet/State, phase evaluation, input buffering, allowed combo cancels, authorization callback, once-per-swing hit bookkeeping, evade/parry/block outcome | Geometry/target validation and actual damage/stamina belong to the existing combat owner. No parallel health/status system. |
| combat-actions | Frame-keyed hitboxes + hurtboxes (sphere/capsule/yaw box) with overlap tests, self/team filtering and once-per-swing hits; dodge i-frames + cooldown; directional guard arc + perfect-parry window with re-arm; poise damage/break/recovery and hit/block/parry stun; deterministic lock-on acquire/switch/break | Composes melee-combat. Returns contacts/outcomes; damage, stamina, health and statuses stay with the existing combat owner. No per-tick sweep, animation-driven boxes, ragdoll or camera presentation. |
| weapon-policies | Deterministic recoil recovery, seeded spread, swept projectiles with gravity, lifetime and bounded ricochets | Existing FPS ammo/reload/fire cadence remain authoritative. No penetration/material model, weapon attachments or automatic replacement of hitscan. |
| match-rules | Warmup/live/overtime/intermission/finished, team score events, best-of rounds, draws and capture points | Native extension policies; existing built-in victory modes stay owned by their modules. No bomb/payload/extraction implementation implied. |
| narrative-graphs | JSON/localization-key graphs, conditional choices, fact set/add transactions, visits, command deduplication and endings | Project UI/cinematics consume committed nodes. No automatic scene playback, audio, QTE or online reward grant. |

## Native/host integration

Include `<aiverselib/genre/Configured.h>`, the needed policy header and `genre/Persistence.h`.
Executable hosts link `AIVERSE::genre`; registered OBJECT modules call
`aiverse_native_use_genre(aiverse_native_<id>)` after `aiverse_native_module(...)`. The helper puts collision
backend dependencies on the owning aiverse_game target, avoiding object-library linkage cycles.
For header-only policies without projectile collision, `AIVERSE::gameplay_native` is sufficient.

See `examples/native/libraries/GenreConsumer.cpp` for validated settings and NativeState storage.
The library_demo native module now initializes these records in setup (module VERSION 3).
`tests/libraries/genre_tests.cpp` runs trajectories, combos, world collision, round transitions,
choice transactions and restoration checks. No engine source edit is required in a consuming project.

External input must arrive as validated Commands. Use these helpers inside the owning authoritative
system/handler, not from presentation. Keep immutable definitions separately from dynamic state.
Use integer ticks and Fixed dt. Save every outcome-affecting state and do not update the same controller
twice per tick. Motion, melee and match policies reject repeated/backwards ticks. Recoil/projectile
steppers are per-step primitives; their owner is responsible for exactly one update per simulation tick.

## Configuration

Scalar defaults are in `libraries/spec/gameplay.json` (configuration package 0.3.0): sections
`locomotion`, `melee`, `projectile`, `matchRules`. Run generate_gameplay.py after spec changes.
Load through `readGameplaySettings`, then `motionConfig`, `attackConfig`, `projectileConfig`,
`matchConfig`. Metres/seconds use integer milli-units in JSON; C++ converts directly to Fixed.
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
