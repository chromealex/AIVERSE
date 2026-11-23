# AIVERSE building generator — Stage 23R: shared ECS/PHYSIS obstruction and rollback collision-graph reconciliation

Date: 2026-11-23
Repository: chromealex/AIVERSE
Branch: feature/aiv20261123_building-grammar
PR: https://github.com/chromealex/AIVERSE/pull/114 (Draft; do not merge before verification)

## User requirements and source inspection

The user approved the next feature stage and repeatedly requires universal ECS and genre-independent mechanics. No legacy FPS/ActorHost integration is allowed. Inspected current branch `engine/buildings/BuildingEcsRuntime.h`, `BuildingDualLeafOwner.h`, `BuildingDualLeafGameSession.h`, `BuildingDynamicNavSource.h`, `BuildingGraphChanges.h`, `engine/sim/CommonComponents.h`, `engine/core/ECS.h`, `engine/physics/Scene.h` and `engine/scene/SceneCollisionNamespaces.h`. Existing canonical ECS Transform and World.eachExistingConst are usable. Optional PHYSIS has a native physics::Scene::overlapBox API. Native SpatialWorld is the existing source of authoritative sliding collider AABBs; SceneCollisionNamespaces owns stable collision ID reservations. BuildingEcsRuntime Stage 23Q suspends after Simulation rollback but previously had no safe reconstruct/rebind API.

## Implemented source files and APIs

- NEW `engine/buildings/BuildingEcsObstruction.h`: `BuildingPhysicalExtent {Vec3 halfExtents; bool blocksBuildingMotion}`, Archive io, `registerBuildingPhysicalExtentPersistence(sim)`, and `buildingEcsSweptOccupied(World,firstLeafSweep,secondLeafSweep)`. Iterates existing ECS PhysicalExtent components in the real World, requires a live Entity and canonical Transform, performs conservative world::overlaps for BOTH independently moving leaf sweeps. `buildingCommonSweptProbe(World)` matches BuildingDualLeafOwner's obstruction callback signature. Zero Box3D includes or mandatory linkage. Game authors must attach and update this physical extent on relevant existing entities: no genre-specific actor struct is assumed.
- NEW `engine/buildings/BuildingPhysisObstruction.h`: optional Box3D-specific adapter around the EXISTING `physics::Scene::overlapBox` using real body shapes/categories, combined with the generic ECS probe. Caller-owned PHYSIS scene remains authoritative; no second Box3D solver. PHYSIS query filter must exclude the leaf bodies if they were also published into Box3D. These SpatialWorld uint32 IDs are not Box3D BodyHandles.
- `engine/buildings/BuildingDualLeafOwner.h`: initial `attach` now supports an optional validated map of saved per-socket physical travel progress plus saved revision without aliasing existing colliders. New `attachRestored(poses,ids,positions,revision,spatial,sceneId,sweepProbe)` re-creates exact two-leaf position AABBs on a NEW detached owner; partial travel is restored as stationary/blocked. `begin` now checks actual physical endpoint rather than a stale boolean so an operator can close/reopen a partially restored aperture.
- `engine/buildings/BuildingEcsRuntime.h`: `rebindAfterRestore(freshSession,freshPhysics,spatial,freshRouter)` only succeeds while suspended, after source scene/revision/socket identity, exact restored per-socket physical progress, real SpatialWorld collider IDs and AABB, and complete graph ACK all agree with the canonical saved ECS singleton. It reuses the original Simulation system registration (weak_ptr captured) rather than adding a duplicate system. No direct unsafe unsuspend setter.
- NEW `engine/buildings/BuildingEcsReconcile.h`: `restoreBuildingEcsLeafScene(sim,runtime,freshPhysics,freshSession,spatial,freshGraphs,authoredPoses,reservedIds,probe,style,authorize,reachable,graphDomains)` validates authoritative saved ECS positions (including exact socket set and endpoint flags), attaches fresh physical two-leaf owner, publishes the conservative full-travel invalidation for ALL explicitly requested graph domains with the current actual SpatialWorld state, waits for all consumer ACKs, attaches a fresh visual session, reconciles ECS metadata and invokes `rebindAfterRestore`. Failures detach new owners and preserve/suspend old ECS state where possible. The graph invalidation covers both panels' possible travel beyond original door bounds, not just the closed aperture rectangle.
- `engine/buildings/BuildingRuntime.h` exports the portable ECS obstruction/reconciliation APIs. Optional PHYSIS header is explicitly separate.
- `skills/world/aiverse-building-style/SKILL.md` and `examples/world/runtime-building/README.md` document the genre-neutral ECS setup/owner boundaries and exact reconstruction recipe.

## Architecture and safety

Common Simulation/World and the existing PHYSIS (optional) and SpatialWorld owners remain authoritative. Run building sweep AFTER movement/contact preparation within the real fixed-tick owner. Never count a moving leaf as its own PHYSIS obstruction. Graph consumers for Navigation, Spatial, Hierarchy, Transport, Gameplay, Presentation, Audio and Simulation are independent; register actual owners before restoration, publish a revisioned structure invalidation to those domain masks, and refresh native NavMesh in its normal graph owner phase. ACK means graph invalidation accepted, NOT navigation already fully rebuilt. Restrict actor access to a full-open physical endpoint. Do not use obsolete FPS systems or invent a second ECS/physics registry.

Restore only into fresh, unowned collision IDs obtained from the existing scene collision namespace owner. The canonical ECS snapshot does not serialize all external-world graphic/transport/physics state: host must retire old GPU leases and callbacks and reconstruct all external owners first. Partially open restored leaves are stationary until an authenticated command selects a new endpoint. Old command sequence caches are NOT replayed on restore; transport/authentication remains external. If graph/physics reconstruction fails, BuildingEcsRuntime stays suspended and should not advance.

## Source provenance (this stage)

- `2b00bd83e50de433a95a918a63175c80a58bf098`: initial combined ECS/PHYSIS query.
- `b08978c5ac80018f69513f144d729eca46510218`: two-leaf exact physical restoration.
- `bea7c1a1bdb6c86c0b9897262b0bfc097a8684ef`: progress-aware endpoint command check.
- `232ed8d00d72ed93573bab2a7f40e537c4ad4d61`: verified common ECS rebind.
- `325af1b5318f9f573c556665cf9d1556f4a5c641`: world/graph/visual reconstruction transaction.
- `de4cbe52aeb782f76234808ba9bef90e87c1a2d4`: restore full canonical snapshot on rejected rebind.
- `6d8e9379db38624fb8a007c7c2178d50d476d0bc`: account for sliding panels beyond the opening in graph invalidation.
- `e379000fc902e46e6d300cbd02591dc4d65695b5`: portable ECS-only obstruction and persistent extent component.
- `875f8d94cfbe56534680eb3a56ec4c93ea02b404`: optional PHYSIS/Box3D adapter.
- `fa2440cb20a7c780997be1c8772abb87eb1161d3`: runtime umbrella export.
- `a30a2217540d8ebc6604552f17df44e52622d18e`: agent skill update.
- `10f66500eb3b3f9aa8bf09756a71c08a251662f3`: native packaged-game usage guidance.
- The present file is saved in a separate documentation commit.

## Testing, limitations and next step

Per the user's explicit feature-first direction, NO C++/CMake/CTest, PHYSIS/Box3D collision playback, shared ECS rollback serialization, render/GPU, nav refresh, Metal/Vulkan/GLES or browser TypeScript test was run. GitHub source commits are evidence only of published code, not production-ready execution. Particular integration limits: applications must attach actual BuildingPhysicalExtent to relevant entities or provide a different canonical geometry probe; optional Box3D depends on build availability; native NavMesh rebuild and authenticated access/transport are owned elsewhere; no automatic project-wide runtime plugin installation or full GPU/scene reupload has been demonstrated. The restore API requires a new physical/presentation owner and fresh graph router; the general real game host is responsible for construction, old owner retirement and scene ID leases. Conservative AABB sliding leaves do not model articulated door collision.

Next Stage 23S: real engine-wide Renderer3D resource owner integration (ModelDraw upload, per-leaf instance transforms, provenance/revisions, GPU retirement only after appropriate render fences), without legacy FPS/RTS code. Stage 23T: HTML5 Studio full interactive preview and recipe edits. Stage 23U: previously deferred full native/Web/GLB/graphics/PHYSIS tests, resolve actual compile and runtime issues and reconcile Draft PR #114 with main. Add skill and handoff per stage.
