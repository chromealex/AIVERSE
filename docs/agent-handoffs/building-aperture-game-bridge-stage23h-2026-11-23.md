# AIVERSE — Stage 23H: authoritative aperture game/render adapter

Date: 2026-11-23
Repo: chromealex/AIVERSE
Branch: feature/aiv20261123_building-grammar
Draft PR: https://github.com/chromealex/AIVERSE/pull/114

## Completed work and architecture
Inspected branch versions of BuildingApertureFixedTick.h, BuildingApertureOwner.h, BuildingAperturePresentation.h, RuntimeBuildingModel.h, Renderer3D.h and SpatialWorld.cpp. Existing Renderer3D uses ModelDraw and owns GPU resources; SpatialWorld::add/remove are the existing dynamic collider mutation APIs. This stage does not add another renderer, physics backend, scheduler or packet transport.

NEW engine/buildings/BuildingApertureGameBridge.h:
- BuildingApertureGameBridge::attach(sceneId, authorize, reachable, apertures, style): creates one scene-bound fixed-tick owner with required server-side authorization/range guards and CPU leaf presentation assets. Fails without a physically attached aperture owner.
- enqueue(World&, BuildingApertureCommand): uses the preexisting bounded command queue; caller authenticates the network connection and resolves actor ownership externally.
- fixedTick(ecs, apertures, spatial, router, serverTick): delegates to the existing authoritative fixed tick, so physics commits happen before registered graph invalidation, and accepts read-only presentation state only after complete graph ACK.
- render(dtMs, submitLeaf): delegates interpolation and passes immutable CPU Model and per-leaf pose to the existing Renderer3D owning callback. The host maps the output to ModelDraw, creates uploads and retires GPU resources under its existing lifetime/fence discipline. Renderer callback rejection never changes simulation or gameplay permission.
- retireActor(Entity), model(socketId), detach(), attached()/sceneId(): game owner lifetime surface. CPU Model lifetime must outlive GPU leases and any in-flight draw.
- engine/buildings/BuildingRuntime.h exports this bridge.
- skills/world/aiverse-building-style/SKILL.md documents the scene binding/order, renderer handoff and limits; existing skill catalog registration remains unchanged.

## Ownership and scene integration order
1. Existing game owner calls RuntimeBuildingGenerator::generateRecipe and publishes static building collision.
2. Existing SpatialWorld owner attaches BuildingApertureOwner with unique collider IDs and live authoritative obstruction sensor.
3. Existing graph consumers (native navigation plus any relevant hierarchy/room/road/zone/etc.) register their callbacks; only requested actual domains enter the event mask.
4. Instantiate BuildingApertureGameBridge, call attach(sceneId, actor-authorize, actor-interaction-volume, apertures, style). Do not create duplicate per-aperture network sessions.
5. Existing transport authenticates commands, server maps clients to real ECS Entities, calls bridge.enqueue. In the exclusive world-owner fixed phase after movement, call bridge.fixedTick(...). The returning frame is not valid for visual publishing unless presentationAccepted is true.
6. Existing Renderer3D phase calls bridge.render(renderDtMs, submitLeaf) and maps const Model + world-space leaf pose to its own ModelDraw instance. Upload once and use existing RenderDeviceLifetime leases, not per-tick Model rebuilds or a second GPU owner.
7. On logout/despawn, call retireActor before ECS generation reuse. On unload, reconcile actors/graph events, release in-flight GPU uses, then detach.

## Exact provenance
- 35266e04ec8e91c3b43564e17840807775d3dd7b — new native game bridge.
- 9ce0b56785b0a1e2ddfaf6d3ba16fea620622484 — BuildingRuntime.h umbrella export.
- 35631f34f56273947d368091cc064c52b697660b — skill integration instructions.
- This handoff is published as an additional documentation commit.

## Actual verification and limitations
Per user request, tests remain deferred. NO C++ compiler, CMake/CTest, Studio TS, real fixed-tick game replay, PHYSIS actor sweep, Renderer3D GPU/Metal/GL screenshot, or end-to-end multiplayer test was executed. The bridge source has not been inserted into an actual game executable's main-loop owner or Renderer3D ModelDraw submission call. Source presence is NOT proof of successful build or runtime interactivity. Renderer integration requires a host callback using real live model/instance resource ownership.

In particular, the existing BuildingApertureOwner remains binary (instantaneously removes/adds one full-width collider), and the presentation uses simplified interpolated half-panel CPU meshes. This is NOT yet moving physical leaves with swept collision, velocity, multi-step transactional rollback, opening-progress interlock, or tight rendering/physics synchronization. Never assert these features are complete based on this stage.

## Next concrete step
Stage 23I: introduce physically swept/stepped collider leaf motion with real PHYSIS entity/vehicle obstruction probes, rollback-safe SpatialWorld updates and deferred passage authorization until leaves are fully clear. Reconcile the existing dynamic NavSource with moving leaf positions and graph dirty regions per committed substep. Then add actual game/world owner registration and Renderer3D draw submission using the current renderer's proper ModelDraw/GPU lease facilities. Once feature completion is explicitly requested, compile and run all deferred native/graphics/network tests and reconcile Draft PR 114 with current main without overwriting concurrent changes. Always create a new handoff documenting exact files, APIs, tests, branch and commit.
