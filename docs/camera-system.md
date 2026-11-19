# AIVERSE LENS — genre-independent ECS cameras and multi-camera capture
Date: 2026-11-19
Branch: `feature/universal-multicamera-render-targets`

> **Status: PARTIALLY IMPLEMENTED, NOT BUILD-VALIDATED.** Stage 1 renamed `render::Camera`; Stage 2 extracted `render::PortraitRenderer`; Stage 3 added native ECS camera components and read-only snapshots. Scheduling, output allocation, RenderGraph integration and all P1/P2 features below are still proposals. See the dated handoffs; keep common engine code genre-independent.

## Stage 4–5 implementation (2026-11-19)

The reusable core has now been committed in the feature branch (NOT hardware-validated):

- `engine/render/CameraRenderScheduler.h`: ECS entity-driven scheduling with priorities, dependency DAG, duplicate-output and cycle diagnostics; `EveryFrame`, `EveryNFrames`, `FixedRate`, `OnDirty`, `Manual`. The highest-priority screen camera is last, after offscreen cameras. Until split-screen is implemented, additional full-screen cameras are suppressed.
- `engine/render/RenderTargetPool.h`: on-demand RGBA8/RGBA16F and depth attachment allocation by asset key; lazy resize; completion-serial deferred retirement; unknown completion retains targets until confirmed GPU-idle teardown.
- `engine/render/CameraRenderPipeline.h`: executes `CameraRenderPlan` into `Renderer3D::render(gfx::PassDesc)`, lets the host submit scene geometry and supply per-camera postprocess instances, and exposes sampleable color/depth textures.
- CTest source fixtures (NOT RUN): `ecs_camera`, `camera_scheduler`, `camera_rt_pool`. GPU pixel tests and full editor/native integration remain pending.

### Native C++ host example

```cpp
render::CameraRenderScheduler schedule;
render::CameraRenderPipeline lens(device, renderer, schedule);
// The host created ECS camera Entities with WorldTransform and CameraComponent.
// CameraOutputComponent{Texture, 42, 512, 512} chooses a stable RT asset key.
// All rendered models are submitted by the game's presentation callback.
device.beginFrame(framebufferWidth, framebufferHeight);
auto result = lens.execute(world, frameIndex, seconds,
    [&](const render::EcsCameraView& camera, render::Renderer3D& r3d) {
        // Submit your world geometry here; respect camera.visibilityMask.
        // Never sample camera.output.targetAssetKey in this same pass.
        submitVisibleWorld(camera, r3d);
    });
auto cameraColor = lens.targets().color(42); // valid after its successful pass
device.endFrame();
// At an externally proven GPU-idle point before Device teardown:
// lens.shutdownAfterGpuIdle();
```

The native example is explanatory, not a buildable standalone project: `submitVisibleWorld`,
`world`, `renderer`, and `device` are owned by the game. A target's `depth` flag
controls whether depth is externally exposed; the internal depth attachment is always
allocated to keep 3D depth testing correct. Do not read a camera target while the same
camera writes it. Declare RT dependencies using `CameraDependenciesComponent`.
For per-camera PostFX, retain a distinct `PostProcess` instance per Entity and call
`executeWithPost`.

## 1. Requirements
1. An arbitrary number of cameras simultaneously present in one scene, each defined by a native ECS `Entity` (existing generation-safe identity), with normal scene hierarchy `Parent`, `LocalTransform`, and `WorldTransform`.
2. Each camera can render to swapchain, external or managed render texture (color/depth, HDR), viewport sub-rectangle, or an offscreen capture; independent clear/load/store and layer masks.
3. A camera need not be active on screen to capture. A sampleable RT remains valid until the host explicitly releases it; GPU lifetime and inter-frame ownership must be respected.
4. Scheduled capture: every frame, fixed Hz, every N frames, dirty-driven, once, manual. Priorities and per-frame budgets must not starve essential output.
5. The same simulation snapshot can be viewed through many cameras. No genre-specific world, input or render logic is allowed in the camera infrastructure.
6. Camera stack: base/overlay, split-screen and picture-in-picture; explicit composition and clear semantics.
7. Independent temporal effects (TAA/SSR/SSAO, motion vectors, auto-exposure) and jitter/history per camera and output size. Camera cuts, teleports, dynamic resize and disabled captures invalidate histories.
8. No read/write feedback loops: a render pass may not sample the image being written. Capture dependencies form a DAG; reject cycles or use explicitly documented previous-frame history.
9. Avoid duplicating immutable geometry uploads per camera. Plan shadow, culling and LOD reuse without sharing view-dependent visibility incorrectly.

## 2. Existing implementation to preserve
- `engine/render/Math3D.h`: new `render::Camera` value type (formerly `RTSCamera`) with target/distance/orbit, perspective/orthographic, near/far and jitter.
- `Renderer3D::beginScene(camera, w, h) -> draw* -> render(gfx::PassDesc, PostProcess*) -> [resolvePostFx()] -> gfx::Device::endPass()`.
- `gfx::PassDesc` already supports separate `colorTarget` / `depthTarget` textures.
- `apps/editor/Editor.h::Viewport` already contains independent render-target pair for Scene/Game windows.
- `engine/render/PortraitRenderer.h` is a genre-free offscreen Renderer3D + model-pose capture host (Stage 2); `modules/rts/view/RtsPortrait.cpp` is its RTS-specific adapter.
- `engine/render/PostFx`, `ScreenLighting`, `Lights`, existing Metal, GL, Vulkan RHI are reused rather than replaced.
- Shared `aiverselib/gameplay/Camera.h` is a camera controller helper, not a render/capture scheduler; avoid conflating the concepts.

## 3. Implemented foundations and proposed modules
- **Implemented Stage 3:** `engine/sim/CameraComponents.h` — native ECS `CameraComponent`, `CameraOutputComponent` and `CameraUpdateComponent` with fixed-point lens values and stable output asset keys, never RHI handles. All authored cameras are ECS entities in `World`; `SceneHierarchy` owns their transforms.
- **Implemented Stage 3:** `engine/render/EcsCameraRender.h` — read-only `visitEcsCameras` / `resolveEcsCamera` after hierarchy update; creates transient `EcsCameraView`/`render::Camera` snapshots with full positional/rotational pose (including roll).
- **Planned:** `engine/camera/CameraPresentationState.h/.cpp` — *not* another entity registry; keeps non-authoritative GPU/temporal/update bookkeeping keyed by `(World identity, Entity::packed(), view variant)`. Entity generation must be checked against `World` before every use. Transient native C++ callers may submit `RenderView` directly without creating a persistent camera entity.
- **Planned:** `CameraTargetComponent` / `CameraRigComponent` and explicit stable persistence schema registration; defaults are not auto-persisted without `PersistentEcs` registration. Cameras do not own simulation time.
- `engine/camera/CameraDirector.h/.cpp` — active view selection, priorities, virtual cameras, cuts and interpolated blends.
- `engine/camera/CameraRig.h/.cpp` — orthographic/perspective targets, follow, look-at, orbit, free-fly, spring arm, rail/dolly, constraints and deterministic damping.
- **Implemented Stage 4 (CPU):** `engine/render/CameraRenderScheduler.h` — capture requests, ordering and dependency/cycle diagnostics, basic update policies. GPU time budgeting and per-view profiling remain planned.
- `engine/render/CameraView.h` — immutable `RenderView` snapshot (Camera, viewport, output, masks, clip overrides, quality, temporal key, clear).
- **Implemented Stage 5:** `engine/render/RenderTargetPool.h` — managed color/depth, HDR, resize and GPU-serial deferred destruction. MSAA, format negotiation, asset persistence and host integration remain planned.
- **Implemented Stage 5:** `engine/render/CameraRenderPipeline.h` — offscreen and primary screen camera execution using existing Renderer3D, GPU target readiness and dependency checks. It is opt-in by native code; not yet wired to Studio cameras or game clients.
- `engine/render/PortraitRenderer.h/.cpp` — purely model/pose/material preview renderer without `RtsView`, `UnitType` or `modules/rts` includes. RTS stays a thin adapter and other hosts call directly.
- `apps/editor/windows/CameraWindow.cpp` — per-entity camera inspector, selection, live preview/RT visualization, capture flags and instrumentation.
- `engine/camera/CaptureAsset.h` (later) — named RT assets exposed to materials, UI and scripts without raw transient texture handles in save data.

### Output types
- `Screen` (swapchain); `Texture` (color/depth/HDR RT); `Viewport` (screen subrect); `Capture` (one-shot color/depth); `Cubemap` (six views, future).
- Do not introduce `RTS`, `FPS`, `Editor`, `Genre` as shared classes or enum cases.

### Scheduling and cycle examples
- `EveryFrame` = render each submitted frame; `Manual` = capture on request; `FixedHz` = rate-limited; `EveryNFrames` = update interval; `OnDirty` = host- or scene-invalidated.
- Capturing CCTV monitor camera C into texture T and displaying T on main camera A requires C before A; C must not render geometry sampling T unless using explicitly delayed history.
- For mirrors/portals, support recursion depth budget and explicit previous-frame fallback; no implicit infinite recursion.
- Track per-camera CPU submit ms, GPU timings when available, draw calls, triangles and RT footprint separately from aggregate frame stats.

## 4. Rendering sequence (conceptual)
1. Simulation completes one presentation snapshot; gather camera ECS state and controller/director outputs.
2. Resolve output sizes, policies and target ownership; schedule camera DAG by priorities/dependencies.
3. For each camera: immutable `RenderView`; scoped renderer policies; bind temporal history keyed by world identity + `Entity::packed()` + view variant; `beginScene`; submit shared world data using camera-specific culling and LOD; render to selected `PassDesc`; resolve PostFX if needed; finish pass; publish produced texture.
4. Composite screen and overlays; finish GPU frame; retain targets and history until safe to recycle. Handle camera removal and resize without use-after-free.

## 5. Roadmap
- **P0**: generic `Camera` (stage 1 source migration, not compile validated), portrait extraction, ECS camera entities (Stage 3 snapshot bridge), RenderTarget asset, camera-to-RT, independent screen camera, manual/every-frame capture, two-camera output example, tests.
- **P1**: policy scheduler, camera stack, director and rigs, persistent ECS schema/serialization, editor camera window, per-view PostFX and temporal state, layer masks/culling and profiling, split-screen.
- **P2**: physical lens/DOF, cubemap and panoramas, HDR EXR/capture sequences, planar mirrors/portals, camera shakes and cinematic rails, stereo/OpenXR.
- Capture output should support environment-dependent clear/load/store, MSAA resolve, high-DPI size, orientation and backend Y-flip rules.

## 6. External libraries
- **None required for the P0 camera core**: use existing `Math3D`, `Renderer3D`, `gfx`, `PostFx`, `ImGui`, C++20.
- **OpenXR SDK (optional P2)** for XR runtimes; never make it mandatory for non-XR builds.
- **TinyEXR (optional P2)** for saving linear HDR image captures when no existing format writer suffices.
- **FFmpeg (optional external tooling)** for video export, not embedded in render graph nor required in runtime.
- Avoid GLM/BGFX replacement: AIVERSE already has math and platform RHI; mixing redundant matrix/texture ownership is a liability.

## 7. Tests / release criteria (planned, NOT RUN)
- C++/mock GPU: generation-safe handles, registry lifecycle, scheduling priorities, DAG cycle rejection, target resize, missing target, clears, invalid dimensions, manual capture and visibility masks.
- Real Metal, GL, Vulkan: render two distinct cameras into two RTs and screen in same frame, verify distinct pixels/view transforms and depth, texture orientation, alpha/compositing, resizing/recreation, no read-while-write hazards.
- Temporal: camera A/B alternating with distinct pose/time and post-processing history; cut/resize/reset; no history contamination.
- Stress: 16/64/256 offscreen cameras with 1/5/15/60 Hz budgets; VRAM bound and observed GPU completion; no implicit per-camera per-frame full-scene upload.
- Genre parity: RTS, FPS, voxel and editor scenes with no dependency from `engine` to `modules/*`.
- Mac Metal validation is required before advertising P0 as production-ready.

## 8. Handoff protocol
Every bounded implementation stage must have code commit(s), then a separate dated handoff commit on this branch, with current head, affected paths, actual executed tests and outcomes, compatibility and next action. Do not merge to `main` until build/CTest, GPU and editor acceptance gates are validated. Respect `AGENTS.md`, editor and Metal batching handoffs.
