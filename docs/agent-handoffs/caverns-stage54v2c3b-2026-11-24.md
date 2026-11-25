# Caverns Stage54v2c3b — admitted native character bridge and fixed-tick fixture

Date: 2026-11-24. Branch `integration/caverns-studio-stage54c-20261124`. Starting HEAD 6b3c4d14; roadmap b76d16c4; implementation/test/docs HEAD `d63d382b94f928ea01d3e4474aaf3c521f08ed4a`.

## Delivered
- `engine/physics/TerraWeaveCavernsPlanetMotorBridge.h`: opt-in translation of ground steering to existing Box3D oriented character input, desired speed bounded by route speed, approach distance and existing walk speed. Validate current canonical epoch, route scope, native lease, matching chart, scene, proxy and capsule dimensions; step existing motor on a private candidate; check resident segment and lease again before publishing candidate and existing proxy/push operations. No independent scene step, second movement writer or floating-point lockstep state. Swimming/flying need their own existing motor.
- `engine/world/TerraWeaveCavernsPlanetRuntimeFrame.h`: scene identity guard for bridge.
- `tests/terraweave_caverns_planet_authority_test.cpp`: actual Simulation fixture with existing PHYSIS installer, NEREID sphere persistence schema, canonical capture/repath/steering, motor consumer, gravity revision change, scope/epoch save/load and rollback checks. The fixture consumer is read-only, not a physical traversal.
- `tests/terraweave_caverns_planet_collision_test.cpp`: oriented steering input and desired speed regression. Existing real oriented Box3D motor test retained.
- Native example README describes host integration, managed environment refresh, admission and single Scene step.

## Evidence
Source committed only. NOT RUN: compiler, CTest, fixed tick fixture, save/load, actual bridge with native published GPU/GI cohort, performance, platforms and headless acceptance. No real playable host or default GameCore migration yet. No new geometry, editor property or graph; existing Studio/MCP regressions NOT RUN. Main unchanged. Native host must bind actual GI hooks and reader exclusion, refresh managed leases after canonical capture, own actor state and fallback policy; native physics is not lockstep authority.

## Remaining
Stage55a: consolidate and execute native/Studio verification on the Mac KINGSTON build, fix compile/runtime regressions, verify full native body cohort publication and actual movement through authored planetary caves. Default GameCore migration is still separate because its movement is grid based; do not claim complete coverage of every host. Verify collision/renderer/GI lifecycle, restore, invalidation and safety before merging. If full playable host acceptance is required, add a concrete host with actual GI implementation rather than fake success callbacks.
