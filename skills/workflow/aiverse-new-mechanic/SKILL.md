---
name: aiverse-new-mechanic
description: Implement any AIVERSE mechanic as composable genre-independent ECS components + feature systems, with optional adapters for existing RTS/FPS modules and cross-genre proof.
---

# New engine mechanic — component + system composition

**Mandatory:** read `AGENTS.md` (universal ECS mechanic contract) and
`docs/ecs-mechanic-composition.md` before writing feature code. These rules
supersede the old RTS-only instructions.

1. **Coordinate branches and handoff.** Find the current integration branch
   and existing agent work. Never rewrite another agent's files without
   reconciling the source. Update the active handoff immediately after each
   implementation step, with commit SHA, changed paths, test result, and what
   remains. Do not merge to `main` without explicit approval and green gates.
2. **Reuse first.** Search `libraries/catalog.json`, public
   `aiverselib` headers, `engine/sim`, `engine/nav`, skills, docs and
   CTest. Refactor existing reusable behavior instead of cloning FPS/RTS
   algorithms or making parallel incompatible configurations.
3. **Define ECS components.** For each feature, define *data-only*
   authoritative, saveable components for state, ownership, settings and
   other necessary capabilities. Use Fixed/integer math, generation-safe
   Entity references, one source of truth, explicit version handling and
   settings validation. A mechanic does not live in a monolithic
   `State::actors`, `GameCore` field, global singleton or render object.
4. **Implement reusable systems.** Each system provides one feature/stage
   with explicit component/resource Read/Write access and a deterministic
   execution order. Missing components mean the Entity does not participate.
   Use a deterministic deferred command/event barrier for creation,
   destruction, damage, shared RNG and other order-dependent side effects.
   The core system must not branch on genre names or require
   `game::GameCore&` / `fps::Module&`.
5. **Separate shared policy from adapters.** Put neutral calculations in
   `libraries/include/aiverselib/` or shared `engine/`. Genre modules
   register systems, map existing input/config, preserve external save/network
   contracts and provide presentation only. Prefer configuration/policies
   over RTS-specific vs FPS-specific implementations.
6. **Projectile example (mandatory pattern elsewhere).** Shooting is
   assembled from `WeaponLaunch`, `ProjectileFlight`,
   `Collision/Impact`, `Damage`, `Effects/Despawn` components/systems.
   A rifle bullet, artillery shell, homing rocket or spell uses one
   calculation library plus settings, not duplicate RTS/FPS math. Start
   with `engine/sim/ProjectileData.h` and `engine/mechanics/Weapons.h`.
   Apply the same decomposition to vehicles, crafting, loot, economy, AI,
   inventory, interactions and other features.
7. **Compose in native C++ games.** Expose through public
   `AIVERSE::libraries`/`AIVERSE::gameplay_native` targets; a project's
   native code registers whatever components and systems it needs, without
   changing engine sources or requiring a specific genre module.
8. **Prove reuse.** Test the *same* library/system in at least two different
   GameModule hosts, plus a combined-mechanics example; register CTest and
   a standalone native consumer. Add invalid configuration, lifecycle,
   actor/entity generation, deterministic event ordering, snapshot/rollback,
   serialisation/replay and cross-worker checksum tests. Existing
   `tests/ecs_cross_genre_mechanics_test.cpp` is the pattern.
9. **Expose in Editor and data.** Add generic component metadata,
   inspector editing of authored state, hierarchy/selection and appropriate
   UI or presentation hooks. No genre-hardcoded editor rendering if the
   components already describe the feature. Keep old content loading with
   stable defaults and errors that identify invalid fields.
10. **Run actual verification.** Build/test on supported platforms, normal
    and sanitized ECS suites, FPS/RTS replay parity, native project consumer,
    affected templates and headless matches. For graphics, validate both
    GLSL and Metal and check visual captures. Unrun checks must be labeled
    unverified; never assert production readiness from code review alone.
11. **Deliver and document.** Update this skill if needed, the public API
    guide, `docs/ecs-mechanic-composition.md`, and handoff. State exactly
    what is implemented vs still a legacy adapter, what passed, and which
    branch/commit contains the change.

The engine is an assembly of mechanic pieces. **The genre is a preset,
not a code ownership or feature eligibility boundary.**
