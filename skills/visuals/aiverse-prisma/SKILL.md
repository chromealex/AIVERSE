---
name: aiverse-prisma
description: PRISMA — universal AIVERSE post-processing, HDR, tone mapping, bloom, temporal/screen-space effects and camera volumes; migration and agent handoff.
---

# PRISMA — AIVERSE universal image post-processing

**Read first:** `AGENTS.md` (mandatory PRISMA vs SpectraFX ownership matrix), `docs/prisma.md`, `docs/postfx.md`, `docs/hdr.md`, the latest `docs/handoffs/prisma-stage-66.md`.

## Atmosphere workflow

For physical sky, volumetric clouds/god rays and local sphere/OBB fog with runtime
reveal, read [the atmosphere reference](references/atmosphere.md). It maps the
existing native interfaces and distinguishes them from PHYSIS authoring MCP.

## Choose the right owner

- PRISMA owns **image-space processing**: HDR scene resolve, tonemapping, color grading, LUTs, bloom, DoF, motion blur, auto exposure, TAA/FXAA/upscaling, screen-space GI/AO/SSR, camera volumes and full-screen `postfx` Shader Graph domain. `PrismaScreenLighting` is part of PRISMA.
- SpectraFX owns **simulation/presentation of 3D VFX**: emitter graphs, sprite/mesh/ribbon particles, smoke/fire/explosions/trails, ECS FX events. It adds linear HDR pixels to the scene BEFORE PRISMA processes the result.
- Shared Renderer3D/RHI, generic Shader Graph compiler and ECS transport adapters are engine infrastructure, not a second postprocess implementation. **Do not duplicate features or couple them to genres.**

## Native C++ integration

```cpp
#include "engine/render/Prisma.h"
#include "engine/render/PrismaScreenLighting.h"

aiverse::render::Prisma prisma;
prisma.init(device);
prisma.configure(gameManifest["postfx"], mapDocument["postfx"]);
prisma.setQuality(aiverse::render::PrismaQuality::High);

// Optional per-camera blend; currently host-controlled (not auto-wired to ECS):
aiverse::render::PrismaVolumeStack cameraVolumes;
prisma.applyCameraVolumes(cameraVolumes, camera.eye());

// Existing Renderer3D flow: scene, particle emissions, resolve, then UI.
renderer.render(pass, &prisma);
drawSceneVfx();
renderer.resolvePrisma();
// Draw HUD/UI after the image resolve.
```

Use `aiverse::render::PrismaSettings`, `PrismaEffect`, `PrismaType`, `parsePrisma`, `prismaCatalog`, `prismaCostReport` for GPU-free data workflows. Link `AIVERSE::prisma_data` (alias of `aiverse_postfx_data`) for pure data/quality logic; renderer integration uses `aiverse_runtime` as before.

## Serialized camera Post Process Volumes (Stage 08)

Use `engine/render/PrismaVolumeData.h` and `parsePrismaVolumes(game["prismaVolumes"], map["prismaVolumes"], &issues)`. GameClient now applies the merged stack from `game.json` and map data to its current camera every frame. The editor applies it per viewport without rewriting project assets. Volume shapes: global/sphere/axis-aligned box, position, radius/halfExtents, priorities, weight, blendDistance, camera layerMask and `profile` with existing `postfx` effect schema. A matching `id` in the map replaces the game definition; map `replace:true` clears game volumes. Only explicitly authored `hdr`/`enabled` override the base camera's settings. **Per-effect parameters are also selectively blended:** un-authored contrast/saturation/quality/textures remain unchanged when the volume only sets e.g. exposure; see `PrismaVolumeEffectMask` (Stage 09). Native C++ volumes without a mask preserve full-profile semantics. Read `docs/prisma.md` for JSON and known blending restrictions. Tests in `tests/render/prisma_volume.cpp` (CTest `prisma_volume`) must be run before shipping.

## Editor volume authoring (Stage 10)

Open **PRISMA — Постобработка**, choose **Вся игра** or **Эта карта**, then use **PRISMA Volumes** to add a global, sphere or AABB box volume (optionally at Scene camera eye). The panel edits volume transforms, blend/priority/quality masks and nested postfx effects. Checkboxes next to effect parameters control **explicit override vs inherited** values; disabling an override removes the JSON key and preserves global camera style. The shared `makePrismaVolumeJson` factory validates new authored volumes. A native editor/Metal validation is mandatory; currently this UI is committed **but not compiled/run**. See Stage 10 handoff.

## Editor and automation

Existing external identifiers are stable by design: `postfx` JSON key, `postfx_catalog`, `postfx_get`, `postfx_add`, `postfx_set`, `postfx_preset`, `postfx_estimate`, `view_postfx` MCP operations and `postfx` saved panel id. The implementation now lives in `apps/editor/core/PrismaOps.{h,cpp}` and `apps/editor/windows/PrismaWindow.cpp` (title PRISMA). Do not rename persisted schemas or duplicate editor registration.

Common workflow: inspect `postfx_catalog` → choose preset or add effects → check device-tier budget from `postfx_estimate` → compare `view_postfx` tiers and `view_screenshot`. The `postfx` Shader Graph domain is post-processing, not the SpectraFX particle graph.

## GPU-free RenderGraph dependency planning (Stage 07)

Include `engine/render/PrismaRenderGraph.h` from native projects/tooling to declare typed resources and passes, compile a stable topological order, detect cycles, duplicate writers, read/write feedback and invalid resources. The returned `PrismaGraphPlan` describes first/last use and aliasable transient slots; it is **NOT an executable RHI render graph**. External input/output/temporal history are managed by the caller. See `docs/prisma.md` and `tests/render/prisma_graph.cpp`. Do not replace the existing `Prisma::resolve()` sequence until actual backend resource allocation/execution is integrated and GPU-verified.

## Opt-in full-resolution temporal resolve (Stage 42)

The canonical engine path is `Prisma::resolve()`, **not** a second
postprocessing implementation inside `PrismaScreenLighting`. The
`temporalResolve` effect is in the normal `postfx.effects` catalog and
shares quality profiles, Camera Volumes and data-only parsers. It is
**inactive by default**, even when configured, until the native host sets
`AIVERSE_PRISMA_TEMPORAL=1` and uses HDR + High quality.

Example authored game/map effect:

```json
{"postfx":{"effects":[{"type":"temporalResolve","historyWeight":0.86,
"depthThreshold":0.0075,"motionScale":0.04,"reactiveStrength":0.95,
"minQuality":"high"}]}}
```

It uses camera + optional object velocity (current-minus-previous UV, signed
RGBA16F), previous per-camera HDR color/depth ping-pong, 3×3 current-color
neighborhood clipping, four independently depth-validated nearest history
samples and optional current-frame reactive particle coverage. One velocity
pass is shared with opt-in Motion Blur. TAA runs before the blur, Bloom and
final HDR tonemap. HDR FXAA is skipped when temporal actually resolved to
prevent two spatial/temporal AA filters on the same frame. Calling
`prisma.resetMotionHistory(viewId)` after a cut invalidates **both** motion
and temporal history. `retireView(viewId)` fence-retires camera-owned
history; owner GPU completion/lifetime rules are unchanged.

Stage 42 alone is native-resolution stabilization; Stage 43 adds optional
jittered subpixel reconstruction, still **not** dynamic-resolution temporal
upscaling or a motion-reconstruction quality certification. It must be validated on Metal/GL/GLES/Vulkan with
moving actors, silhouettes, sky, particle transparency, camera teleports,
dual viewports and pass count. No native compile, CTest or GPU screenshot
was run in the GitHub-only stage. See
`docs/handoffs/prisma-stage-42.md`.

## Native-resolution subpixel sampling and confidence (Stage 43)

Stage 43 extends `temporalResolve` with two optional catalog parameters:
`jitterPixels` (0..0.5, default 0.5) and `historyConfidence` (0..1,
default 0.65). Jitter is **separately gated** by
`AIVERSE_PRISMA_JITTER=1` *and* `AIVERSE_PRISMA_TEMPORAL=1` plus an
active High/HDR temporal effect. Jitter never runs for games that have not
explicitly enabled both environment flags.

Before `Renderer3D::beginScene`, call the presentation-only camera API:

```cpp
prisma.setViewId(cameraId);              // stable identity, not resolution
prisma.applyCameraVolumes(volumes, camera.eye());
auto renderCamera = prisma.jitteredCamera(camera, framebufferW, framebufferH);
renderer.beginScene(renderCamera, framebufferW, framebufferH);
// Keep the ORIGINAL camera for picking, camera navigation and HUD layout.
```

`jitteredCamera` uses a per-camera 8-phase Halton (2,3) sequence in
`RTSCamera::jitter` (NDC). The temporal shader samples the current HDR
scene at `stablePixel + currentJitter` and reads prior HDR history at the
reprojected pixel **minus previousJitter**; the persistent history target
always uses the stable output grid. The previous jitter is removed from
motion magnitude when computing history confidence.

The temporal confidence rejection combines per-tap previous-depth matching,
3×3 current-color luminance variance and neighborhood clipping, current
depth discontinuities, object/camera motion and the existing reactive mask.
Invalid/sharp camera cuts seed fresh history; changes to projection type,
FOV or clip planes explicitly reset motion and color history. Scene/Game/
GameClient render copies of camera; input/picking/overlays remain unjittered.
`PrismaStats` and the editor PRISMA panel show last-frame resolve/jitter.

Stage 43 is **native-resolution subpixel temporal AA**, not TSR/FSR/DLSS,
dynamic resolution, jittered shadows or proven reconstruction quality.
Do not compose with the older optional `PrismaScreenLighting` TAA.
No CMake/native compile, CTest or Metal/GL/Vulkan screenshots were run;
verify motion, transparency, silhouettes and subpixel stability before
production. See `docs/handoffs/prisma-stage-43.md`.

## Experimental temporal upscaling (Stage 44)

The **canonical** Prisma owner now has a host-controlled split between the
internal 3D scene resolution and the final output target. It is strictly
opt-in: author `temporalResolve` with `renderScale` in **[0.5,1]** (default
1), choose HDR/High, then set all three environment flags
`AIVERSE_PRISMA_TEMPORAL=1`, `AIVERSE_PRISMA_JITTER=1` and
`AIVERSE_PRISMA_UPSCALE=1`. A 1.0 renderScale or missing flag retains the
original 1:1 render size. The scale is quantized to 1/16 steps to avoid
continuous transient-texture churn across camera volumes.

```cpp
prisma.setViewId(stableViewId);
prisma.applyCameraVolumes(volumes, originalCamera.eye());
auto [renderW, renderH] = prisma.renderSizeForOutput(outputW, outputH);
auto renderCamera = prisma.jitteredCamera(originalCamera, renderW, renderH);
renderer.beginScene(renderCamera, renderW, renderH);
// Draw 3D and particle/reactive data in renderW x renderH.
renderer.render(fullOutputPass, &prisma); // still a full-size output target
drawVfxInto3DScene();
renderer.resolvePrisma();
prisma.clearCameraVolumes();
// Draw HUD and overlays at outputW x outputH with originalCamera.
```

The existing GameClient and editor Scene/Game implementations follow this
contract; renderer projection/jitter, scene depth, motion vectors, camera
blur and other HDR passes operate at the internal size. Before the final
tonemap/composite, the temporal shader writes an **output-size RGBA16F**
per-view history; it uses normalized signed motion vectors independent of
the two resolutions, current RGB/depth at native scene UVs, separately
depth-validated history taps at output pixel spacing and mild compressed-HDR
reconstruction controlled by `reconstructionSharpness` (0..0.5, default
0.15). The final image and UI retain full output resolution. If the
temporal pass fails, the full-output composite can sample the low-res
current frame with ordinary bilinear filtering, but temporary jitter
instability is possible until the next frame.

This is a first experimental **fixed-scale temporal upscaling** path,
NOT a shipping FSR/TSR/DLSS substitute and NOT automatic dynamic
resolution. It is uncompiled and GPU-unverified: evaluate motion
reprojection, thin details, foliage, silhouettes, bloom and transparency,
over/under-allocation, scaling ratios and Metal/GL/Vulkan shader validity.
See `docs/handoffs/prisma-stage-44.md`. Do not enable by default or infer
improved frame rate from source code.

## GPU-fence-gated adaptive scaling and stable fallback (Stage 45)

`engine/render/PrismaAdaptiveResolution.h` adds a CPU-only
`PrismaAdaptiveResolution` hysteresis controller that adjusts render scale
only from **new, completed GPU-frame timings** plus a strictly advancing
`Device::gpuCompletedFence()`. The native `Device::gpuFrameMs()` supplies
an asynchronously completed *whole GPU frame* measurement where supported
(e.g. Metal); unsupported GL/Vulkan/mobile backends may return 0 and must
retain the authored scale. CPU frame times/FPS are NEVER substituted.

The `temporalResolve` effect now provides optional `adaptiveResolution`
(default false), `targetGpuMs` (default 16.6; 4..50 ms) and
`minRenderScale` (default 0.5). It also requires
`AIVERSE_PRISMA_ADAPTIVE=1` along with the Stage 44 trio of
`AIVERSE_PRISMA_TEMPORAL=1`, `AIVERSE_PRISMA_JITTER=1`,
`AIVERSE_PRISMA_UPSCALE=1`. The authored `renderScale` is the upper
bound. Over target by >15% for 12 new completed GPU observations steps
scale down 1/16; <80% of target for 36 samples steps it up; 48 new
completed GPU samples of cooldown prevent oscillation. Each view has
its own state, but timings represent the entire GPU frame, NOT an
isolated viewport.

Before returning a reduced render size, `renderSizeForOutput()` now
**preflights GPU resources** on the graphics thread: valid temporal and
velocity pipelines, fallback composite, full-output HDR history pair and
matching scratch depth, low-res HDR scene/depth/velocity/scratch. Failed
preflight returns the original full dimensions, suppresses jitter for the
view and rate-limits retries (60 calls). Resource ownership stays in
the existing Prisma cache/history and is GPU-fence retired on resize.

If the temporal pass unexpectedly fails *after* jittered geometry has
already been encoded, the existing final composite now samples at
`s + currentJitter` to cancel the projection offset. This is a
stable-space **spatial fallback**, not a substitute for history or an
automatic GPU retry. The shader's `graphFrame.zw` is zero in normal
paths and is used only for fallback sampling. Input/overlay/UI cameras
are unchanged. The editor shows the adaptive-state, current internal
scale, whole-GPU-frame timing and whether jitter compensation was used.

**Do not confuse:** `gpuCompletedFence` is a validity gate for new
asynchronous timings, never permission to release arbitrary resources;
normal Prisma GPU lifetime rules remain unchanged. Profiling and
compilation have NOT RUN in this stage, and neither adaptive frame-rate
improvement nor image-quality superiority has been measured. See
`docs/handoffs/prisma-stage-45.md`.

## Scoped viewport GPU diagnostics and Prisma-memory guard (Stage 46)

The RHI provides three **optional**, nonblocking `gfx::Device` hooks:
`beginGpuScope(tag)`, `endGpuScope()`, and
`takeGpuScopeTimings()`. The bounded scope covers an actual region of GPU
commands between the two calls, not CPU duration. It uses a **separate
result queue** from the existing `timeGpuFrame/takeGpuTimings` benchmark
interface and must not overlap an active full-frame `GL_TIME_ELAPSED`
query. Desktop OpenGL 3.3 on supported macOS/Windows builds uses real GPU
elapsed queries; GL ES/WebGL/Linux fallback, Metal and Vulkan report
unsupported rather than inventing per-viewport timing. Metal's existing
`gpuFrameMs()` remains a whole-command-buffer value, never a viewport
value.

The editor can opt into Scene/Game GPU-region timings with
`AIVERSE_PRISMA_VIEWPORT_GPU_TIMING=1`. It scopes the GPU submission
from `Renderer3D::render` through `resolvePrisma` and the final viewport
pass, then nonblockingly polls only finished scope queries in the
pre-render maintenance window. `Prisma::acceptViewGpuTiming(viewId,ms)`
accepts those **completed/valid** samples from a trusted host;
`viewGpuTimings()` supplies read-only per-view diagnostics. The
per-view adaptive controller prefers a *new scoped measurement* when
available, otherwise falls back to the previously documented confirmed
whole-frame GPU timer; it never substitutes CPU time/FPS. A scope may
include shadows, reflections and FX, but does not measure asset uploads
before `Renderer3D::render`. Render passes with no scope support simply
remain unmeasured.

The existing opt-in `temporalResolve.adaptiveResolution` catalog adds
`maxPrismaMemoryMiB` (32..4096 MiB, default 256). The controller
compares it with the **lower-bound** sum of
`Prisma::memoryStats().liveKnownBytes` plus
`Prisma::retirementStats().knownPendingBytes`, where shared size-cache
resources are counted only once and pending resources are not
double-counted. A known-memory overage can drive a downward 1/16 scale
step after 12 distinct confirmed GPU completions (even when no valid
GPU-ms duration is available). It blocks upward quality promotion while
over budget and still uses the 48-sample cooldown. It never force-releases
resources: actual destruction still requires the existing GPU-fenced
retirement path. This is a **Prisma-only known-memory guard**, NOT total
VRAM usage, device residency, physical driver allocation or a real
VRAM-budget query; opaque/unknown textures are excluded.

Scene/Game scoped timings and memory pressure appear in Stats/Prisma
editor windows, and `PrismaStats` shows the GPU timing source
(scoped/whole), last valid ms, known bytes and pressure. On an unsupported
backend the UI says unavailable, not 0 ms.

These APIs are source-level and **NOT native/GPU tested**. Before shipping,
compile GL33+GLES and Metal/Vulkan, run GPU timing query/camera lifetime
smoke, verify no overlap with other profilers and benchmark consumers,
exercise memory thresholds and test GL context loss/query saturation.
`docs/handoffs/prisma-stage-46.md` is the next-agent source of truth.

## Metal hardware viewport timers and Renderer3D memory accounting (Stage 47)

Stage 47 implements an experimental **real Metal GPU timestamp** backend
for the existing `gfx::Device::beginGpuScope/endGpuScope/takeGpuScopeTimings`
API. It uses `MTLCounterSampleBuffer` from the Metal device's
`MTLCommonCounterSetTimestamp` only if that GPU supports
`MTLCounterSamplingPointAtBlitBoundary` or
`MTLCounterSamplingPointAtStageBoundary`. Scope endpoints are encoded
into tiny blit encoders in the *existing* command buffer, never split into
separate command buffers or taken from the whole-frame
`GPUStartTime/GPUEndTime`. The completed command buffer's two timestamp
samples are resolved in a nonblocking poll; the GPU clock delta is
converted to elapsed time by **two correlated CPU/GPU clock pairs**
(`MTLDevice::sampleTimestamps:gpuTimestamp:`). Impossible/invalid
samples are rejected, never passed off as a valid per-viewport reading.
Scopes are bounded to 64 pending records. GPU fences and existing Metal
frame completion remain unchanged. Devices without supported timestamp
counters return unsupported, so the prior whole-GPU-frame fallback is
still separate and correctly labeled.

The editor's existing opt-in
`AIVERSE_PRISMA_VIEWPORT_GPU_TIMING=1` now covers supported Metal
devices as well as desktop GL33. The editor already scopes Scene/Game
GPU commands and polls completed scope results separately from
whole-frame benchmark timings. **No Vulkan or GLES/WebGL scoped
timing support is claimed.**

`Renderer3D::residentMemoryStats()` is a read-only CPU metadata estimate
for **directly owned** GPU resources: sun shadow map, selection mask,
scene-capture textures, trace placeholders, renderer-owned model
placeholders, per-camera object-motion/reactive buffers and owned
line/selection/indirect buffers. Distinct handles are counted once;
`sceneColor_/sceneDepth_` cache aliases are not counted a second time.
Pending retired object-motion textures appear in a separate byte field,
not in live residency. This does not include external Model/Material
assets, `LightSystem` shadow/reflection resources, persistent scene/
geometry owners, unenumerated GPU driver/shader allocations, or PRISMA
targets. Buffer physical versions/mips can exceed the reported bytes.

The host calls `prisma.setExternalRendererGpuMemory(liveKnownBytes,
pendingKnownBytes)` before `prisma.renderSizeForOutput()`. Editor
Scene/Game and GameClient now supply the direct Renderer3D memory
estimate. Prisma's adaptive threshold `maxPrismaMemoryMiB` keeps its
existing serialized key but compares the **known Prisma +
directly-owned Renderer3D live and pending lower bound** with the
author's threshold. The two owners' resources do not overlap. This
is not a VRAM-capacity query, not a true hard cap, and not justification
to destroy GPU-fenced objects early.

**Stage 47 correctness fix:** a per-view scoped query sequence and the
whole-frame GPU fence are separate token domains. The hysteresis policy
resets its sample counter (NOT render scale) when switching sources.
A scoped sample is stale after more than eight subsequent completed GPU
fence advances without a replacement, allowing a truthful whole-frame
fallback. No duplicate GPU completion is counted as a fresh sample.

Feature guards, game data, ECS simulation and editor ownership remain
as in Stages 41–46. **Native Obj-C++/Metal compilation, counter query
runtime, GPU timing calibration and VRAM profiling have NOT RUN.**
Apple hardware-counter support and GPU calibration must be verified on
real devices before shipping; see `docs/handoffs/prisma-stage-47.md`.

## Cross-owner GPU memory budget (Stage 48)

Stage 48 expands the Stage 47 **source-level** lower-bound memory path
while preserving the established no-double-counting ownership rules.
`engine/render/GpuOwnedMemory.h` defines a read-only
`GpuOwnedMemoryCounter` and `GpuOwnedMemoryStats`. It samples RHI
`textureInfo/bufferInfo` only; it never reads back, waits on,
destroys or transfers GPU objects. It deduplicates exact texture and
buffer handle IDs per owner. Known formats are counted using minimum
bytes/pixel and unrecognized compressed/missing RHI metadata as
**unknown**, never guessed. Per-backend buffer versions and mips can
make actual physical VRAM larger than the known lower bound.

All of these GPU owners now provide a **read-only allocation estimate**:
- `LightSystem::ownedGpuMemoryStats()`: light map and depth, static
  light atlas, spot shadow, planar reflection color/depth, black
  placeholder and light map/halo/beam/decal buffers
- `PersistentGpuSceneUpload::ownedGpuMemoryStats()`: every in-flight
  per-frame GPU scene upload slot, not just the current one
- `GpuSceneCulling::ownedGpuMemoryStats(device)`: each allocated
  per-frame visibility/compute buffer
- `GpuSceneDrawCommands::ownedGpuMemoryStats(device)`: each
  input/output/compaction/visible-count buffer across all in-flight
  GPU submission slots

`Renderer3D::residentMemoryStats()` now includes these **mutually
exclusive owners** alongside its previous shadow/capture/selection/
motion buffers. Per-owner known byte subtotals allow the editor to
explain the total. Borrowed geometry/model/material buffers are
explicitly excluded. The global `GlobalGeometryPool` lives outside
Renderer3D; its `geometryPoolStats(device).allocatedBytes` counts
each shared RHI geometry page once, independent of how many Models
reference it. Geometry pages are NOT also enumerated as independent
Renderer3D buffers.

The host sets both sources **before** requesting render resolution:

```cpp
const auto owned = renderer.residentMemoryStats();
prisma.setExternalRendererGpuMemory(
    owned.liveTextureKnownBytes + owned.liveBufferKnownBytes,
    owned.pendingMotionKnownBytes);
prisma.setExternalGeometryPoolGpuMemory(
    geometryPoolStats(device).allocatedBytes);
auto [renderW, renderH] = prisma.renderSizeForOutput(outputW, outputH);
```

Both Editor Scene/Game and native GameClient use this flow. The existing
`temporalResolve.maxPrismaMemoryMiB` name remains for JSON/editor/MCP
backward compatibility but the Stage 48 threshold now applies to the
known resident/pending lower bound of **Prisma + Renderer3D subowners
+ GlobalGeometryPool pages** (saturating arithmetic). It is NOT a
device-wide VRAM budget or a promise to stay under the threshold.
Other geometry owners, dedicated external model buffers/material
textures, other apps using the same GPU, driver heap alignment and
mips remain unobservable. Do not double count those when new owner
telemetry is subsequently added.

**Work sequencing:** finish remaining functional owners, integration,
fallback and capabilities **before** beginning the separate build/
GPU validation/repair phase, per user instruction. After each feature
stage, commit a dedicated handoff. Stage 48 source was structurally
inspected, not natively compiled or GPU-tested. See
`docs/handoffs/prisma-stage-48.md` for the complete owner boundary
and subsequent feature scope.

## Dedicated model assets and per-viewport admission (Stage 49)

The current renderer/memory report is a **strictly owned, lower-bound GPU
inventory**. Stage 49 extends the Stage 48 owner tree to model assets
without counting shared geometry pages or borrowed material images twice:

- `appendModelOwnedGpuMemory(Model, GpuOwnedMemoryCounter&)` counts
  dedicated Model VBO/IBO **only when `geometryPoolPage` is empty**,
  nonborrowed `Image.tex`, and owned morph/baked-animation textures.
  Its ownership follows the real `releaseModel()` contract.
- `ModelTextureCache::appendOwnedGpuMemory(counter)` enumerates cached
  model/material PBR textures once from the cache owner. Models borrowing
  this cache set `Image.borrowedTexture=true`; do not count them again.
- `RtsView::ownedModelGpuMemoryStats(device)` aggregates those GPU-owned
  models from its unique model cache and cached material textures, with
  shared handle de-duplication across models.
- `TextureCache::ownedGpuMemoryStats()` independently enumerates
  sprite, generated and atlas-page textures, de-duplicating shared pages
  referenced by atlas sprites. It excludes retired/borrowed resources.

`Prisma::setExternalPresentationAssetsGpuMemory(knownBytes)` accepts
the sum of *nonoverlapping* presentation asset owners. Editor Scene/Game
add model resources and their independent FX sprite cache. GameClient
currently adds its RtsView model/material owner (other FX render owners
may publish additional disjoint inventories later). GeometryPool
still uses `setExternalGeometryPoolGpuMemory` and each page counts once
per device, NOT per model. No shader/driver/texture mip assumptions are
made; unknown RHI metadata remains unknown.

**Multiple cameras** can call
`Prisma::setViewQualityPolicy(viewId,{priority,maxTemporalHistoryMiB})`.
Priority 0..255 (0 yields first; 255 yields last) changes ONLY the
memory-pressure hysteresis in adaptive mode, never the ordinary
12-sample GPU-slowdown response. When the device is over the known
memory budget without slow GPU timing, low priority yields after 6
confirmed observations, default/medium after 12, high after 30; the
existing cooldown remains 48 completed observations. An independent
per-camera `maxTemporalHistoryMiB` limit (0 = no limit) considers
**two output-size RGBA16F histories**, i.e. `width*height*16` bytes.
If that amount is over the admission quota, `renderSizeForOutput`
declines the temporal upscale and returns full size, retires old
history through GPU fences, records admission rejection and
`jitteredCamera` explicitly stays unjittered. `resolve` MUST not
allocate that rejected history again. Original UI/picking remains
unshifted. `Prisma::retireView` clears both view policy and history
admission state.

Editor Scene uses a lower-priority policy (64; two-history quota
128 MiB); Editor Game and the native GameClient use 224 with
256 MiB history quota. Custom hosts can supply their own values via
the public genre-independent API. The existing
`maxPrismaMemoryMiB` is the *known* total owner budget, not a hard
VRAM cap or an allocator.

**No tests yet** per project sequencing: code and source readback
first, then standalone build/CTest/backend GPU validation only after
all functions are finished. Every implementation stage requires its
own `docs/handoffs/prisma-stage-66.md`; Stage 49 details are in
`docs/handoffs/prisma-stage-49.md`. Prior stage ownership, Metal/GL
scoped timers, backend fallback and GPU-fenced resource lifetime are
not changed.

## External VFX/terrain/planet/world resource owners and native TAA admission (Stage 50)

Stage 50 extends read-only owner-inventory to **presentation render
modules**, not to gameplay/ECS:

- `ParticleSystem::ownedGpuMemoryStats()` lists ONLY its private
  vertex/index buffers and builtin particle atlas. Graph textures,
  imported effect/mesh textures and `FxTexture` instances are
  **borrowed**, never owned by the particle simulator
- `ClientFx::ownedGpuMemoryStats()` combines independent
  `ParticleSystem` buffer/atlas inventory and its own
  `TextureCache` of resolved effect sprites/atlas pages
- `TerrainRenderer::ownedGpuMemoryStats()` inventories all
  resident terrain chunk LOD VBO/IBOs, water/apron geometry,
  per-cell/vision/heightfield/detail textures and the
  terrain-private `TextureCache` (its layer/cliff refs are borrowed)
- `PlanetRenderer::ownedGpuMemoryStats()` inventories resident
  planet body chunks, per-body vision/detail textures,
  sphere/atmosphere meshes, placeholders and its independent
  material texture cache
- `RtsView::ownedPresentationGpuMemoryStats(device)` combines its
  unique model/material resource report with terrain and planet
  owners even when one is not actively drawn this frame
- `WorldScene::ownedGpuMemoryStats(device)` enumerates unique
  resident GPU models across shared world chunks. It intentionally
  **excludes ModelTextureCache** because that cache may be shared
  with RtsView (GameClient `streamedWorld` uses
  `view_.textureCache()`); the borrowed cache images are already
  counted by their owner, while the WorldScene's unique VBO/IBOs
  and direct model images are counted only once

Host integration is done **before**
`Prisma::renderSizeForOutput(outputW,outputH)`. Editor combines
its `view_.ownedPresentationGpuMemoryStats`, independent
`fxTextures_.ownedGpuMemoryStats()`, and
`gameFx_.ownedGpuMemoryStats()` with saturating addition.
Native GameClient combines `view_.ownedPresentationGpuMemoryStats`,
`fx_.ownedGpuMemoryStats()`, and optional
`worldScene_->ownedGpuMemoryStats()`. This remains a lower-bound
quality-pressure metric and **does not confer ownership or allow
premature resource release**. Separate Renderer3D subowners and
GlobalGeometryPool pages remain as in Stages 47–49.

**Critical native Temporal Resolve fallback fix:** the
`ViewQualityPolicy.maxTemporalHistoryMiB` cap now applies to
*all active HDR Temporal Resolve histories*, including full-resolution
Stage 42 stabilization **without** subpixel jitter/upscale, rather
than only scaling modes. For opt-in jittered native 1:1 presentation,
`renderSizeForOutput` also preflights two output HDR histories,
scene/depth, velocity and scratch before `jitteredCamera` can shift
the 3D projection. If preflight fails, jitter is rejected for the view
and `Prisma::resolve` does not bypass the rejection by reallocating
that history. Failed preflight retries remain rate limited by the
Stage 45 cooldown; no new GPU wait or force-destroy is introduced.
Normal non-temporal renderers and effects are unchanged.

**Explicit user sequencing:** continue implementing missing features
first; *do not* run CMake, CTest, Metal/GL/Vulkan/GLES/WebGL GPU
validation until the entire functional backlog is closed.
`docs/handoffs/prisma-stage-50.md` records exact files, commits,
actual structural checks, caveats and feature Stage 51.

## Cross-module owner closure, runtime support report and temporal conflict policy (Stage 51)

Stage 51 extends `RtsView::ownedPresentationGpuMemoryStats(device)`
with graphics-owner read-only GPU resource reporting by **real owners**:
`FogOfWar` (private sheet VB/IB/texture),
`RtsFieldView` (field sheet buffers and per-field textures),
`RtsOverlays` (owned overlay buffers/white texture and its texture cache),
`RtsGroundFx` (owned mesh buffers, white placeholder and texture cache),
`Rts2DView` (sprite-batch VB/IB/casters, tile mesh and image cache),
`RtsRoadView` and `RtsShoreView` (resident kit images,
unique owned model VBO/IBOs, road decal meshes and road texture cache),
`RtsPlanetGroundFx` (owned tile/lane meshes),
`RtsGroundCanvas` (owned sprite batch and texture cache),
`RtsPaintLayers` (paint canvas, coastline atlas and owned mesh buffers).
`SpriteBatch::ownedGpuMemoryStats()` explicitly excludes borrowed
`TextureRef` images and static caller-owned `SpriteMesh` buffers,
which those callers enumerate themselves. All these queries use only
`GpuOwnedMemoryCounter` RHI metadata and do not read back or change GPU
resources. Shader/pipeline allocations, driver overhead, uncounted
mip/physical buffer versions and other unrelated owners remain unknown.

**Temporal requested/capability/actual diagnostic:**
`Prisma::runtimeCapabilities() const` is a pure read-only report of
device health, RGBA16F HDR format support, effective HDR request,
authored+flagged Temporal Resolve, whether temporal/velocity shader
pipeline handles are prepared, requested jitter/upscaling/adaptive
options, and successful last-frame HDR/TAA/jitter/upscale/reactive usage.
`fallbackReason` names a concrete missing requirement or failed
history admission/preflight where available. The editor's existing
PRISMA panel shows requested vs supported/prepared vs actually
performed, never claims that a pipeline prepared flag is a successful
real GPU execution. The report is per Prisma owner, with last-frame
statistics of the latest render; it does not allocate textures/pipelines.

**Legacy temporal owner conflict:** `Prisma::canonicalTemporalRequested()`
returns true when authored canonical HDR Temporal Resolve is requested
and High quality is effective, independently of whether its lazy GPU
pipeline has compiled. In that case the optional historical
`PrismaScreenLighting` wrapper MUST skip its own TAA/history passes
and route its camera-jitter request to `Prisma::jitteredCamera` instead.
No simultaneous accumulation with different history and jitter
sequences. **Stage 52** restores the independent linear-HDR
bounce/volumetric passes before canonical Prisma resolve, but never
runs the old temporal pass with the canonical one. Use the separate
alpha-preserving composite shader, not the old TAA copy whose history
alpha stores depth. The wrapper's `temporalShadowResolve()` is true
ONLY while its legacy TAA is the actual owner; `lightingOnly()`
indicates a non-temporal HDR-lighting path. Canonical mode allocates
no second history/depth images; old allocations are retained across
mode switches until adapter resize/shutdown (GPU-fence retirement
for that historical adapter still requires separate validation).
Volumetric noise phase is deterministic without wrapper history,
including canonical fallback frames. See
`docs/handoffs/prisma-stage-52.md` for actual code commits and checks.

**Custom camera lifecycle:** `Prisma::retireUnregisteredViews(liveViewIds)`
lets a host pass exactly its active stable camera identities at the END
of the frame. The routine unions tracked exposure, temporal, quality,
motion, scoped timing and size-reference IDs and calls the existing
`retireView` for every identity not in the live list. It ONLY retires
per-camera allocations by real GPU fence and leaves size-shared caches
to `retireUnreferencedSizeCaches`. Hosts must separately retire
Renderer3D object-motion view targets, as the editor already does for
Scene/Game. Editor calls the generic Prisma live-view cleanup after
its existing Scene/Game close transition. There is no guessed CPU-frame
or implicit GPU idle.

**User instruction:** complete functional stages before launching
native build/CTest/GPU/performance validation. Stage 52 source was
inspected with 15/15 static checks only; never report compilation
or GPU runtime PASS. Continue from
`docs/handoffs/prisma-stage-52.md` and keep Draft PR #26 untouched
until explicit merge approval.

## Stage 53 — read-only per-view postprocess execution matrix

`Prisma::effectReport(stableViewId) const` and
`Prisma::effectReportViewIds()` expose snapshots collected AFTER each
camera's `resolve()`, so a Scene/Game/custom view cannot inherit another
camera's last-run effects. `ViewEffectReport.sequence` is local to that
camera, and `EffectExecution` distinguishes **authored**, **requested**
(enabled/tier/opt-in), **pipelinePrepared** (cached RHI handle only),
**encoded** (a contribution's GPU draw command submitted) and a
human-readable fallback reason. Shader preparation is NOT execution;
encoded is NOT a GPU fence/screenshot/pixel test. Effect rows cover the
resolved profile including disabled and tier-skipped entries.

The **PRISMA — Постобработка** editor window displays last-known
snapshots grouped by stable camera ID; querying these snapshots MUST
never compile programs, allocate render targets, poll GPU or block.
`retireView()` and `retireUnregisteredViews()` drop orphaned snapshots;
`setSettings()` / `shutdown()` clear stale diagnostics. A cached pointer
returned by `effectReport` is invalid after owner mutations: copy what
you need before another view resolve or profile change. Camera IDs must
remain stable across live frames, never be inferred from render sizes.
Do not confuse current `runtimeCapabilities().*LastFrame` (latest
Prisma resolve overall) with the separate per-view effect snapshots.

Stage 53: **18/18 GitHub source-presence/call-order/lifecycle checks
only**; no native compiler, editor launch, CTest, GPU execution or
image parity was run. See `docs/handoffs/prisma-stage-53.md`.
Next functionality: independent Renderer3D object-motion camera cleanup
and GPU-fenced deferred teardown of the historical PrismaScreenLighting
adapter. Keep Draft PR #26, do not merge to main without authorization.

## Stage 54 — independent camera motion retirement and optional lighting lifetime

`Renderer3D::retireUnregisteredObjectMotionViews(liveViewIds)`
retains ONLY IDs supplied by the host after all per-camera render
submissions. Every absent stable view ID is retired through the
existing object-motion queue using `Device::gpuSubmissionFence()`.
The owner collects textures only after verified
`Device::gpuCompletedFence()`. If no reliable backend fence exists,
targets are quarantined until a separately guaranteed GPU-idle drain;
NEVER use CPU `frameSerial`, wall time, frame count or size to
infer completion. This object-motion queue belongs to Renderer3D,
**not** `Prisma::retireView` or the Prisma size-sharing cache.

The editor passes the exact same Scene/Game live camera IDs to
`Prisma::retireUnregisteredViews` and
`Renderer3D::retireUnregisteredObjectMotionViews`, then calls
`Prisma::retireUnreferencedSizeCaches` and completion collectors.
Native GameClient similarly retires both cameras' private history
when displaying menu/asset/map loading frames and retains camera ID
3 during actual 3D rendering. For custom camera hosts implement the
same explicit live-ID roster; never invent a hidden-view timeout.

The optional historical `PrismaScreenLighting` adapter now has its
OWN independent `PrismaGpuRetirementQueue` for bounce, scatter,
legacy history[2], and private depth. At resize/disable its
`releaseTargets` fences these handles instead of directly destroying
in-flight GPU textures. `collectGpuRetirements()` is nonblocking
and polls only the backend completion token;
`retirementStats()` is read-only and
`drainRetiredAfterGpuIdle()` must be called ONLY after explicit
GPU-idle synchronization. `shutdown()` also REQUIRES GPU idle
for shaders/pipelines/buffers. It NEVER owns Prisma's shared scene
targets, velocity or canonical TAA histories.

Stage 54 was structurally inspected via GitHub readback
(**30/30 static presence/lifecycle checks**). CMake, compiler,
CTest, actual RHI completion, screenshots and benchmarks remain
NOT RUN. Handoff: `docs/handoffs/prisma-stage-54.md`.
Complete remaining functional catalog/editor work first; retain
Draft PR #26 and never merge main without user authorization.

## Stage 55 — no collision between postfx Shader Graph and Auto Exposure

For HDR High + explicitly authored/opted-in Auto Exposure and a
`postfx` Shader Graph with four project textures, never assume
the old fixed `TEX8` binding. Read
`engine/render/PrismaCompositeTextureLayout.h` first:

- Composite fixed inputs: textures 0..4. The four bounded graph
  texture slots: 5..8. Auto exposure: 8 when 0..3 graph textures
  are used, **9 when four** graph textures are present.
- The same CPU-only constexpr layout MUST drive generation of
  GLSL/MSL sampler declarations, `ShaderDesc.textureSlots`,
  shader preprocessor `PF_AUTO_EXPOSURE_SLOT9`, and runtime
  `Device::setTexture`. Do not hard-code a second conflicting
  exposure binding in the editor or shader builder.
- If a backend rejects the 10-slot variant, retain the old
  no-auto-exposure (but otherwise identical graph) composite
  fallback, then the plain scene fallback if even that fails.
  Never count an unavailable composite as executed.
- The per-camera `ViewEffectReport` includes
  `shaderGraphTextures` and `autoExposureTextureSlot`
  (negative means final composite did not encode exposure).
  The editor reports that captured value, without compiling
  GPU pipelines to inspect it.

This source was read back on GitHub and passed 25/25 static checks.
The RHI/GL/Metal/Vulkan slot implementation was inspected by source
only. **No native C++/GLSL/MSL build, CTest, GPU, visual parity or
CI tests were actually run**. Read exact commits and next features
in `docs/handoffs/prisma-stage-55.md`. Keep PR #26 Draft and
complete functionality before the dedicated verification phase.

## Stage 56 — opt-in metric Depth of Field (camera view-plane focus)

The `depthOfField` catalog retains legacy `focusDepth` and
`focusRange` as device-depth [0,1] parameters. A new optional
`metricFocus` boolean defaults to **false**, preserving old
postfx JSON, MCP, native and PrismaVolume behavior. When enabled,
`focusDistance` is the view-space focus plane in world units
(not radial Euclidean eye distance), `focusRangeMeters` the
sharp interval around that plane, and
`occlusionToleranceMeters` the depth-difference tap rejection
threshold. Defaults are 12, 4 and 0.35 respectively. If a project's
world unit is one metre these values are metres.

The renderer already provides `PostFrame.depthNear/depthFar` and
`orthographic`. For valid camera metadata:
perspective `z=near*far/(far-depth*(far-near))`,
orthographic `z=lerp(near,far,depth)`.
Orthographic clip near may be negative (currently -200..1000),
and perspective near must be positive. If metadata is invalid,
the shader is told to execute its unchanged legacy hardware-depth
path; an encoded-but-degraded `EffectExecution` row records this
fallback for the stable camera ID.

Metric DoF uses the same 8-neighbour RGB filter while sampling
neighbour device-depth taps, rejecting sky and foreground/background
silhouette discontinuities. It preserves the source HDR alpha in
the metric path and samples scene depth through TEX1 **without
attaching it for writes**: use a separate scratch depth target.
The legacy shader branch retains its original CoC, blend/kernel
and alpha semantics. This is not a true lens/bokeh renderer and
cannot restore occluded colors or fully resolve transparency.

`PrismaData.cpp::prismaEffectWork` conservatively changes its
DoF estimate from 10 to 18 pixel-read units when `metricFocus`
is authored. This is not a GPU timing. The editor exposes new
catalog fields through its generic effect/volume editors, explains
their meaning and shows `degraded fallback` when relevant.
`AIVERSE_PRISMA_DOF=1` remains necessary, normal
`minQuality=High` remains the default; explicit project tier
overrides can run DoF in SDR, and the status report should match
actual runtime execution rather than falsely requiring HDR.

Read `docs/handoffs/prisma-stage-56.md` for exact commits and
continuation. Readback confirmed source-level invariants only;
native CMake, shader compiler, GPU parity and CI are NOT RUN.
Continue feature work before the dedicated test phase and leave
Draft PR #26 unmerged without user instruction.

## Stage 57 — robust opt-in HDR Auto Exposure (per camera)

`autoExposure` continues to run only when authored, HDR/High is
effective and `AIVERSE_PRISMA_AUTO_EXPOSURE=1`. Its 1x1 history
remains owned by stable camera view ID, not by shared viewport size.
The weighted log2 luminance meter now accepts optional fields
`highlightRejection` (default 0, max 0.95),
`highlightStartEV` (default 3, threshold in **log2 luminance**),
and `highlightSoftnessEV` (default 2 stops). They are appended
to the same `PrismaData` parameter catalog; existing JSON/MCP
and old volume numeric masks retain their original offsets.

The first exposure mip adjusts each sample's weight by
`1-rejection*smoothstep(start,start+softness,logL)`.
There is a mandatory 5% minimum contribution per sample; never
create an all-zero global meter for an all-bright HDR scene.
When rejection=0 the old meter and spatial `centerWeight`
remain unchanged. The downsample pyramid continues to accumulate
weighted log sums AND weight sums. This is **not** histogram/
percentile metering, automatic local exposure or a sky mask.

Important order: `Prisma::resolve` must detect automatic camera
jumps BEFORE running the exposure luminance pyramid. Reset only
the current `viewId` exposure, motion and temporal validity on
an automatic camera cut. An explicit `resetMotionHistory(id)`
also invalidates `resetAutoExposure(id)`; reset-all handles
both maps. Histories remain allocated and GPU-fence owned.
No CPU-frame guess or destructive sync is added.

HDR pipeline: sample the pre-tone-map linear SceneColor, apply one
GPU-adapted 1x1 auto-exposure factor at the final composite
(before ACES/sRGB), then the standard display-space grade,
LUT and postfx Shader Graph. ColorGrade's authored exposure
is an additional controlled HDR multiplier, not a second
auto-exposure history. Preserve the Stage 55 four-graph-textures
sampler layout (exposure at 8 or 9). If GPU composite fallback
omits exposure, do not claim it ran.

`Prisma::ViewEffectReport.autoExposureHighlightRejection` describes
the strength actually selected in the FINAL encoded composite,
zero if omitted; the editor shows it only alongside an active
Auto Exposure slot. This is an issued draw status, not a GPU
completion/readback. The effect editor also explains the new
opt-in thresholds.

**Verification:** 31/31 static readback/source checks; CMake,
compiler, CTest, GPU, shader compiler, editor runtime and CI
**NOT RUN**. See `docs/handoffs/prisma-stage-57.md` for exact
commits, limitations and the next feature. Keep the draft PR
unmerged until expressly instructed otherwise.

## Stage 58 — cinematic exposure lock and per-camera area metering

The existing `PrismaType::AutoExposure` GPU owner now supports
optional `freezeExposure` and circular `meterRegionEnabled`.
Both default to FALSE; old JSON, color order and existing
`AIVERSE_PRISMA_AUTO_EXPOSURE=1`/HDR quality gates remain
unchanged. Newly appended catalog fields:
`meterCenterX/Y` (normalized bottom-up screen UV, defaults .5/.5),
`meterRadius` (.25 frame-height units), `meterFeather` (.1),
and `meterOutsideWeight` (.05, clamped minimum .02).
These inherit through existing profile/volumes and automatically
show in the generic editor/MCP parameter catalog.

Cinematic native projects can override an individual camera ID:
`setViewExposureFreeze(id, bool)` /
`clearViewExposureFreeze(id)`;
`setViewExposureMeterRegion(id, ExposureMeterRegion)` /
`clearViewExposureMeterRegion(id)`.
An explicit false override suppresses authored freeze/region true.
The region setter clamps finite values, including outside weight.
Overrides are presentation-only, NEVER deterministic ECS state.
They are removed on `retireView(id)`, in
`retireUnregisteredViews(liveIds)` and at shutdown; hosts MUST
send the full live view roster. Two views may be colocated and
share resolution without sharing exposure or override state.

Freeze only reuses `ExposureView.history[current]` if the
camera's 1×1 GPU texture is valid and history is known valid.
It skips ALL luminance pyramid + adapt GPU draws, keeps the
previous exposure factor and refreshes time to avoid a large
delta on unfreeze. Cold cameras/explicit or auto Camera Cuts
invalidate history and reseed once from the current scene,
then resume freezing. There is no CPU readback or new GPU
target. Do NOT reuse an invalid history or double-bind the
same texture as both color attachment and sampler.

Area metering changes only `kExposureLog` via
`ExposureMeterU {px,p0,sun,region}`. All other shader UBOs
remain the original `SimpleU`. Region.z<0 bypasses the
new weighting completely; the shader transforms sampled UVs
using `TUV(q)` for backend Y-flip parity and normalizes X
by the output aspect ratio. It combines the new smooth
inside/outside circle with previous center-weight and
bright-outlier weight, with guaranteed nonzero sample
contribution. The 1×1 exposure and Stage 55 Auto Exposure
slot 8/9 coexist with Shader Graph unchanged.

`ViewEffectReport.autoExposureFrozen` and
`autoExposureRegionMetered` reflect what the last FINAL
encoded composite actually did, not requested settings:
frozen=true only when reusing the old history without a meter,
regionMetered=true only when producing fresh exposure with
a region. Both false when the exposure composite fell back
or was disabled. Editor renders these values per stable
camera without creating shader pipelines.

**Verification:** 36/36 GitHub source/call-order/lifecycle checks
only. **NOT RUN:** C++/Obj-C++ compile, shader compilation,
CTest, native editor, Metal/GL/Vulkan/GLES/WebGL, pixel parity,
GPU fence and performance checks. Complete remaining functional
stages before test phase; see
`docs/handoffs/prisma-stage-58.md`. PR #26 stays Draft;
do not merge to main without explicit user authorization.

## Stage 59 — fixed cinematic EV, box metering and AE failure gates

The `autoExposure` effect now supports optional
`manualExposure` (default false) and `manualEV`
(default 0, range −12..+12 stops). Manual EV is NOT
`freezeExposure`: it requires no earlier GPU meter, history,
resource, shader pass or `AIVERSE_PRISMA_AUTO_EXPOSURE=1`.
The authored AutoExposure effect still needs an enabled
PRISMA profile, usable HDR and its quality gate.
Manual mode bypasses the `kExposureLog/Reduce/Adapt`
path and multiplies the existing linear HDR
`CompositeU.filt[2]` by `exp2(manualEV)`. It is evaluated
before ACES/sRGB/display ColorGrade/LUT/Shader Graph,
and it composes multiplicatively with authored
ColorGrade exposure. A plain `composite(0)` fallback
retains this fixed EV; no extra sampler or RenderGraph
ownership was added. Nonfinite native or authored EV
is replaced by 0, and values are bounded to ±12.

For native/cinematic cameras call
`setViewManualExposure(stableViewId,true,ev)`;
`setViewManualExposure(id,false,0)` explicitly
disables profile-authored manual exposure; and
`clearViewManualExposure(id)` re-enables inheritance.
Manual selection invalidates old per-view AE history,
so resuming the automatic mode begins with fresh
scene luminance instead of stale adapted pixels.
All manual override maps are per `viewId` and
included in `retireUnregisteredViews(liveIds)`,
`retireView` and shutdown. Do not invent ECS state
or aggregate by camera dimensions.

Area metering still defaults to the Stage 58 circle.
An optional `meterBox` flag (default false) changes
`kExposureLog` to a soft rectangular ROI with
`meterHalfWidth/Height` (default 0.25 each; normalized
width/height of viewport). The `ExposureMeterRegion`
native struct appends `box`, `halfWidth`, and
`halfHeight` to preserve existing positional
aggregate-initializer compatibility. Native inputs
are finite-clamped. Stage 59 extends ONLY
`ExposureMeterU` with `regionBox` and the associated
`kExposureLog` UBO field count; reduction/adapt
and unrelated shaders retain their old uniforms.
ROI uses backend-correct UVs, aspect-correct soft
rectangle distance and a bounded nonzero outside
sample weight.

The exposure pipeline now checks that the entire
luminance pyramid produced a valid 1×1 target
BEFORE allocating/writing camera history. On any
failed or incomplete reduction, invalidate the
existing exposure history and allow final composite
to omit Auto Exposure; do not sample partial mip
targets as valid exposure. No additional GPU waits
or synchronous disposal.

`ViewEffectReport.manualExposureEncoded`,
`manualExposureEV`, `autoExposureBoxMetered`
are read-only last-encoded camera fields. Only
the true final composite (possibly plain fallback)
may report fixed manual EV; box reporting only
when a fresh ROI exposure was composited.
The editor surfaces these distinctions; prepared
shaders and encoded draws are NOT verified GPU
pixels or completion fences.

Stage 59: **49/49 GitHub source-only structural
checks** after readback; **NOT RUN** CMake,
clang, Obj-C++, shader compilation, CTest,
GPU, editor startup, image parity, FPS and CI.
Read `docs/handoffs/prisma-stage-59.md` for
exact commit IDs, architecture and next work.
Retain PR #26 Draft, do not merge into main
without an explicit user request.

## Stage 60 — preserve ordinary PRISMA when Shader Graph fails

The `Prisma::resolve` final composite must NOT drop working
Bloom, LUT, ColorGrade, Vignette, Grain and other fixed postFX
merely because an optional authored Shader Graph fails shader
compilation, lacks source textures or collides with GPU capabilities.

The CPU fallback policy is now:
(1) graph + optional Auto Exposure,
(2) graph without AE,
(3) **full fixed-effect mask without graph + AE**,
(4) fixed effects without AE,
(5) plain composite(0) only as last resort.

Keep the fixed mask snapshot before attempting graph variants.
Never blindly execute a graph whose image loader returned the
valid `black_` fallback: verify that EACH authored input path
has a truly valid uploaded `graphTextures_[path]` resource.
Missing inputs skip the graph for this resolve while preserving
the ordinary fixed postprocess chain. `graphTexture` now
negative-caches failed GPU upload attempts as well as failed
decode; `Prisma::setImageLoader()` is the existing fence-safe
owner cache reset, not an automatic filesystem watcher.

Recompute `PrismaCompositeTextureLayout::plan` using the
actual FINAL selected pipeline, not the authored graph's
texture count. Stage-55 four-texture graph exposure uses
`TEX9`; a graphless AE fallback uses `TEX8`.
Do NOT bind textures 5..8 if the selected composite has no
graph; do not report graph execution merely because a fixed
pipeline exists. The CPU-only
`ViewEffectReport.shaderGraphFallback` flag and
per-effect `ShaderGraph.reason` record the failure without
forcing GPU resource creation from the read-only editor.
The editor warns but reports individual fixed-effect
`encoded` from the final mask.

LUT decode, incorrect N²×N strip, and texture upload
errors now populate `Prisma::lastError`, retaining the
existing per-path failure cache and allowing other effects.
Cache ownership, shader graph JSON schema, ECS, RHI,
SpectraFX and manual EV paths are unchanged.

Stage 60 verification: **33/33 source-only structural
readback checks**, no CMake/C++/shader compilation,
CTest, Metal/GL/Vulkan/GLES/WebGL GPU smoke, editor
runtime, screenshots, pixel parity or CI. Do NOT claim
hardware execution or production-ready correctness.
Refer to `docs/handoffs/prisma-stage-60.md` for exact
code commits, limitations, tests NOT RUN and next stage.
Keep PR #26 Draft; do not merge main without approval.

## Stage 61 — reactive MotionBlur edges, alpha and fail-safe FXAA

The existing `PrismaType::MotionBlur` catalog appends one optional
float `reactiveTapRejection` (default 0, range 0..1).
It does NOT shift prior numeric offsets or require any new ECS or
GPU history owner. Only the `kMotionBlur` pass adds a fifth
UBO vec4 `tapInfo` (other fullscreen passes remain unchanged).
Its strength is passed to the shader only if a current-frame
borrowed RGBA8 reactive mask has accepted metadata/RHI format
and correct INTERNAL scene dimensions. With strength=0, retain
the original shutter-depth filtering. With strength>0, suppress
neighbor color taps with reactive coverage unlike the center
pixel, complementing the existing depth layer rejection.
Preserve source HDR alpha in the MotionBlur result; the separate
canonical temporal history continues to write device depth into
its own history alpha.

Do NOT infer a usable mask merely from a non-null handle when
size/format metadata rejects it. When metadata is absent and
the backend cannot introspect, the existing legacy borrowing
contract still accepts it; the status must not claim actual GPU
pixel validation. `ViewEffectReport.reactiveInputSupplied`,
`reactiveInputAccepted` and
`motionBlurReactiveEdgesEnabled` are per stable camera ID
diagnostics. The last flag must become true only if MotionBlur
was actually encoded with the accepted reactive input.
The editor exposes this without RHI allocations.

SDR FXAA must preserve the previous `src` when a new
`kAa` color target or its depth attachment cannot be
created. Keep `fxaaInput` and `fxaaOutput` independent,
ensure their handles and depth are valid BEFORE draw, and
only swap `src` to output after a pass was actually encoded.
The graph FXAA path is still opt-in and direct FXAA remains
a fallback if its graph executor cannot complete.
For HDR output FXAA, only render the post-composite pass
when `!temporalResolved` **and** the preceding final
composite pipeline is valid, with target and depth verified.
Do NOT attempt to read an unwritten intermediate texture.

Stage 61 source readback: **40/40 static contract/lifecycle checks**.
**NOT RUN** native CMake/clang, GLSL/MSL/SPIR-V compiler,
CTest, editor/client start, Metal/GL/GLES/Vulkan/WebGL,
HDR alpha/pixel capture or real performance tests.
See `docs/handoffs/prisma-stage-61.md` for exact commits,
source impacts, limitations and next functionality.
Maintain Draft PR #26; never merge to main without approval.

## Stage 62 — current-frame reactive coverage MUST survive a camera cut

`Renderer3D::renderObjectMotionVectors` now admits object velocity
ONLY with valid previous camera/pose history and a non-resized view
(`velocityHistoryReady`) plus RGBA16F support. Do not publish
fabricated current=current or zero-motion velocity textures
on the first frame, on a camera cut/epoch change, or after
viewport resize.

Reactive transparency coverage is a SEPARATE current-pose
producer:
`Renderer3D::renderCurrentFrameReactiveMask(post,history,
sceneDepth,w,h)`. It runs even when object velocity is not
admitted, provided a native post hook requested motion/reactive
inputs and borrowed scene depth is valid. It creates its own
private depth scratch attachment if velocity didn't already
allocate it, and only renders a RGBA8 reactive target when
there is current translucent/dithered geometry or a provider
callback. The scene depth remains a sampled borrowed texture,
not a render attachment or owner. The target's contents are
cleared per pass; no stale previous-mask reuse is allowed.
Rigid, morph/skinned/baked transparent models, immediate water,
particles/custom callbacks all use their CURRENT pose.
`MotionTextureInput::declared(...,RGBA8)` is published only
after a mask pass was encoded, never on absent pipelines.

History for the NEXT frame is updated AFTER the current-frame
reactive draw; on resize, velocity target, private depth and
reactive target all use existing GPU-fenced
`queueObjectMotionTexture`. No CPU frame delay and no
synchronous `destroyTexture`. Maintain stable per-view IDs;
do not merge camera histories by shared resolution.

`Renderer3D::ObjectMotionStats.submitted` remains strictly
the object velocity pass; new `velocitySeedOnly` and
`reactiveSubmitted` distinguish a first-frame seed from
a successful reactive-only mask. `Prisma::ViewEffectReport`
adds `objectVelocityInputAccepted` and
`reactiveWithoutObjectVelocity` (for the CURRENT accepted
borrowed inputs). The editor displays mask-only reactive
behavior; these fields describe pass submission and metadata,
not GPU completion or pixel correctness. Stage 61's
`reactiveTapRejection` continues to require an accepted
mask and an encoded MotionBlur pass.

Static readback checks: **42/42 structural contracts and
2/2 C++ scope-brace checks**, not compilation.
Native CMake, C++/Obj-C++, shader compile, CTest,
Metal/GL/Vulkan/GLES/WebGL, first-frame/cut/resize
screenshots, GPU-fence stress, performance and CI
remain **NOT RUN**. See
`docs/handoffs/prisma-stage-62.md` for exact commits,
limitations and the next step. Keep PR #26 Draft and
do not merge into main without explicit user approval.

## Stage 63 — reactive alpha-cutout / dissolve GPU silhouette and callback lifecycle

`Renderer3D::renderCurrentFrameReactiveMask()` now prepares a
separate rigid `objectMotionReactiveCutoutPipeline_` for
`AlphaMode::Mask` and object-space `dissolve>0`.
The `Renderer3DReactiveShaders.h` cutout GLSL and Metal
variants reproduce material base-alpha/texture-alpha cutoff,
standard KHR UV offset/scale/rotation, vertex alpha/AO
rules, TeamMode::BaseAlpha / VertexAlpha overrides, the
same 4×4 dithered LOD threshold and the ordinary
rigid-model `modelNoise3` dissolve threshold (7 and 19
cycles). Only surviving pixels write RGBA8 reactive R
with borrowed sceneDepth sampled read-only. Renderer3D
owns the separate scratch depth and the mask.

Do NOT use this exact rigid shader for animated vertex
deformation or custom Shader Graph vertex displacement.
The animated/baked reactive coverage paths now replay
the exact CURRENT-frame LOD dither via U1 / MSL buffer(2).
Animated dissolve is conservative silhouette coverage:
the reactive pass does NOT reproduce the deforming
object-space noise and may mark pixels the color shader
discarded. Animated material-specific cutout and
actor-reveal aperture need their own future specialized
path and real GPU validation; do not overclaim parity.

Reactive provider readiness now depends on WHICH
current scene source actually needs which pipeline:
simple rigid, cutout/dissolve rigid, animated skin,
animated baked, immediate MeshVertex water, or
custom callback. Do not publish a valid looking,
but empty mask solely because an unrelated GPU
reactive shader happens to have compiled. Cutout
shader/pipeline handles are owned exclusively by
Renderer3D and destroyed at the host's GPU-idle
shutdown point, just like existing reactive shaders;
per-view target resize still uses GPU-fenced
`queueObjectMotionTexture` rather than immediate
texture destruction.

Callback lifetime is strictly scene-scoped.
Register using `addMotionReactiveDraw()` AFTER
`beginScene()` and BEFORE `resolvePrisma()`.
Callback must only encode draws into the already
open RGBA8 reactive pass; it must not begin/end
its own pass or access destroyed/external resources.
It is not persisted to the next camera/frame.
New `ObjectMotionStats.reactiveCallbacksDropped`
counts callbacks lost because the post hook, scene
depth or a suitable pass is unavailable; existing
`reactiveCallbacks` counts invocations, not verified
GPU draws. `cutoutReactiveDraws` and
`dissolveReactiveDraws` count encoded attempts,
not pixel coverage.

**Actually performed:** readback from GitHub and
51/51 static source/binding/lifetime assertions,
plus lexical brace balance of the modified files.
**NOT RUN:** actual C++ or Metal/GLSL compiler,
CTest, GPU shader execution, visual parity,
cross-camera callback/fence stress, performance,
native Editor/GameClient or CI. Full handoff:
`docs/handoffs/prisma-stage-63.md`. Keep
Draft PR #26 unmerged until user explicitly
approves merging main. Continue remaining bounded
features, then enter the common native test phase.

## Stage 64 — reactive animated alpha/dissolve and actor reveal

Do NOT mutate normal `Renderer3DMotionShaders.h` or the
established object velocity pipeline to achieve reactive material
parity. Stage 64 creates *reactive-only shader variants* via
`reactive_shaders::animatedVertexGlsl(source,baked)` and
`reactive_shaders::animatedMsl(source,baked)`.
These adapt existing skin/morph/baked vertex shaders and preserve
current deformed model-space pose, UV and vertex color alpha.
They deliberately return empty strings when source anchors
change; the render path skips such an invalid pipeline rather
than silently publishing an unshaded mask.

The animated fragment has a 160-byte `ReactiveAnimatedU1`:
`lod,uv0,uv1,maskInfo,world,reveal,revealAxis`.
It occupies normal uniform U1/Metal buffer(2);
existing motion U0 and deformer U3/U4/U5 remain intact.
It uses source base color texture at TEX1 (scene depth
at TEX0), applying UV transform, Material alpha cutoff,
TeamMode::BaseAlpha/VertexAlpha and vertex AO handling,
current-deformed object-space dissolve noise and exact
4×4 LOD dither. This is current-frame mask coverage,
not motion velocity or temporal history.

Actor Reveal is now handled by rigid and animated reactive
variants, but only for the main presentation camera
(`geometryMainView_`), a flagged `kDrawActorReveal`
ModelItem and a nonzero `actorReveal.w`. Renderer3D computes
the same actor center/radius, view-axis and perspective
attenuation as normal ModelShaders; fragment shaders replay
the 4×4 screen dither aperture. Rigid U0 cutout uniforms
now occupy 240 bytes, with world matrix and reveal
vectors added after prior fields. Both shader variants
sample borrowed sceneDepth but write to Renderer3D's
own RGBA8 reactive target and private Depth scratch.

New `ObjectMotionStats.actorRevealReactiveDraws` is
an *encoded CPU draw counter*, not GPU pixel validation.
Existing cutout/dissolve/LOD counters now include
animated variants where appropriate. If custom material
Shader Graph displaces vertices or overrides alpha beyond
ordinary Material settings, provide an explicit native
reactive callback instead of claiming this shader reproduces
an arbitrary custom material. This does not create a
separate postprocess GPU history or ECS state.

**Actual verification:** 61/61 static source/binding checks;
four shader-source generation fixture checks (skin and baked
in GLSL and Metal). **NOT RUN:** CMake/clang, shader
compilers, native Editor/GameClient, CTest, GPU alpha parity,
Metal/GL/GLES/Vulkan/WebGL execution, stress and CI.
See `docs/handoffs/prisma-stage-64.md` for exact commits,
limits, next step and host pull commands.
Draft PR #26 remains unmerged; do not merge main/base
without user's approval.

## Stage 65 — source-level catalog closure and numeric safety

The implemented PRISMA catalog has exactly 19
`PrismaType` entries. The Stage 65 audit cross-checked
all 19 against `PrismaData::buildCatalog`,
`prismaEffectWork` and `Prisma::resolve`
(including per-effect execution status). The engine
Editor Scene/Game and native GameClient use
stable presentation view IDs 1/2/3 and maintain
per-view GPU history/retirement rather than sharing
by resolution. This is **source/API implementation
coverage only**, NOT a verified native renderer.

All authoring paths must reject nonfinite numeric GPU
parameters. `parsePrismaEffect` in strict mode throws
for NaN, infinity or nonnumeric RGB components;
in non-strict mode it reports the issue and retains
the default parameter or all-default RGB triplet.
Use double precision to clamp native Json numbers
BEFORE casting to float, so huge finite numbers
cannot overflow shader values.

For direct C++/ECS profiles (which bypass JSON),
call the GPU-free
`sanitizePrismaSettings(PrismaSettings&)`.
It normalizes missing/extra flattened values,
nonfinite defaults, catalog numeric/color ranges
and booleans. `Prisma::setSettings` already does
this before storing a base profile, and
`PrismaVolumeStack::resolve` does it after
native-volume interpolation so invalid inputs
cannot pollute a live camera's shader uniforms.
Do not implement a second sanitizer in
FPS/RTS/other genres.

**Acceptance/feature manifest:** consult
`docs/prisma-feature-closure-stage-65.md`.
The next phase is ONE consolidated native
CMake/CTest, shader compilation, Metal/GL and
available GLES/Vulkan/WebGL rendering/pixel
parity, GPU fence/memory and performance
verification/fix cycle. Actual outcomes must be
logged in a separate Stage 66 handoff.
CMake registers `prisma_graph`,
`prisma_volume`, the Shader Graph CTest target,
`hdr_policy` (HDR option and desktop only)
and `aiverse_tests`; built-in GPU fixture
binaries may need explicit invocations.

**Stage 65 actual checks:** 19/19 catalog mappings
and 36/36 static source/host/test registrations.
**NOT RUN:** any native compile, shader compiler,
CTest, GPU runtime, screenshots, perf or CI.
See `docs/handoffs/prisma-stage-65.md`.
Keep PR #26 Draft and main unchanged; never merge
without explicit user approval.

## Stage 66 — consolidate native validation, never invent GPU pass

Stage 65's 19/19 PRISMA catalog closure is SOURCE-LEVEL ONLY.
Stage 66 installs a real GitHub Actions workflow
`.github/workflows/prisma-final-validation.yml` for the
feature branch. The workflow configures CMake with
`AIVERSE_BUILD_TESTS=ON`, `AIVERSE_BUILD_HDR_TESTS=ON`,
`AIVERSE_BUILD_EDITOR=ON`, builds all native targets,
runs ONE full `ctest --output-on-failure`, then attempts
the available native HDR test fixture (Linux Xvfb/Mesa
`aiverse_hdr_scene`, macOS OpenGL `aiverse_hdr_gl`).
Per-OS configure/build/CTest/GPU logs and any HDR capture
are uploaded with `if: always()`. Workflow does not
pretend a remote hosted runner is the user's physical
Metal-capable Mac Studio.

A GPU-free test expansion in
`tests/render/prisma_volume.cpp` checks Stage 65
`sanitizePrismaSettings`: nonnumeric RGB elements,
strict exception vs non-strict default triplet,
native NaN/Inf fallback, malformed flattened profile
length, excess values, large finite number clamp,
and bool normalization. The test is compiled with
the existing `prisma_volume` target, not a second
postprocessor or test-only GPU rendering owner.

**Current run reference (supersedes older cancelled runs):**
https://github.com/chromealex/AIVERSE/actions/runs/44597082684
for source/tests commit `aa262d6b44546985429c0cda0abf0338f270c693`.
GitHub queued Linux/macOS; native compile/CTest/GPU
outcomes have NOT been observed yet. Earlier runs
44595169973 and 44595377866 were cancelled by
concurrency after test source updates. Never report
PASS/FAIL for a queued or cancelled job. Fetch
the newest run's jobs/steps/logs/artifacts before
updating the Stage-66 handoff.

The existing `prisma_graph` CTest also now checks
reactiveShaderVariantContract (actual four generated
GLSL/MSL skin/morph and baked shader strings, including
fail-closed anchor changes and current UV/alpha)
and compositeTextureLayoutContract (0–4 bounded
graph input textures with/without Auto Exposure,
TEX8/TEX9 slot ownership). They compile/run ONLY
after the queued runner actually executes.
Report each actual error and fix it in the feature branch;
re-run consolidated checks until actual pass.

The GitHub workflow triggers when PRISMA/Editor/Client
source or its workflow definition changes, not when a
handoff document changes. The existing
`docs/prisma-feature-closure-stage-65.md` details
coverage and required per-backend visual cases.
Use `docs/handoffs/prisma-stage-66.md` as the current
validation handoff, updating its real results when known.
No merge to main/base without explicit user permission.

## Source tree and compatibility

- Canonical core: `engine/render/Prisma.{h,cpp}`, `PrismaData.{h,cpp}`, `PrismaShaders.h`, `PrismaVolumes.h`.
- Temporal/screen-space: `PrismaScreenLighting.{h,cpp}`, `PrismaScreenLightingShaders.h`.
- Legacy `PostFx*.h`, `ScreenLighting.h` and old ops header are compatibility-only; legacy .cpp shims have no definitions. New engine changes MUST go in PRISMA files.
- The volume stack remains GPU-free. The main PRISMA chain includes **source-level, experimental** Auto Exposure, DoF, camera/object motion blur, Stages 42–43 native temporal resolve/jitter, Stage 44 temporal upscaling and Stage 45 opt-in **whole-GPU-frame adaptive scale**. RenderGraph execution is partially opt-in; a production-grade dynamic resolution/TSR/FSR system is **not** implemented. Native builds and GPU parity tests remain unrun for these stages; never label them verified or production-ready.

## Mandatory validation and handoff

Run `./run-tests.sh --quick` plus targeted HDR policy/GPU pixel tests once a native checkout is available; on macOS build `aiverse_hdr_policy`, `aiverse_hdr_scene`, `aiverse_hdr_gl`, editor/client and check the Metal path. Report actual commands and PASS/FAIL/NOT RUN, never infer success from source files or queued CI.

After **each** bounded code stage, commit `docs/handoffs/prisma-stage-66.md` on the same branch: exact SHA, architecture and API, all changed paths, actual tests/results, limitations, and the next actionable step. Keep PRISMA handoffs separate from `docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md` maintained by SpectraFX.


## Optional draining water on lens

Use the independent `lensWater` effect; see `docs/prisma-lens-water.md`. It is
OFF when absent, supports enabled/amount plus dropSize, density, distortion,
flowSpeed, dryTime and film, and must not be coupled to underwater absorption.
NEREID supplies camera immersion; PRISMA owns per-view wetness and composite.
Update setUnderwater every frame after setViewId/profile resolution and retire
the view when destroying a camera. Do not implement lens droplets in NEREID,
Terra, SpectraFX or a scene-specific shader. Verify actual encoded pixels plus
on/off, dry camera and exit/dry-time behavior; compilation alone is insufficient.
