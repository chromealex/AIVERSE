---
name: aiverse-projectile-programs
description: Give AIVERSE RTS weapons, projectile abilities and FPS weapons an ordered `flight` program - fly-through, split, ricochet, damage scaling, gravity arcs, homing - from data only, deterministic on flat maps and planets.
---

# Projectile programs (`flight`)

Use when shots need Path-of-Exile / roguelite style behaviour (pierce, fork, chain, arc). Plain weapons need no
`flight`; without it movement, collisions and checksums are unchanged.

Authority: `docs/projectile-programs.md`. Kernel shared by RTS and FPS: `engine/sim/ProjectileProgram.h`.
Data-only example: `examples/projectiles` (straight, ballistic and planetary maps).

1. Put `flight` on an RTS weapon, inside an ability `delivery` of type projectile, inside a nested projectile
   effect, or in `actor.weapon` / an `actor.weapons` entry (FPS: `docs/fps.md`).
2. `modifiers` are an ordered program, not flags: scalars bake at birth; split / ricochet are gates and later
   entries wait for them. Bound fan-out with `splitBudget`; `blockTerrain: false` ignores terrain.
3. Damage and on-hit payloads stay with the existing combat / effect owners - do not duplicate damage code.
4. Check: `aiverse_tests projectile_program`, `rules_lint`, `scenario_run` with the weapon, and the demo with
   `AIVERSE_CONTENT=examples/projectiles`; template checksums must not change.

Workflow recipe over existing engine code; no runtime library.
