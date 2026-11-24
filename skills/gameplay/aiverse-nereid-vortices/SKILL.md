---
name: aiverse-nereid-vortices
description: Bounded fixed-point eddy/vortex flow for generic water, reusable in any native AIVERSE project.
---

# NEREID — Bounded fixed-point eddy/vortex flow for generic water

Read `AGENTS.md`, `docs/nereid/ARCHITECTURE.md`, `docs/nereid/HANDOFF.md` and the common `skills/gameplay/aiverse-nereid-water/SKILL.md` first.

## Package and native API

Use `target_link_libraries(my_native_game PRIVATE AIVERSE::lib_nereid_vortices)` in CMake, or `aiverse_native_use_libraries(aiverse_native_GAME nereid-vortices)` when using the native module helper. The shared water dependency is `AIVERSE::lib_nereid_water`. Include `"engine/world/nereid/NereidVortices.h"`; namespace `aiverse::lib::nereid`.

Attach WaterVortexFieldComponent (up to 16 WaterVortex entries) to a water entity. waterCurrentWithVortices composes river current plus local circulation/radial components deterministically. The persistence helper `registerNereidVortexPersistence(sim)` is declared in `"engine/world/nereid/NereidWaterSystems.h"`, so include that header too when registering manually. If you use `registerNativeWaterPipeline` with `enableVortices=true`, do not register the same component twice. Never write the visual eddy offset back into physics or WaterBody.flow.

## Engineering contract

All simulation inputs and outputs are deterministic Fixed/ECS state. These systems are genre-independent: do not add FPS, RTS, PHYSIS, Box3D or visual renderer dependencies. Do not change the authoritative `WaterBodyComponent.flow` to implement presentation effects. Use `docs/nereid/HANDOFF.md` for current limitations, implemented APIs, exact commits, known tests and next agent steps.

Source: `engine/world/nereid/NereidVortices.h`.
Status: experimental; native performance, platform tests and renderer parity must be verified before production use.
