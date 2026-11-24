---
name: aiverse-nereid-interactions
description: Water entry/exit/wake event ECS and read-only VFX/audio presentation, reusable in any native AIVERSE project.
---

# NEREID — Water entry/exit/wake event ECS and read-only VFX/audio presentation

Read `AGENTS.md`, `docs/nereid/ARCHITECTURE.md`, `docs/nereid/HANDOFF.md` and the common `skills/gameplay/aiverse-nereid-water/SKILL.md` first.

## Package and native API

Use `target_link_libraries(my_native_game PRIVATE AIVERSE::lib_nereid_interactions)` in CMake, or `aiverse_native_use_libraries(aiverse_native_GAME nereid-interactions)` when using the native module helper. The shared water dependency is `AIVERSE::lib_nereid_water`. Include `"engine/world/nereid/NereidInteractions.h"`; namespace `aiverse::lib::nereid`.

After same-tick WaterContact sampling, addWaterInteractionSystem produces single-tick enter/exit/wake WaterInteractionOutputComponent events. Attach WaterInteractionComponent and WaterInteractionStateComponent to each producer. Register registerNereidInteractionPersistence. Presentation reads copied events; it never mutates canonical gameplay water.

## Engineering contract

All simulation inputs and outputs are deterministic Fixed/ECS state. These systems are genre-independent: do not add FPS, RTS, PHYSIS, Box3D or visual renderer dependencies. Do not change the authoritative `WaterBodyComponent.flow` to implement presentation effects. Use `docs/nereid/HANDOFF.md` for current limitations, implemented APIs, exact commits, known tests and next agent steps.

Source: `engine/world/nereid/NereidInteractions.h`.
Status: experimental; native performance, platform tests and renderer parity must be verified before production use.
