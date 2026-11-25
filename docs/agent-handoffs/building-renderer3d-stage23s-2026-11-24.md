# AIVERSE building generator — Stage 23S: common Renderer3D model upload, draw and safe GPU retirement

Date: 2026-11-24
Repository: chromealex/AIVERSE
Branch: feature/aiv20261123_building-grammar
Draft PR: https://github.com/chromealex/AIVERSE/pull/114

## Goal and architecture constraint

User approved Stage 23S after Stage 23R ECS/PHYSIS rollback work. The engine has common ECS/Simulation + Renderer3D and MUST NOT depend on obsolete legacy FPS, ActorHost or RTS modules for building features. This stage provides a concrete integration with common WorldScene's existing Renderer3D draw path, and a device-generation-checked graphics resource owner. Building physics, authority and graph ACK remain exclusively in the existing BuildingDualLeafGameSession/BuildingEcsRuntime owner, not the render thread.

## Existing source inspected

- engine/buildings/BuildingDualLeafGameSession.h: borrowed render::Model plus BuildingApertureLeafPose callback; forEachCpuModel exposes models without transfer of ownership.
- engine/buildings/BuildingAperturePresentation.h: two visual leaf transforms and one CPU render::Model per aperture socket; runtimeMutable geometry, no per-frame CPU rebuild.
- engine/render/Model.h: render::prepareModelUpload, render::uploadModel, render::releaseModel; unique/buffer/texture GPU ownership.
- engine/render/Renderer3D.h: ModelDraw, drawModel, persistentId/staticPacketRevision, invalidateModelCachesFor and existing shader/batch owner.
- engine/render/RenderDeviceLifetime.h: host-owned generation-specific render-thread device Lease.
- engine/world/WorldScene.h/.cpp: real shared draw(Renderer3D&,Cell,int) for native scene visuals and a verified shutdown idle barrier; no generic building render attachment existed.
- engine/render/AsyncResourceRetirementQueue.h: existing asynchronous completed-fence model retirement used by chunk/world scene. Not duplicated here; the building scene currently chooses conservative full-device idle before releasing its session-owned models.

## Changes actually committed

1. NEW engine/buildings/BuildingRenderer3DScene.h:
   - BuildingRenderer3DScene with Status { Detached, Resident, CleanupRequired }, explicitly owned by the graphics device and one already attached BuildingDualLeafGameSession.
   - initialize(device,renderer,RenderDeviceLifetime::Lease,session,instanceNamespace): validates session, device generation and owner thread, gathers up to 64 unique CPU models, disables rigid merge/autolod/shared geometry pooling, prepares and uploads each model through existing render::prepareModelUpload and render::uploadModel. Speculative upload uses publishSharedTextures=false so transient imports do not mutate the shared texture cache. The owner retains model pointers until shutdown completes.
   - draw(renderer,origin,cellSize,renderDtMs): requires resident same-generation GPU lease, obtains only graph-acknowledged poses from BuildingDualLeafGameSession::submit, computes world::relativeTo for camera-relative transforms, and sends a separate render::ModelDraw to existing Renderer3D::drawModel for both leaves. Persistent IDs derive from unique host-provided scene namespace + socket + leaf and are not a function of camera origin. Static packet reuse is disabled for moving visuals. The authoritative ECS/SpatialWorld state is never mutated by drawing.
   - shutdown(): only at safe owning renderer frame boundary, and only after a successful gfx::Device::waitForResourceIdle barrier. Invalidates Renderer3D model caches and calls releaseModel for all retained CPU models. If device generation is stale or idle refuses, the owner retains model references (CleanupRequired or Resident) and returns false rather than deleting them while GPU work is in flight. Host must keep session/device alive and retry; no unsafe destructor-driven release.
2. engine/world/WorldScene.h:
   - Weak typed reference registration setBuildingRenderOwner(shared_ptr<building::BuildingRenderer3DScene>) and setBuildingRenderElapsedMs(uint32_t). Neither shared ECS nor graphics ownership is transferred to WorldScene.
   - buildingDrawError() reports a failure to the caller because draw originally returns void.
3. engine/world/WorldScene.cpp:
   - Real WorldScene::draw path now invokes the optional building graphics owner after existing streaming terrain, water and forest submissions, using the same renderer camera and floating origin.
   - WorldScene::shutdown clears weak registration but deliberately does not delete session GPU Models; explicit owner shutdown is required before session detach.
4. engine/buildings/BuildingRuntime.h: exports the common Renderer3D scene owner.
5. examples/world/runtime-building/README.md: graphics setup/teardown example with real existing device lease, shared world scene draw, and explicit ownership.
6. skills/world/aiverse-building-style/SKILL.md: Stage 23S constraints, public integration API and next-agent handoff.

## Verified source commits (not runtime tests)

- eadda160ec42479d30146a09dd6647c23f1511d1 — new renderer resource owner.
- 33f041fb35b0c7ada31c8aaac6bc9c7e16261f47 — WorldScene weak owner seam and diagnostic error.
- 935bb1508c64914c78a58246a46cedfae40c6f0c — real WorldScene::draw submission and teardown link.
- 32e1ae3bdd256f5c9e0275511a7e621e05218e2d — BuildingRuntime umbrella export.
- e477b9e8432c3aa147bd3a5daf99f055e25f3418 — common packaged game integration README.
- df82b3cd3957926a8a6917ec16ab8e36d715f755 — building agent skill update.
- This handoff is its own final stage documentation commit.

## Owner-phase procedure for next agent

1. Common Simulation::setup/BuildingEcsRuntime from Stages 23Q/23R controls physics, graph ACK and authoritative active socket progress. The common scene owner retains BuildingDualLeafGameSession and prepares its visuals.
2. On the actual existing gfx::Device thread BEFORE a GPU rendering pass, use BuildingDualLeafGameSession::bindMaterial for each socket (PBR image resolver must reference real packaged assets), then initialize a shared BuildingRenderer3DScene(device,renderer,existing RenderDeviceLifetime lease,session,unique scene instance namespace). Do not allocate another gfx::Device or a parallel texture material cache.
3. Register WorldScene::setBuildingRenderOwner(owner). Every rendering frame setBuildingRenderElapsedMs(dt) and call existing WorldScene::draw(renderer,origin,cellSize); it now includes two leaf ModelDraw calls per socket with revision-safe CPU poses.
4. On scene unload, remove the weak registration first. At a graphics frame boundary outside passes invoke owner.shutdown. If not successful, keep the device/session and retry later; NEVER detach the session or GPU device generation. After success the common scene owner may detach BuildingDualLeafGameSession and BuildingDualLeafOwner and release collision ID leases.
5. On rollback the ECS owner is suspended until the externally owned physics and graph state are reconstructed. After the graphics owner has been retired and after new BuildingDualLeafGameSession CPU models exist, initialize the new graphics owner with a new safe scene namespace/epoch; do not draw old or unacknowledged CPU poses.

## Known limitations / actual testing

No C++ compiler, CMake, CTest, native WorldScene main-loop execution, render screenshot, GLB parser/validator, actual PBR map sampling, GPU resource retirement trace, PHYSIS replay, or Metal/Vulkan/GLES test was run. User explicitly deferred testing until functional work is complete. These are implemented source-level integration paths, not proof of successful live GPU execution.

The GPU owner intentionally uses a synchronous full-device idle barrier on shutdown instead of the engine's asynchronous resource retirement queue: this is conservative and safe if device idle works but can block scene unload. Future iteration can transition to completion-token retirement with preserved Model/session ownership until each token is confirmed. The graphics owner MUST be explicitly shutdown; WorldScene only weakly references it. A concrete game executable must opt in by retaining and initializing the owner; default WorldScene has no building owner. Current render integration covers sliding-door/gate/roof leaves but not all lift mesh publication or arbitrary moving building subsystems. Static building geometry remains generated by the existing procedural model authoring and runtime model path.

## Next stage

Stage 23T: finish HTML5 Studio building generation controls, interactive PBR/UV preview, recipe reopening and authoring of semantic graph/physics sockets so a scene can actually instantiate moving leaves. Stage 23U: full previously deferred native C++/TypeScript/CMake/CTest/glTF validation, PHYSIS rollback, Renderer3D GPU memory and Metal/Vulkan/GLES acceptance, repair actual failures, reconcile PR #114 with main and only then merge. Continue creating independent handoff and updating skill after every stage.
