# TerraWeave Stage 48 — native terrain runtime and batched grass rendering

2026-11-19; `feature/terraweave`; Draft PR #24.

## Actual commits

- `0a954feec4a04e9d4413198dfd30d21a53d3d6e7`: new `engine/world/TerraWeaveTerrainRuntime.h` coordinating Earthworks, NEREID bank erosion, RootSupportRegistry and GrassCoverRegistry, with bounded per-frame RHI/collision tile uploads.
- `5e58743b6eedaea5b2fcb9e782616fdf8e2f3efe`: limit active/pinned terrain residency and use load-before-unload collision policy; only resident edited tiles are queued, nonresident edits are baked on demand.
- `de9d0d2cdbe8d2f621a1a3c078b5ac7afd3d93b6`: new `engine/render/TerraWeaveGrassRender.h`. `makeGrassPatchModel` reduces up to 262,144 potential blades in a `GrassPatch` to max 8,192 stable stratified blade candidates, each 1–2 opaque pointed triangles; one `render::Model` draw per patch, NOT per blade. Bounded `GrassPatchRenderer` owns up to 256 resident patches, max 8 updates per owner-thread frame, detects patch/environment changes, and invalidates geometry only for intersecting Earthworks edits. Grass size, survival and tint depend deterministically on `PlantEnvironment` age, sunlight, moisture, nutrients and sparse coverage. No per-blade ECS components, persistent blade arrays or special GPU renderer.
- `0300072a78516f9cacc4b16f47e0851f24e20a8a`: independent `GrassRenderOriginCm` contract.
- `dcef1803494a8adcfa10c4f3d5052977212c23af`: integrated `TerrainMutationCoordinator::publishGrass(device,visibleRect)` and grass `draw()/shutdown()` into the runtime; terrain edits flag only affected grass GPU patches.

## API and architecture

Instantiate `TerrainMutationCoordinator(EarthworksLayer&,tileCellSizeMetres,terrainResolution,RootSupportRegistry*,GrassCoverRegistry*)` from a native project. `excavate(brush,tick)` and `erode(completedNereidBankProbes,tick,dtMs)` write the SAME sparse Earthworks height source, propagate sparse grass and root support state, and queue only affected resident tiles. `publishVisible(device,spatial,pinnedCells,maxTileUploads,idProvider,materialProvider)` builds and swaps terrain geometry and SpatialWorld collision from the same `TerrainPatch`, with max 8 new tile uploads per frame, max 512 pinned cells and conservative 4m pinned triangles. The host must reserve nonoverlapping collider ID intervals, omit duplicate old static terrain collision/draw submission, and supply the FULL physics/simulation pinned set, not frustum-only visibility. Existing regions remain until all newly requested tiles are ready.

`publishGrass(device,visibleCmRect)` proximity-selects patches in range, builds valid bounded GPU models using the same authoritative Earthworks heights, while `draw(renderer,origin)` submits terrain and grass via existing Renderer3D (not custom RHI). `takeTerrainDirty()` emits `TerrainPatchDirtyRegion` after actual collision/RHI publication for host NavMesh and near/far vegetation invalidation. Native ECS/game must bind WorldScene, simulation authority and navmesh/asset streaming explicitly; the coordinator does NOT automatically install itself into RTS/FPS game clients.

## Actual verification / limitations / next

GitHub writes and source readback succeeded. Native Clang/GCC, CMake/CTest, Box3D, Metal/GL/Vulkan screenshots, frame-time/memory profiling and native RTS/FPS integration **NOT RUN**, per user's feature-first stage. No proof of 1 draw per patch was measured on RHI: the adapter emits one model descriptor per patch into Renderer3D and backend batching policy decides actual GPU calls. It uses opaque triangles, not photorealistic textured grass, and small quality tiers may thin dense populations. NavMesh dirty events are delivered but the game's navmesh rebuild remains host-owned. Live runtime calls must be serialized against NEREID ECS and RHI worker barriers; never sample mutable Earthworks on async workers.

**Next:** optional Box3D terrain/plant fall adapters (Stage49), then connect native project ECS host to soil revisions and navmesh. Capture first native compilation failure and fix before claiming success. Keep PR Draft.
