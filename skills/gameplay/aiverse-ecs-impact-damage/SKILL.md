---
name: aiverse-ecs-impact-damage
description: Reuse one ECS projectile impact and deterministic damage-resolution pipeline across RTS, FPS, racing, RPG, turrets, spells and custom native projects.
---

# ECS impact and damage systems

Read `AGENTS.md`, `docs/ecs-mechanic-composition.md`,
`skills/gameplay/aiverse-ecs-projectile-systems/SKILL.md`, and
`docs/editor-unification-handoff.md` before editing shared mechanics.

1. **Components, not genre names.** Include `"engine/mechanics/EcsDamageSystems.h"`.
   Use `HitPointsComponent` for an entity's health,
   `ProjectileDamageComponent` for shot damage,
   `ColliderEntityIndex` to map collision IDs to generation-safe entities,
   and `DamageQueue` for deterministic staged effects.
2. **Compose separately registered systems.** Attach
   `addProjectileFlightSystem(sim, ... , onImpact)` with
   `onImpact=enqueueProjectileDamage`, then
   `addDamageResolutionSystem(sim, ... , onApplied, policy)`.
   Register flight before damage. Neither system requires
   `game::GameCore` or `fps::Module`.
3. **No parallel side effects.** Projectile traversal produces a canonical
   ordered event list; the damage queue is swapped and consumed at the
   explicit resolution barrier. A callback's newly queued damage is
   processed at the next barrier. Leave creation and destruction deferred
   until after relevant traversals.
4. **Project-specific customization through policy.** `DirectHitDamagePolicy`
   applies damage directly. For armor, teams, shields, immunities or PvP,
   provide a deterministic policy callable as
   `int32_t(Simulation&, const DamageRequest&, const HitPointsComponent&)`.
   Do not duplicate projectile flight or impact ordering.
5. **Save/rollback.** All mutable health, projectile and damage queue state
   lives in ECS components/resources. The public `saveDamageQueue`,
   `loadDamageQueue`, and `hashDamageQueue` helpers provide an explicit
   bounded, versioned (v1) queue byte format, transactional loading and
   generation-safe checksum. A GameModule still must include this resource
   in its own serialize/hash hooks: World COW snapshots alone are not
   equivalent to serialized save-state support. Serialize custom policy
   state as components; avoid mutable policy capture or wall-clock timing.
   Use `sim.rng()` in a canonical ordered stage for random outcomes.
6. **Native access.** Link `AIVERSE::lib_ecs_impact_damage` in a native
   project. This transitively selects the public projectile and collision
   libraries; include the package header without changing engine modules.
7. **Cross-genre proof.** Run `ecs_cross_genre_projectile_test`, which uses
   the same flight/damage pipeline in shooter and artillery GameModules,
   compares checksums and rollback, and tests optional COW.
   Also run `ecs_projectile_composition_test` for flight-only behavior.
8. **Migration honesty.** Existing RTS and FPS combat implementations are not
   automatically redirected by installing this package. Integrate one
   feature stage at a time; compare gameplay outputs and legacy save/net
   behavior before removing their adapters.

This is the standard pattern for **every** AIVERSE mechanic:
data-only components + independently registered systems + public, pure
policies + capability-based configuration + deterministic events.


## Actual FPS integer HP adoption (2026-11-18)

Both the generic `addDamageResolutionSystem` and the built-in
`fps::Module::damage` now use the same immediate, deterministic
`applyIntegerHitPointDamage(int32_t& current,int32_t incoming)` function.
This helper applies positive already-mitigated damage, clamps at zero and
returns the amount actually subtracted without overflowing on large values.
**Do not** call it before applying the host's armor/gear/knockdown policies.

In FPS, live health is the `CharacterHealth` component of the character
entity (the FPS host is ECS-only; there is no Actor vector or mirror).
The helper does **not** mean that separate `HitPointsComponent` owns
live FPS health or that `DamageQueue` replaces synchronous FPS damage.
Do not add a second writable health owner until an explicit save/version/
checksum/network migration is proven.

Validation: `ecs_integer_health_commit_test` covers old FPS arithmetic for
all 0..100 HP and 0..1000 incoming damage plus overflow cases.
Also run `aiverse_fps_tests`, including ECS ON/OFF actor/damage parity
and v9 save/rollback, before accepting the cutover. Native execution
is not yet verified.
