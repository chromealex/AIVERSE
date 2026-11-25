# AIVERSE Buildings — Stage 23Q: common genre-neutral ECS/Simulation integration

Date: 2026-11-23
Repository: chromealex/AIVERSE
Branch: feature/aiv20261123_building-grammar
Draft PR: https://github.com/chromealex/AIVERSE/pull/114

## User decision and scope

After explicitly reverting obsolete FPS/ActorHost coupling, the user approved continuing functionality before tests. ALL building feature systems must use common ECS and engine libraries independent of RTS, FPS or other genre modules. This stage adds common Simulation registration/persistence and a safe rollback boundary. It changes NO legacy FPS or ActorHost files.

## Existing code inspected

- `engine/sim/Simulation.h/.cpp`: common Simulation contains ECS World, deterministic fixed tick, `addSystem`, `insertSystem`, copy-on-write `SimSnapshot`, `addStateRestoredHook` and `PersistentEcsRegistry`. `Simulation::setup` invokes module setup, auto-installs PHYSIS (if compiled) and then seals persistence registry.
- `engine/core/ECS.h`: shared `World::setResource<T>`, `resource<T>`, and snapshot-COW semantics.
- `engine/sim/PersistentEcs.h`: versioned persistent `registerResource<T>` and Archive io contract.
- `engine/buildings/BuildingDualLeafOwner.h`, `BuildingDualLeafGameSession.h`, `BuildingGraphChanges.h`, `BuildingAperturePresentation.h`: native engine-wide physical pair, command/graph ACK and committed presentation already exist, but are not part of Simulation by default.
- `engine/world/WorldScene.h`, `engine/scene/SceneCollisionBridge.h`, `SceneCollisionNamespaces.h`: existing shared scene render and collision ownership. Do not create a second storage.

## Changes committed

1. NEW `engine/buildings/BuildingEcsRuntime.h`:
   - Copyable `BuildingEcsLeafState` and `BuildingEcsWorldState` with versioned Archive io. ECS singleton records sceneId, fixedTick, physics revision, graph ACK and bounded per-aperture socket ID/progress/open/moving flags. No graphics handles or raw collider pointers are stored in World.
   - `registerBuildingEcsPersistence(Simulation&, sceneId)` registers the persistent resource from common GameModule::setup BEFORE the persistence registry seals, creates the ECS singleton.
   - `BuildingEcsRuntime::attachAfterSetup(sim, dualSession, dualPhysics, spatial, graphRouter, stepPermille)` installs an ordered `building.dual_leaves` Simulation system AFTER PHYSIS/module registration, BEFORE the first tick. It captures a weak_ptr and therefore does not keep a deleted scene alive or invoke a dangling owner.
   - Initial dual-leaf positions are copied into the persistent singleton at attach, and each actual physical step updates it (including pending graph ACK states). Presentation remains gated by actual graph ACK.
   - Common `enqueue(command)` sends an already authenticated ECS actor command into the existing dual-leaf game session; `retireActor(entity)` works with ECS generation lifecycle.
   - Simulation rollback/state restore triggers a hook that FAILS CLOSED: runtime suspends rather than progressing stale external SpatialWorld/graph/gpu state. A new reconciled scene/session must be attached by the actual host after reconstruction.
2. `engine/buildings/BuildingRuntime.h` now exports the common ECS adapter.
3. `skills/world/aiverse-building-style/SKILL.md` documents the two-phase Simulation lifecycle, PHYSIS ordering, rollback and renderer/authority boundaries.

## Required actual host connection

Call `registerBuildingEcsPersistence(sim,sceneId)` from the common GameModule::setup before the persistent ECS registry seals. Game creates/generated construction model and one true SpatialWorld dynamic collision owner with two disjoint collider IDs per operable socket, registers real graph consumers (Navigation, Spatial, etc.) and builds the existing `BuildingDualLeafGameSession`. AFTER common Simulation::setup returns, BEFORE sim.step() begins, call BuildingEcsRuntime::attachAfterSetup(). Keep its returned shared_ptr in the common scene owner. The callback is automatically executed as an ordinary ordered Simulation system after movement/PHYSIS. The existing authenticated transport resolves client actor IDs to real ECS Entity generation before calling runtime.enqueue. Renderer3D resource-owner callbacks upload the generated CPU models and submit accepted leaf poses; this adapter does not touch GPU resources.

On World rollback, saved ECS state is restored but the external physics/graph/router/presentation are not part of COW World. The hook suspends system and queue acceptance until game host does an authoritative reconstruction (destroy old host/session/physics, rebuild real colliders/graph consumers and reconcile saved leaf progress). Do NOT simply clear `suspended_`; there is no unsafe resume method. Host is responsible for dropping stale commands and GPU leases before removing old CPU models.

## Exact source provenance

- `3de77955e08a27dd8e2460a3dec7ae4611a77e65`: new common ECS Simulation integration and persistent resource.
- `5c8ebb2d179bdae5fafb0592569c12b273747bd5`: initial snapshot and pending graph ACK state correction.
- `3c464020cbbb4f4a888e078e201f2bd7dd8aba72`: umbrella export from BuildingRuntime.
- `7846538ad2a598bee3ddc684bc841a13ef236c31`: building skill rules.
- This file is its own subsequent handoff commit.

## Actual tests / limitations

Tests explicitly deferred by user. NO C++ compile, CMake, CTest, sample game host startup, PHYSIS sensor/motor test, network replay, glTF, Metal/Vulkan/GLES draw or memory lifetime tests run. The common integration is an OPT-IN registration helper, not an automatically installed system in all games and not proof that a concrete game map already calls it. The game/module must call the two-stage setup with real owner objects; renderer model publication remains a separate resource owner. The ECS singleton persists leaf progress but the physical owner and graph routing still retain derived state outside ECS; rollback is intentionally suspended, not automatically reconciled. Native world/PHYSIS collision and graph users must be registered separately. Do not describe all rollback physics as solved.

## Next concrete steps

Stage 23R: add safe reconciliation from restored canonical ECS leaf state to SpatialWorld two-leaf colliders, synchronize old/new scene collision ID namespaces and verify dynamic navigation graph invalidation on each committed revision. Implement a true common PHYSIS/ECS obstruction adapter using existing ECS world components, not FPS character structs. Stage 23S: wire actual shared Renderer3D render owner, GPU upload/draw/retirement. Stage 23T: finish Studio editor authoring and interactive preview. Stage 23U: user-deferred full native/web/glTF and GPU acceptance, fix compile/runtime issues and reconcile Draft PR with main. Create new skill/handoff per stage.
