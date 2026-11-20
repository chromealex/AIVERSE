# AIVERSE GI — Stage 35: genre-independent compute/CPU transport owner (2026-11-20)

## Exact source status, branch and commits

- Branch: `feature/gi-lighting-portable-backends-20261119`. PR #47 remains a **draft** to `feature/hybrid-dynamic-gi`, not current `main`.
- Code commits: `ddbf95b116bea5337f487022f5985a23f7767b38` (`engine/render/GiLightingController.h`), `f6861d43c41d6ddf77208529ed1569d79eaa3f68` (external Metal confirmation forwarding), `da4b899962173d1291225cab1928d102b1d23d7b` (`tests/render/gi_lighting_controller.cpp`), `d715b5d9a3822722fe5243260503869fcaa6051f` (`CMakeLists.txt`).
- **SOURCE COMMITTED ONLY — native build, CTest, Metal/Vulkan GLSL, GLES real render and performance NOT RUN.**

## Purpose and APIs

Added a single public opt-in `GiLightingController` with three presentation modes: `Off`, `CpuProbes`, `GpuCompute`. The common `GiTransportConfig` supplies origin, spacing, dimensions, ray count, trace distance, preferGpu and CPU budget defaults. `initialize(device,cfg)` first tries Metal/Vulkan `GiGpuDynamicRuntime` (actual RHI compute/storage image availability must succeed); otherwise falls back automatically to `GiDynamicRuntime`, which uses CPU BVH rays and sparse RGBA32F sampled textures. GLES3/WebGL2 always choose the CPU path even if a fake driver claims compute.

`setGeometry(shared_ptr<const GiChunkedGeometrySnapshot>,monotonicRevision)` and `setGeometryRegion(next,revision,bounds,radius)` provide one lifetime-safe immutable read barrier publication API: the controller retains snapshot ownership through CPU `GiTraceGeometryView` borrows or GPU jobs. GPU re-publication can return false while a fence is pending, without dropping the previous snapshot. `setLights(vector<GiMovingLight>)` is stable-key based and checks mode-specific max count (GPU 16, CPU 64), without silently discarding authored lights. `updateFrame(camera,gpuProbeBudget)` either polls/submits GPU tickets or applies the adaptive CPU budget; `bind(renderer)` and `unbind(renderer)` ensure borrowed atlases are never intentionally presented without a geometry snapshot or a safe fence. `confirmExternalGpuCompletion(serial,success)` is only for real external Metal completion signals when callbacks are unavailable; Vulkan still refuses manual ACKs because its own VkFence serial is authoritative.

The controller is genre independent. It does not own game ECS components or save/network data, does not add direct sun/point light twice, and remains completely OFF unless an application creates and drives it.

## Compatibility and limitations

- Existing direct light source API and all baked GI data remain unchanged. No default renderer hooks automatically create the controller. Apps must call it after ECS/terrain/physics change barriers, supply genuine immutable geometry and source-revision monotonicity, and unbind before teardown.
- GPU compute branch is bounded by previous `GiGpuSmallScene`: 8192 triangles, 16 lamps, 32 dirty probes per dispatch. CPU branch is bounded by the adaptive governor, at most 64 lamps. For more complex worlds, use a carefully budgeted per-cell/clipmap owner rather than silently exhausting GPU storage.
- The fake backend test covers mode selection, successful GPU/CPU resource teardown without in-flight work, cap rejection, monotonic source revisions and CPU update. It **does not prove Vulkan/Metal shaders compile or emit correct pixels**.
- CPU fallback on GLES LDR uses the Stage 34 `giDynamicColorPassAllowed` policy. GPU-written RGBA16F remains gated to HDR PBR. Shader Graph/terrain/foliage GI coverage still needs explicit integration; this is not a complete universal lighting system.
- `setLights` in CPU mode currently propagates invalid-input exceptions as in existing `GiDynamicRuntime`; `setGeometryRegion` can also propagate invalid bounds. Host must validate producer inputs or catch those failures. GPU mode returns false for many invalid inputs. This is a known asymmetry to resolve in a later stage if desired.

## Actual validation / next exact action

- Actually performed: GitHub source reads, code commits, CTest registration and handoff creation only.
- **NOT RUN**: native build/CTest, GLSL/MSL compilation, Vulkan GPU validation layers, GLES3 CPU timings, pixel parity, full acceptance.
- Next: build `aiverse_gi_lighting_controller_test`, `aiverse_gi_dynamic_runtime_test`, `aiverse_gi_gpu_small_scene_test` and run the corresponding CTests. Fix compiler/link errors and document tested SHA.
- Add an adapter from the already present direct `PointLight` authoring to stable-key indirect `GiMovingLight` descriptors with explicit opt-in multiplier and source namespace, including rejection/handling of spotlights until cone-correct bounce is implemented. Wire it through native ECS/world source snapshots after barriers; keep baked static sources excluded. Add tests and a separate handoff stage.
- Compile GPU shader variants and validate real Metal/Vulkan/GLES images. Reconcile current main and then the full test suite in a single acceptance pass, without prematurely merging the draft PR.
