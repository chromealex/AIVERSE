# Ordered projectile programs

RTS weapons, projectile ability/effect deliveries and FPS weapons can opt into `flight`. Without it,
legacy movement, collisions and checksum streams are unchanged. No game-specific IDs or code
are required. See `examples/projectiles` for an executable data-only example.

```json
{
  "id": "needle",
  "range": 20, "damage": 10, "cooldown": 1, "projectileSpeed": 20,
  "flight": {
    "radius": 0.1, "maxDistance": 30, "lifetime": 10,
    "homing": false, "blockTerrain": true, "splitBudget": 64,
    "modifiers": [
      {"kind": "flyThrough", "count": 2, "falloff": 0.8},
      {"kind": "split", "trigger": "hit", "count": 3, "angle": 30},
      {"kind": "damage", "factor": 0.5},
      {"kind": "ricochet", "count": 2, "radius": 5}
    ]
  }
}
```

For an ability, put the same `flight` object inside
`"delivery": {"type":"projectile", "speed":20, "flight":{...}}`.
A nested `{"type":"projectile", "speed":20, "flight":{...}, "effects":[...]}` effect also works.
Damage and on-hit payloads use the existing combat/effect owners. `flight.homing` is the
steering and initial target-capture setting for this path. A point-target projectile has no
homing target until ricochet.

## Ordering and inheritance

This is an ordered program, not an unordered set of flags. At birth it bakes scalar modifiers
and enables Fly Through until the first Split or Ricochet gate. Later entries wait for that
gate. Each scalar is applied once, in authored order, with normal Fixed rounding.

- **Split:** on the first direct hit, or at `trigger: "time", after: seconds`, create `count`
  children in a symmetric fan around local up of total width `angle` degrees. Count excludes the source.
  The source keeps its existing prefix, age and ordinary collision/penetration behavior;
  it does not receive the suffix. Children inherit consumed gates, remaining counts, damage
  scale and direct-hit history, reset their local age and activate the suffix. They continue
  from the split position, never from the launch point. They do not home back to the old target.
  A source without an earlier Fly Through ends on contact as usual.
- **Ricochet:** choose the nearest eligible, previously unhit unit in `radius` from the contact,
  with entity-index tie-breaking. Flat straight RTS and FPS candidate selection rejects occluded targets when terrain blocking is enabled.
  Curved RTS shots select by surface distance; their actual flight sweeps decide terrain impact. Create one replacement, consume one count, preserve the prefix, activate the suffix
  once, then remove the source. Remaining bounces stay active in the prefix; later bounces do
  not prematurely activate another pending gate. This is target-to-target redirection, not a
  surface reflection. If no target exists, retire that gate and try the suffix on the same hit.
- **Fly Through:** `count` is the number of direct hits the projectile can pass through; the next
  hit terminates it. Apply `falloff` after each continued hit. Multiple active entries are consumed
  in declaration order, one per hit. All direct hits in a tick are swept, ordered and processed;
  each packed entity ID can be directly hit only once per projectile lineage.
- **damage / speed / radius:** multiply the respective value by `factor`. Damage scales weapon
  damage/onHit or the existing ability payload scale. The hit that activates a gate uses the
  pre-gate scale; the suffix affects its children/replacement or subsequent source hits after
  failed Ricochet. Speed and radius changes are reflected in the next sweep immediately.

No contact or timer consumes random numbers. Children/replacements first move on the next tick.
Split inherits remaining distance and lifetime; Ricochet does not reset either or its local age.
Consequently a chain cannot replenish its range or keep extending its lifetime. A new Split
child's `after` is relative to its own birth; a gate activated late may already be due.
Splash/payload area damage retains its existing semantics; the history tracks **direct contacts**,
not arbitrary targets affected by nested payloads. A target can therefore receive splash and a
later direct hit. Split children may each hit the same new target, independently.

## Collision and limits

The chain kernel (`engine/sim/ProjectileProgram.h`) is shared by RTS and FPS. Each host supplies
its collision, targeting and damage pipeline. `flight.gravity` accelerates down local up;
`flight.verticalSpeed` adds launch velocity along up. Split rotates the current heading around
up and preserves vertical velocity. Ricochet changes the heading and keeps vertical velocity,
gravity, remaining range and lifetime. It does not restart an arc. Speed modifiers scale the
heading component; gravity and accumulated vertical velocity are independent.

### RTS trajectories and planets

Existing flat straight flight uses swept XZ disks and type radii. Ballistic flight uses swept
3D spheres; target centres are at their surface position + 0.6, with radius at least 0.6.
`delivery.arc:true` is supported with `flight`: `arcHeight` (0..128) derives initial vertical
speed and gravity from launch distance and speed. It returns to launch height at the original
aim distance; after branching the same gravity continues. The minimum fitted flight time is
0.1 seconds. `arc` and explicit nonzero flight gravity/verticalSpeed are mutually exclusive.

On planets, horizontal movement follows the local shell; headings are transported into the
new tangent plane, including cube-face seams. Ballistic altitude follows the same vertical
integration with radial up. Collision checks the body's terrain height and solid cells; target
search excludes other bodies. Planet-to-planet travel remains the transit mechanic.

Curves use bounded swept chords (at most 0.25 world units and 1/120 second; planets also limit
steps by cell size). This is deterministic fixed-step approximation, not analytic curved mesh
intersection. Flat solid terrain/diggable rock uses radius-expanded cell boxes, conservative
at corners; void cells remain blocking columns. Heightfields and planet surfaces stop a shot
when it meets the ground. Buildings use unit collision, not duplicate pathfinding obstacles.
A wall wins an exact distance tie. `blockTerrain:false` bypasses terrain while retaining limits.
Entities spawned/teleported by payloads enter the RTS spatial hash on its next normal rebuild.

### FPS

Put the same `flight` block in `fps.weapon` or an entry of `fps.weapons`, alongside positive
`bulletSpeed`. It works with or without `mechanics.enabled`. Missing `flight.gravity` inherits
`bulletGravity`; explicit zero disables gravity. Pitch is preserved, including vertical fire.
The range is the smaller of weapon `range` and flight `maxDistance`.

FPS sweeps expanded actor/vehicle bounds and uses `SpatialWorld` box/triangle collision. Damage
uses existing armor, hit-region, knockdown and kill handling. Material penetration remains
independent of Fly Through; it consumes the existing penetration budget. Ricochet skips allies
when friendly fire is disabled and breaks ties by actor/vehicle identity. Hit history includes
actor life generation, so a respawn is a new target. A host cap of 256 live/queued projectiles
can suppress further branches. Ammo is charged only for the original shot.

Authoritative snapshots retain their existing position/velocity wire layout; clients display
branches through the existing projectile renderer. The server keeps chain state and history.

```json
{"bulletSpeed":40,"bulletGravity":9,"flight":{"verticalSpeed":3,
  "modifiers":[{"kind":"split","trigger":"time","after":0.2,"count":3,"angle":30},
               {"kind":"ricochet","count":2,"radius":8},
               {"kind":"flyThrough","count":2,"falloff":0.8}]}}
```

| Setting | Default | Accepted range |
|---|---:|---|
| radius | 0.1 | 0..4 world units |
| maxDistance | 30 | 0.01..4096 world units |
| lifetime | 10 | 0.01..120 seconds |
| gravity / verticalSpeed | 0 / 0 (FPS gravity inherits bulletGravity) | 0..1000 / -1024..1024 |
| splitBudget | 64 | 0..256 descendants |
| modifier entries | empty | at most 16 |
| split count / angle | 2 / 30 | 2..8 / 0..360 degrees |
| timed split after | 0 | 0..120 seconds |
| ricochet count / radius | 1 / 5 | 1..32 / 0.01..128 units |
| flyThrough count / falloff | 1 / 1 | 1..32 / 0..1 |
| scalar factor | 1 | 0..4; speed 0.01..4 |

A split atomically refuses to branch if its available budget is smaller than its count; the
source consumes that gate. After a successful split, the source retains zero split budget and
the remaining budget is partitioned among children (earlier children receive the remainder).
This bounds the entire split tree, not only each generation. Hit history is capped at 256.
Effective speed/radius/damage multiplier saturate at 2000 / 4 / 64; speed has a 0.01 floor.
Definitions reject unknown fields, malformed types, fractional counts and out-of-range values.

## Persistence and verification

All active program fields, counters, consumed gates, history and projectile weapon identity are
serialized. Program state and definitions contribute to the checksum; loading an active flight
under changed flight settings fails checksum verification. New saves use version 6 (FPS module state version 6). Version 5 RTS saves retain their straight-program defaults. Version 4 saves remain readable with legacy rules and their original
projectile defaults; loading one into rules that enable `flight` is explicitly rejected because
the old save has no program state. Old JSON games continue to load and preserve their baseline
simulation checksums.

`aiverse_tests projectile_program` exercises real ability/weapon commands, sorted swept contacts,
walls, prefix/suffix semantics, ricochet target ordering, timed split budgets, filters, range/life,
restore after branching and checksum sensitivity. Run the full suite and sample matches after
simulation changes. The demo can be launched with `AIVERSE_CONTENT=examples/projectiles` and
`AiverseGame --map 0 --spectate`; new projectiles use the existing projectile rendering.

### Initial flat-map implementation verification (2026-11-12)

- Release builds of tests, headless and the Metal client completed without compiler warnings.
- Full engine suite: 9029 checks, zero failures, including actual v4 binary fixture loading,
  deep-copy rollback, serialized restore and coincident ricochet targets.
- All 81 RTS baseline runs matched their stored checksums after normalizing checkout CRLF.
  Existing time-limit results remain (clanlands seed 2 and fourages seeds 1..3).
- Four FPS reference templates completed 30-second smoke runs; these were not winner tests.
- Ten standalone library suites, catalog/generator checks, genre policies and external native /
  relocated source consumers passed.
- The example passed snapshot, serialized and fresh serialized restoration, 200 ticks each.
  Its 100-frame Metal screenshot was inspected; all three split branches render through the
  existing projectile renderer. Direct source-content launch lacked the standard HUD assets;
  this check covers the battlefield, not a complete game UI.
- Windows, Android, iOS and Web runtime/builds were not tested. No shaders were changed.

The extended suite adds ballistic overflight/landing, inherited vertical velocity, planet seam
contacts, body isolation, FPS height-sensitive hits, world blocking and post-split save continuation.
`examples/projectiles` includes straight, ballistic and planetary maps; FPS configuration above
can be used in any FPS project without RTS dependencies.

### Universal trajectories verification (2026-11-12)

- macOS Release: engine tests, headless, RTS client and FPS client built without compiler warnings.
- Full suite: 9284 checks, zero failures; 407 focused projectile checks included.
- FPS combat/save/authority tests passed, including the 100-client loopback scenario. Async
  collision streaming tests passed. Four FPS templates passed 30-second smoke runs.
- All 81 RTS baseline runs matched their stored checksums. Existing timeout/draw outcomes
  remain; this does not claim every scenario gained a winner.
- Ballistic and planet demos passed snapshot, serialized and fresh serialized restore,
  200 per-tick comparisons each. Both were inspected in Metal screenshots. The source-content
  demo lacks standard HUD files, so screenshots verify the battlefield and projectile branches.
- Skill catalog/routing validation and external native + relocated source consumers passed.
- Windows, Linux, Android, iOS and Web builds/runtimes were not verified in this change.

FPS state version 6 extends the version 3 layout; version 3 saves remain readable with default projectile-program state.
