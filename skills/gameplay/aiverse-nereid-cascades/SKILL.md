---
name: aiverse-nereid-cascades
description: Shared cascade/waterfall geometry, joins and optional flow continuity, reusable in any native AIVERSE project.
---

# NEREID — Shared cascade/waterfall geometry, joins and optional flow continuity

Read `AGENTS.md`, `docs/nereid/ARCHITECTURE.md`, `docs/nereid/HANDOFF.md` and the common `skills/gameplay/aiverse-nereid-water/SKILL.md` first.

## Package and native API

Use `target_link_libraries(my_native_game PRIVATE AIVERSE::lib_nereid_cascades)` in CMake, or `aiverse_native_use_libraries(aiverse_native_GAME nereid-cascades)` when using the native module helper. The shared water dependency is `AIVERSE::lib_nereid_water`. Include `"engine/world/nereid/NereidCascades.h"`; namespace `aiverse::lib::nereid`.

Attach WaterCascadeComponent on a cascade entity, optionally WaterCascadeConnectionsComponent, WaterCascadeJoinPolicyComponent and WaterCascadeFlowPolicyComponent. Call registerNereidCascadePersistence at setup. Simulation uses fixed-point ECS data, while WorldScene generates presentation-only descent ribbon. Keep cascade flow policy optional and unrelated to genre.

## Engineering contract

All simulation inputs and outputs are deterministic Fixed/ECS state. These systems are genre-independent: do not add FPS, RTS, PHYSIS, Box3D or visual renderer dependencies. Do not change the authoritative `WaterBodyComponent.flow` to implement presentation effects. Use `docs/nereid/HANDOFF.md` for current limitations, implemented APIs, exact commits, known tests and next agent steps.

Source: `engine/world/nereid/NereidCascades.h`.
Status: experimental; native performance, platform tests and renderer parity must be verified before production use.
