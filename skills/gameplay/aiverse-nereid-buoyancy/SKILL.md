---
name: aiverse-nereid-buoyancy
description: Shared fixed-point buoyancy, drag and host-owned integration, reusable in any native AIVERSE project.
---

# NEREID — Shared fixed-point buoyancy, drag and host-owned integration

Read `AGENTS.md`, `docs/nereid/ARCHITECTURE.md`, `docs/nereid/HANDOFF.md` and the common `skills/gameplay/aiverse-nereid-water/SKILL.md` first.

## Package and native API

Use `target_link_libraries(my_native_game PRIVATE AIVERSE::lib_nereid_buoyancy)` in CMake, or `aiverse_native_use_libraries(aiverse_native_GAME nereid-buoyancy)` when using the native module helper. The shared water dependency is `AIVERSE::lib_nereid_water`. Include `"engine/world/nereid/NereidBuoyancySystems.h"`; namespace `aiverse::lib::nereid`.

Attach WaterBuoyancyComponent, WaterVelocityInputComponent and WaterForceComponent to the same water-contact entity. Run water sampling first; addWaterBuoyancySystem computes deterministic force output. Register registerNereidBuoyancyPersistence during native setup. Physics integration remains owned by the host.

## Engineering contract

All simulation inputs and outputs are deterministic Fixed/ECS state. These systems are genre-independent: do not add FPS, RTS, PHYSIS, Box3D or visual renderer dependencies. Do not change the authoritative `WaterBodyComponent.flow` to implement presentation effects. Use `docs/nereid/HANDOFF.md` for current limitations, implemented APIs, exact commits, known tests and next agent steps.

Source: `engine/world/nereid/NereidBuoyancySystems.h`. Sphere seas (planets) use the same
components with `"engine/world/nereid/NereidSphereWater.h"`: `addSphereWaterBuoyancySystem` after the plane one gives
radial buoyancy to entities with a `WaterSphereContactComponent`.
Status: experimental; native performance, platform tests and renderer parity must be verified before production use.
