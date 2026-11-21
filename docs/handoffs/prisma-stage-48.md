# PRISMA Stage 48 — LightSystem, persistent scene and global geometry memory ownership

**Date:** 2026-11-19
**Repository:** chromealex/AIVERSE
**Branch:** `feature/prisma-postfx`
**Draft PR:** https://github.com/chromealex/AIVERSE/pull/26
**Baseline:** `docs/handoffs/prisma-stage-47.md`
**Code/agent skill/design HEAD just before this handoff:** `4e264241f8393dfdc3b218bf16eb550abf9fe367`. This handoff is a separate final stage commit.
**Main:** unchanged. Do not merge into main or change PR base without explicit user approval.

## Actual implemented commits and changed paths

1. `79384a3f8fb27f2e22005dfe7bfaab40b0841d9f`: new `engine/render/GpuOwnedMemory.h`, shared CPU-only RHI metadata accumulator, unique texture/buffer handle deduplication per owner, known lower-bound bytes, unknown metadata, dynamic buffer versions and saturated arithmetic.
2. `10c5dc1b0b385471806f8fc7592689c4aa7ecf91`: `engine/render/Lights.h`, public `LightSystem::ownedGpuMemoryStats() const` read-only API and shared helper include.
3. `2674c145bdb167f1cce169ae9dedc3333318e4c9`: `engine/render/Lights.cpp`, enumerate light map/color+depth, static light atlas, spot shadow, planar reflection color+depth, black placeholder, light map/halo/beam/decal streaming buffers. Lighting system remains owner; no alias transferred to Prisma.
4. `1a2dc39e2536431e2ddaaf2ab712fcf0eef00e78`: `engine/render/PersistentGpuSceneUpload.h`, count all three in-flight GPU-scene staging buffers by real RHI metadata instead of only current frame's buffer.
5. `ab2b139aabccd7bb88bc7ddfe29c6c59511311b9`: `engine/render/GpuSceneCulling.h`, count three-frame visibility output allocations, not CPU scene state.
6. `ebb54899fbb9e71edffe26b7c457a57bd8c9e46c`: `engine/render/GpuSceneDrawCommands.h`, count input/output/dense compaction/visible-count buffers in all three retained GPU frame slots and all submissions, not CPU lastRecords.
7. `b87cb9cf6e6ab9854d54d909a1335c53253231b6`: `engine/render/Renderer3D.h`, extend `ResidentMemoryStats` with `lightSystemKnownBytes`, `persistentUploadKnownBytes`, `persistentCullKnownBytes`, `gpuAuthoredKnownBytes` categories.
8. `26d13e2921f7f55efbfd2344673ad7a9b8b6f756`: `engine/render/Renderer3D.cpp`, append the four disjoint owner reports to its Stage 47 direct-handle resident totals; preserve separate pending-motion bytes; no borrowed model/material resources or independent GeometryPool pages are double-counted. The estimator is metadata-only, safe to call in pre-render host code.
9. `1f994f02646dc79cb54c3b2aa4e4da87bf2daea9`: `engine/render/Prisma.h`, public optional `setExternalGeometryPoolGpuMemory(uint64_t)` plus per-instance storage. Existing `setExternalRendererGpuMemory` contract remains compatible.
10. `12c2fde87fd6b29a0fb997fb7364f5868b097808`: `engine/render/Prisma.cpp`, include separate shared geometry-page bytes in the existing saturating memory-pressure calculation and reset on shutdown.
11. `6459f188029fef66dccb4b11ccaf0c0221caee97`: `apps/editor/Editor.cpp`, forward deduplicated renderer aggregate and `geometryPoolStats(device).allocatedBytes` before each Scene/Game `renderSizeForOutput` call.
12. `410aa79e949848a1b74dc512d23482c783bf87b8`: `apps/client/GameClient.cpp`, same host-side Renderer3D and global geometry pool estimate on client view ID 3.
13. `f9468da702f3baab5cce79784c36dd0e201dc77d` and `a32ebbc1d6406c8ccb05a6624810a49e5355b6c5`: `apps/editor/windows/StatsWindow.cpp`, GPU owner category detail including lighting, persistent uploads/culling/draws, geometry pool pages and missing/unknown metadata; avoid sampling geometryPoolStats twice and clarify included/excluded owners.
14. `b5acfcf5c957ac0083968d56944b05e93295a5ea`: `apps/editor/windows/PrismaWindow.cpp`, label adaptive memory figure as Prisma + Renderer3D owners + GlobalGeometryPool known minimum.
15. `afdf043d6b11cd6df39314f3a510b49e93759024`: `skills/visuals/aiverse-prisma/SKILL.md`, full agent integration, ownership boundaries and mandated features-before-tests sequencing.
16. `4e264241f8393dfdc3b218bf16eb550abf9fe367`: `docs/prisma.md`, Stage 48 memory architecture and conservative claims.
17. This separate file `docs/handoffs/prisma-stage-48.md`: handoff committed after source edits and readback.

## Architecture and API

The new `GpuOwnedMemoryCounter` uses read-only `gfx::Device::textureInfo` and `bufferInfo` on typed live handles. It cannot allocate, copy, map, GPU synchronize or release resources. For known R8, RGBA8, RGBA8_sRGB, Depth, RGBA16F, RGBA32F it uses a known **minimum** bytes/pixel. For compressed or unknown-format/metadata it records an unknown resource, not a zero allocation estimate. It records buffer `versions>1` as a caveat rather than multiplying logical bytes by an untrusted driver-specific factor. Repeated handles in one owner's resident report count once.

`LightSystem`, `PersistentGpuSceneUpload`, `GpuSceneCulling` and `GpuSceneDrawCommands` each enumerate only allocations created by their own owner. `Renderer3D::residentMemoryStats()` aggregates these mutually exclusive owners with the former directly owned sun shadow/scene capture/selection/trace placeholders/motion and direct buffers. It still reports pending retired object-motion targets separately and never double counts the aliased `sceneColor_/sceneDepth_` handles.

`GlobalGeometryPool` is a different owner: `geometryPoolStats(device).allocatedBytes` is the sum of buffer capacities of each still-live page in that device's shared weak-page registry. One page may back multiple Models; its bytes must only be added once. No attempt is made to enumerate the same shared page again from each Model or from the Renderer3D draw lists. Dedicated external unpooled model VBO/IBOs and textures remain intentionally uncounted.

Native host call order before each viewport's render-size decision:

```cpp
prisma.setViewId(viewId);
prisma.applyCameraVolumes(volumes, unjitteredCamera.eye());
auto owned = renderer.residentMemoryStats();
prisma.setExternalRendererGpuMemory(
    owned.liveTextureKnownBytes + owned.liveBufferKnownBytes,
    owned.pendingMotionKnownBytes);
prisma.setExternalGeometryPoolGpuMemory(
    geometryPoolStats(device).allocatedBytes);
auto [renderW,renderH] = prisma.renderSizeForOutput(outputW,outputH);
auto camera = prisma.jitteredCamera(unjitteredCamera,renderW,renderH);
renderer.beginScene(camera,renderW,renderH);
```

Everything remains genre-neutral and postprocess-only: never mutate FPS/RTS gameplay/ECS or input, never make jitter control picking/UI, and never enable temporal upscaling/adaptation by default. The serialized `temporalResolve.maxPrismaMemoryMiB` key remains unchanged even though the known-memory estimate it compares now includes Prisma, Renderer3D subowners, and GlobalGeometryPool pages. It is NOT true total GPU VRAM or a hard allocator cap. If known memory is over budget, the adaptive controller may step down future internal resolution after confirmed GPU completions; it never destroys live/pending GPU resources early.

**Resource omissions:** texture mips/driver placement/alignment, Metal dynamic-buffer physical versions and transient snapshots, external Model/Material dedicated GPU assets, auxiliary screen modules, CPU model/image caches, unrelated engine subsystems, process-external GPU apps and all truly unobservable VRAM. Adding any owner requires a correct unique-allocation report and documented no-overlap guarantee. Never silently count one resource in two different owner totals.

## Verification actually performed

Performed connected GitHub source inspection of LightSystem fields, per-frame persistent buffers, per-submission GPU-authored allocations, GeometryPool live-page accounting, Renderer3D/Prisma host integration and all affected code/docs files. **49/49 structural source-presence and ordering checks passed** (after correcting one whitespace-only match condition). Verified Scene and GameClient external-memory calls occur before render-size decisions and that the owner aggregator includes each of its four subowners once. These are **not native tests** and do not establish compiler/runtime correctness.

**NOT RUN by user direction to finish all functions first:** native CMake/C++/Metal/GL compilation, CTest, GPU integration, device-loss tests, VRAM profiling and screenshot comparisons. Do not report any as PASS, and do not claim this is production-ready total VRAM measurement.

## Remaining FUNCTIONAL work before the separate validation phase (next Stage 49)

1. Provide independent GPU-memory ownership for **unpooled** dedicated Model VBO/IBO + uploaded Material/texture resources, and external scene render modules if their owners expose nonduplicating metadata. Do not count externally owned textures simply because a Renderer3D draw item borrows them.
2. Add configurable **per-view memory-pressure priorities/quotas** under multiple Scene/Game/host views and memory-budget admission policy for adaptive upscaling, using bounded GPU-fenced retirement only. Prevent an inactive viewport's stale external owner values from skewing its next render-size decision.
3. Add functional renderer-wide allocation/memory tracking only where all owners reliably enumerate resources (or explicit known/unknown categories); do not claim device-total VRAM from lower-bound metadata.
4. Keep complete Metal/GL/Vulkan/GLES-compatible temporal fallback and shader paths behind explicit opt-ins, not production defaults. No tests until the user's requested feature-completion phase ends.
5. Every feature stage gets separate `docs/handoffs/prisma-stage-49.md`, exact branch SHA, API, touched files, actual tests (or NOT RUN), limitations, and next concrete feature action. The independent final build/GPU/test phase starts only after remaining functional owner/quality capabilities are implemented.
6. PR #26 stays Draft; base `feature/universal-entity-inspector-fps`. Do not merge any branch or `main` without explicit user request.

## Exact pull commands to resume work on the feature branch

```bash
git fetch origin
git switch feature/prisma-postfx
git pull --ff-only origin feature/prisma-postfx
```
