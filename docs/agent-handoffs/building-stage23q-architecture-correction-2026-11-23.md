# AIVERSE Buildings — Stage 23Q architecture correction: reject legacy FPS coupling

Date: 2026-11-23
Repository: chromealex/AIVERSE
Branch: feature/aiv20261123_building-grammar
Draft PR: https://github.com/chromealex/AIVERSE/pull/114

## User correction / architectural decision

User explicitly stated that the prior FPS module is obsolete for this work: the modern AIVERSE engine runs all mechanics through shared ECS and genre-neutral native libraries. Building generation, dual-leaf movement, PHYSIS contacts, graph invalidation, materials/textures and renderer scene publication must NOT be installed in the old FPS/ActorHost client or network authority. The previous Stage 23Q attempt incorrectly added legacy FPS coupling, and the source changes were reversed immediately when the user corrected the architecture.

## Specific Stage 23Q reversals committed

- engine/actor/ActorModule.h restored to the exact pre-23Q version (commit ac3df16a9430578e84c65c7db4beefe76a158b73).
- engine/actor/ActorModule.cpp restored to the exact pre-23Q version (commit 6ec9c35ac6eb8b99d153c4214c6ca5c0842f2643).
- engine/net/authority/Session.h restored to the exact pre-23Q version (commit 087d3f396c0f4b9df5927f67b9c0e5f1d8b04809).
- apps/fps/client.cpp restored to the exact pre-23Q version (commit e851d487b53c487566b9d765b39b45963ac9b287).
- engine/actor/ActorBuildingApertures.h deleted (commit 021c65d3cdfcd67334d653a4b577dbc16a2281e0).
- apps/fps/FpsBuildingDoorScene.h deleted (commit 9816d1880f8b13d0669e8d514a585ea05dbbdad3).
- skills/world/aiverse-building-style/SKILL.md updated to prohibit genre-specific reintroduction (commit b9e5164c5a0a61f442a907aec5ca2dfab95e3a60).
- This handoff is recorded as its own separate documentation commit.

These reversals only undo changes introduced for the aborted Stage 23Q. They do not delete pre-existing legacy FPS code, which is outside the building feature's scope.

## Architecture / APIs that remain

- libraries/include/aiverselib/systems/RuntimeBuildingGenerator.h: portable, genre-independent building generation.
- engine/buildings/BuildingDualLeafOwner.h: authoritative world::SpatialWorld paired physical leaves and swept owner-provided obstruction callback.
- engine/buildings/BuildingDualLeafGameSession.h: authoritative ECS World command queue / fixed-tick / graph ACK + CPU leaf presentation bridge.
- engine/buildings/BuildingDynamicNavSource.h and BuildingGraphChanges.h: native navigation and graph invalidations.
- engine/buildings/BuildingAperturePresentation.h, BuildingMaterialAssets.h, RuntimeBuildingModel.h: existing Renderer3D-compatible CPU models and unified PBR materials/textures.
- Engine/common ECS Simulation and shared world scene owners are the proper integration points. No module name, genre, player model or network implementation should be hard-coded in the building system.

## Actual validation / limitations

No native compiler, CMake, CTest, real ECS simulation, PHYSIS, GPU upload, Metal/Vulkan/GLES screenshot or end-to-end network QA was performed. GitHub source updates are confirmed through the GitHub API, but the restored code was not compiled here. Stage 23Q is not considered completed functional integration. The common building session is still an API that must be registered in the real shared ECS/world scheduler and actual Renderer3D owner; do not describe an old FPS module as running this functionality. PR #114 remains Draft; do not merge without reconciliation and tests.

## Next concrete stage

Inspect the CURRENT shared ECS Simulation/World lifecycle and genre-neutral world scene presentation graph (not apps/fps or engine/actor/ActorModule). Register BuildingDualLeafGameSession only through the actual common ECS simulation owner and existing renderer resource owner, using real engine-wide PHYSIS actor/vehicle/cargo queries and stable scene collision namespaces. Integrate after existing movement/physics and before nav/visual publication; preserve the established command authorization and rollback boundaries. Then update this skill, create a new handoff, and run deferred tests only when user asks to lift the test deferral.
