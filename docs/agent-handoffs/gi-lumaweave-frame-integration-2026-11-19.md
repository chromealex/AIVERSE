# GI + LumaWeave integration — agent handoff (2026-11-19)

Branch: `feature/gi-lumaweave-frame-integration-20261119` (forked from `feature/lumaweave-massive-lighting` at `4a560575417022b4e78fc4c1b677eccce99f6f8f`). Never modify `main` from this stage. Indirect GI and direct LumaWeave remain separate contributions; no double light.

## Stage 1 — geometry-free mobile fallback (SOURCE COMMITTED; NOT NATIVE TESTED)
Source commit: `ca77cb89ee6f51df805b069f852b208d7ee33a96`.
Changed: `engine/render/LumaWeaveNativeCoordinator.h`.

- Add `prepareFrame(const GiGpuBvh* geometry, uint64_t revision, ...)` with a nullable geometry/revision-zero pair. The original `const GiGpuBvh&` overload stays source compatible and forwards to this overload.
- The Metal ray-traced path requires BOTH non-null geometry and nonzero geometry revision. Without geometry it cannot enter Metal, instead the existing explicitly selected mobile/legacy raster path can receive the same validated LumaWeave registry without a dummy BVH. Strictly reject mismatched pointer/revision pairs.
- Neither ECS, LightSystem, RHI, nor the GI probe runtime was modified. Legacy-vs-GPU exclusivity and overflow source-loss diagnostics remain unchanged.

Actual checks: GitHub file read and GitHub source update acknowledged. **NOT RUN:** native C++ compiler, CMake, CTest, Metal/GLSL compilation, GPU screenshots/perf, benchmarks. Source has not been validated by a native build.

Environment/rebase warning: GitHub comparison on 2026-11-19 showed the source feature branch 467 commits behind `main`, with substantial concurrent renderer changes. Do not pretend it is rebased/merged. Preserve main's renderer, camera, texture, FX changes when eventually integrating; resolve a real three-way merge on a native checkout. No branch merge has been attempted.

Next: wire an OPTIONAL GiGpuCascadeRuntime into native per-view driver, with actual fence polling, explicit bounce-source sync, per-view GI overlay reset (Renderer3D::beginScene does not clear it), and fail-closed presentation when source sync is deferred. Then add targeted CPU/source regression fixtures, update AGENTS.md and acceptance runner, and perform ONE native validation pass after code freeze. Record exact test SHA/results, not historic 33/33.

## Stage 2 — native optional bounced GI, source synchronization and compile regression (SOURCE COMMITTED; NOT NATIVE TESTED)

Implementation commits:
- `a8894d64a6416cea9a0538730c8a2ea9fb866b36` — `engine/render/LumaWeaveNativeDriver.h`, nullable direct ray BVH, optional `GiGpuCascadeRuntime*`, bounded GI probe budget, opt-in `synchronizeGiPointSources`, GI result diagnostics, fence-aware source synchronization and per-view scoped GI overlay clear.
- `395afb9c745c261020a58aef90d7f18573aea4aa` — correct native driver GPU completion type name: `LumaWeaveScreenMetal::Completion`.
- `03d5ce6b82a47d818fd0a1baa2c6576c6a0e78b1` — same preexisting typo correction in `engine/render/LumaWeaveWorlds.h`.
- `0088a5095f8676cddf67f7c448c2c4608f87e784` — new `tests/render/lumaweave_gi_frame_contract.cpp`, source-type checks plus fake GPU bounce synchronization cases (ready, pending, revisions, 16-source limit, spot rejection, clear, reset).
- `d16d4528a212e8c0b399d3be4e1ba6d6e14304d3` — CMake CTest target `lumaweave_gi_frame_contract`.
- `5533101c4c938fd9532691f78098c4c20b3c2748` — `AGENTS.md` ownership and safety contract.

API: `LumaNativeViewInput.indirectGi` optionally provides a host-owned, already-configured cascade GI runtime, and `giProbeBudget` controls total cascade probe jobs. Set `synchronizeGiPointSources=true` only if the GI runtime must be fed from the current LumaWeave source registry; this explicitly rejects unsupported spot emitters and >16 moving point bounce sources. On a deferred/busy publication, frame refuses to bind stale bounced GI. `giRequested`, `giBound`, `giDeferred`, `giSourceSync` and `giFrame` distinguish the actual result from default lighting. The RAII scoped borrowed atlas clear prevents one viewport's GI volume leaking to another; it is invoked after renderer beginScene and before rendering and again on exit, even during exceptions. No authoritative ECS or simulation mutation.

Actual validation: GitHub read/update/create commit operations acknowledged, CMake target registered. **NOT RUN:** C++ compilation, Clang Debug/Release, CTest, Metal MSL, GPU frame capture, visual parity, mobile backend tests, full acceptance script. Earlier 33/33 GI tests were owner-reported against stage7, NOT this source SHA. No claims of pass.

Known limitations and next: verify header-only native driver code builds (its include graph is broader than earlier tests); ensure runtime replacement does not reuse the old `LumaWeaveGiSync` revision, preserve GI fence-poll diagnostics, and accept the isolated branch name in the one-shot validation script. Then native Mac test only after code freeze. Current branch still inherits the 467-main-commit divergence from its LumaWeave parent.

## Stage 3 — runtime-replacement safety, one-shot acceptance and native compile gate (SOURCE COMMITTED; NOT NATIVE TESTED)

Implementation commits:
- `b0e8c822c859d3fe659360b5a9e86edfe66da4d2` — `LumaWeaveNativeCoordinator::resetGiSourceSync()` public explicit adapter reset, no GPU or ECS mutation.
- `2c85573f8d58a564c6a1465a635d13fa93ecceda` — `LumaWeaveNativeDriver` resets revision-cache on GI runtime pointer swap or re-enabling GI source synchronization; exposes same-address `driver.resetGiSourceSync()`; rejects sync=true without runtime; preserves prior GPU poll in separate `giPollFrame`, and clears adapter on safe shutdown.
- `088915889e5a2ce995f56251a9e9ec3ac9e30c15` — `tools/lumaweave_acceptance.sh` now accepts either LumaWeave branch or isolated GI integration branch, still requires explicit `--run`, clean native macOS checkout and Xcode Metal.
- `08c9f6d31bb7834c2e5b8caabd36ccb794063efe` — `docs/lumaweave.md` fixes stale claim about GGX: experimental High/Ultra material-normal/roughness specular path exists but actual GPU parity remains unverified; documents combined direct/indirect API.
- `2732ccefebe30aaaab303b6b98905c5d1e7b96a6` — `tests/render/lumaweave_native_compile.cpp` forces compiler instantiation of normal and per-world native frame templates; intentionally not linked to GPU RHI.
- `12e00a8644e748cf5dfe09582097c9eb5da0b4b3` — CMake registers `aiverse_lumaweave_native_compile` OBJECT target in test-enabled default ALL build.
- `eb484de6a95bd6e24a2146e6cef683780147a4da` — compile-time assertions for GI lifecycle reset and completed-vs-new GI frame status.

API/lifecycle: `LumaNativeViewResult::giPollFrame` records pre-sync real GPU completion observations while `giFrame` is the post-sync GI job state. The driver auto-resets its `LumaWeaveGiSync` cache on pointer replacement and on transition from external to synchronized GI source ownership; an owner reconstructing a runtime at the same address MUST explicitly call `driver.resetGiSourceSync()`. `LumaWeaveWorlds` uses independent drivers per world. The compile-only OBJECT fixture deliberately checks that `driver.frame` and `worlds.frame` function-template bodies compile, rather than only parsing headers.

Actual validation: GitHub updates accepted; **NOT RUN**: native compiler, ALL build, CTest, Metal compiler, on-device GPU readbacks, screenshots, performance, full acceptance script. A test target's presence is not a pass. Do NOT re-use earlier 33/33 PASS as validation for this branch.

Native final acceptance on a clean checkout of this branch:
```sh
git fetch origin
git switch feature/gi-lumaweave-frame-integration-20261119
git pull --ff-only origin feature/gi-lumaweave-frame-integration-20261119
git rev-parse HEAD
bash tools/lumaweave_acceptance.sh --run
```
The acceptance script builds both configurations, executes registered CTest and assembles/compiles MSL; hardware GI+Luma PBR visual correctness, mobile raster and actual GPU ms must still be separately measured. Prefer testing the source branch first, then reconcile its ~467 missing main commits in a separate integration branch and repeat tests, with no unreviewed mass overwrite of concurrent renderer/camera work.

Known limitations: static source GI selection still requires host-provided scene geometry; bounce spot/area transport is intentionally unsupported (explicit failure); GI GPU caps, 4096 direct-source cap, mobile lossy fallback and metal-only 3-cascade texture slot are existing design limits. The new driver is opt-in, not automatically registered in all games/editor scenes. Physical end-to-end verification and main reconciliation remain outstanding.

## Stage 4 — portable Vulkan/GLES CPU probe GI route (SOURCE COMMITTED; NOT NATIVE TESTED)

Source commit: `740f9d1cc21cdc29c5af6f06445005bb6f523d2e`; changed `engine/render/LumaWeaveNativeDriver.h`.

The native driver now accepts a second, **optional** `GiDynamicRuntime* indirectCpuGi`, independent from the Metal-only `GiGpuCascadeRuntime*` and sharing the same `LumaWeaveRegistry` source identities. This CPU L1 dynamic-probe path uses the existing `GiDynamicProbeReference` (bounded CPU BVH tracing) + `GiDynamicProbeTexture` (incremental RGBA32F upload) + the existing GLSL `DYNAMIC_GI` model permutation; neither GLES3 nor Vulkan falsely claims Metal compute support. On Metal, a ready GPU cascades runtime wins; elsewhere a ready CPU GI runtime is selected. Direct light stays exclusive: Metal LumaWeave or raster legacy `LightSystem` on Vulkan/GLES. `LumaIndirectRoute` and `cpuGiFrame` report the actual selected transport.

The host MUST initialize CPU GI on the render device and publish its immutable `GiTraceGeometryView` with revisions, then optionally set `synchronizeGiPointSources` to publish eligible point sources (up to the existing CPU reference cap 64). Spot-to-point bounce is still rejected, not silently approximated; source revision is cached transactionally and reset on runtime pointer swap or explicit `resetGiSourceSync()` (also required after same-address reinitialization). CPU `setLights`, `update` and `bind` are source/raster operations without a GPU completion claim. Per-view RAII clears borrowed atlas pointers.

Backend audit from actual GitHub source: `engine/gfx/vulkan/VkDevice.h` does not override `Device::supportsCompute()`; `GfxGL.cpp` likewise has no compute implementation, so true Vulkan compute/RT or GLES compute GI/direct is **NOT implemented**. Vulkan's `VkResources.cpp` supports sampled RGBA32F and the Vulkan shader translator accepts model GLSL; GLES3 implements RGBA32F texture upload and GLSL ES3 sampling, with target-driver validation still required. GLES3 does not have hardware compute in this RHI. The CPU probe path is intended as a compatibility route, **not** claims of GPU RT, full-world CPU scalability or parity.

Actual verification: GitHub source update/read only. **NOT RUN** CMake/CTest/Metal/GLSL/SPIR-V compilation, on-device Vulkan/GLES rendering, mobile FPS and visual parity. Existing acceptance script primarily validates Metal+host CTest; add Vulkan lavapipe and GLES3/WebGL2 shader rendering/visual checks at final acceptance. No prior 33/33 test result covers this source.

Next: tighten CPU probe rate and no-geometry safeguards, document backend-specific required features, add a cross-platform source regression/compile target, and reconcile current main changes without destructive blanket overwrite. Complete source functionality FIRST, then one consolidated test/visual validation pass.

## Stage 5 — zero-budget moving-light ghost prevention for portable GI (SOURCE COMMITTED; NOT NATIVE TESTED)

Source commits:
- `a96f2a7a9c446c88c7dadc5c695c386c963c6b9e` — `GiDynamicProbeReference::setLights(lights,invalidatedProbes)` extends the existing source-change dirty tracking with exact dirty-probe IDs, zeroes stale L1 coefficients immediately and deduplicates overlapping old+new light volumes. Original single-argument API is source compatible.
- `21401b1d8cf10e43c95af8baa0cd7eebca74db80` — `GiDynamicRuntime::setLights` records invalidated probe indices for texture zeroing on the next `update(0,camera)`, before any budgeted tracing. This prevents yesterday's bounced light persisting for many frames when CPU budgets on Vulkan/GLES are small.
- `857ef5c9edb218bf9a29eb6edda2fb4e4a2b5a35` — source regression in `tests/render/gi_dynamic_probe_reference.cpp` checks immediate CPU coefficient clearing on source movement and clean eventual rebuild.
- `cfb93700b226266fae39aa22209d8c306d692ab6` — `tests/render/gi_dynamic_runtime.cpp` checks that no-trace `update(0)` uploads black into stale GPU atlas texels.

No RHI or ECS changes, legacy baked indirect remains independent. New dirty IDs are sorted and deduplicated, reducing upload regions. Source changes outside the probe influence volume cause no invalidation.

Actual checks: GitHub source update acknowledgments only. C++ compile, CTest, Vulkan GLSL/SPIR-V, GLES3 shader/device, Metal GPU readback/visual benchmarks **NOT RUN**. The test assertions have not been observed passing.

Next stage: explicitly bound portable CPU GI work per backend; author compiler/runtime backend policy checks and Vulkan+GLES acceptance harness. Finish functional work and reconcile main before a single execution of test suite.

## Stage 6 — portable backend budget gates + SPIR-V compile source + acceptance plan (SOURCE COMMITTED; TESTS DEFERRED)

Source commits:
- `8642734c54cb9a3ba280a19b214209bf1010eb64`: `engine/render/LumaWeaveBackendPolicy.h` capability report and default bounded CPU GI probe work: GLES3=2, Vulkan=16, GL33=8, Metal CPU fallback=8 probes/frame. This is a safety work limit, **not** measured frame timing; `metalComputeCandidate` is only a capability candidate, not shader compilation success.
- `e7fe07972fff2f07cec374fd7cb31531249d84b1`: `LumaWeaveNativeDriver.h` inspects device backend, honors sampled RGBA32F capability for CPU GI, limits actual CPU GI probe submissions and exposes `cpuGiProbeBudgetAdmitted`/`cpuGiBudgetClamped`.
- `af16e34666e720145442f8df73d570376546b65f`: source-only `tests/render/lumaweave_backend_policy.cpp` (all four backends, no mistaken Vulkan/GLES compute claim).
- `388cdd40ff3ee1ff4869fdf8716f3dd766d4953e`: registered `lumaweave_backend_policy` CTest.
- `923920c47b84335fa7d03a3976b54c5a1427043f`: `tests/render/gi_vulkan_glsl_contract.cpp` checks real `GiDynamicShaders.h` GLSL L1, validity and dual-cascade permutations with vendored Vulkan GLSL-to-SPIR-V compiler. It does NOT require Vulkan device and does NOT validate GLES3 GPU shader.
- `46d177623a0b1727244da6774e62705e105f1275`: CMake test `gi_vulkan_glsl_contract` (only when `AIVERSE_VULKAN` is enabled).
- `2c4c754fefd1aca4f90f86bb4a092ec36c7a94cc`: `tools/lumaweave_portable_acceptance.sh` Linux-only, inert without explicit `--run`: builds Debug+Release, executes entire CTest, rejects skipped Vulkan device and silent fallback, captures GLES3/GL and Vulkan renderer screenshots under Xvfb. This is NOT the dedicated GI+Luma native-view visual fixture.

Actual work: source file creates/updates acknowledged in GitHub. **NOT RUN** native compiler, CTest, MSL/SPIR-V GLSL compile, Vulkan device or OpenGL ES3 captures, WebGL2, GPU timing. The one-shot runners are PREPARED ONLY. Main branch still unmerged. Do not call this complete Vulkan GPU compute: Vulkan `Device::supportsCompute()` is not implemented and GLES3/WebGL2 have no compute API in this engine.

Next: prepare and integrate a minimal native lighting visual scene that actually invokes the LumaWeave driver on Metal/Vulkan/GLES, verify GLSL ES3 16 sampler limits, audit material depth and dynamic probe source publication, and reconcile current main renderer interfaces. Then run one consolidated pass per platform on real hardware (including fallback and measured cost), not before source freeze.

## Stage 7 — non-destructive current-main merge (SOURCE MERGED; NATIVE NOT RUN)

- Main source SHA at merge: `d7d9b3f4180dcba98b9238fd56eedc7ccb420bb1`.
- Pre-merge integration source SHA: `f765dbcb39ac39f41c7182c70e5ee7a2d852084a`, protected by backup branch `backup/gi-lumaweave-pre-main-20261119`.
- **Actual merge commit:** `6cc3f07711f851d9e32dc3ab88250d1146b6abeb`, two parents (pre-merge feature + main). `main` itself was NOT changed.
- A temporary GitHub main-to-feature sync PR #44 correctly became closed/merged when the two-parent feature merge landed. Do not merge the GI branch into main before native acceptance.
- GitHub comparison after merge: `feature/gi-lumaweave-frame-integration-20261119` is `ahead=319, behind=0` relative to main.

Integration method: GitHub Git object merge tree rooted at the exact main tree `49ae6dfc459386f59acdb624e0a38cd67e404e7f`, applied 109 changed feature blob paths, with genuine three-way line merges on the five shared paths `AGENTS.md`, `CMakeLists.txt`, `Renderer3D.cpp`, `Renderer3D.h` and `Renderer3DModels.cpp`. Overlap review identified only two concurrent same-location insertions: both branches appended to `AGENTS.md`, and main inserted `<array>` while GI inserted `<cmath>` in `Renderer3DModels.cpp`. Both sets were retained. The resulting tree `c3f3eba909e8be823ed058e600d5f916626a83ee` was fetched and inspected: 5710 entries, complete (not truncated), with representative GI/backend and main concurrent model/batching/Vulkan files present. No risky force update and no overwrite of `main` were performed.

**Actual verification:** GitHub object writes accepted, two-parent merge ref updated, main ref unchanged, branch diff graph confirmed main ancestor. **NOT RUN:** native Clang/GCC compile, CMake, CTest, Metal MSL, Vulkan SPIR-V, GLES3/OpenGL renderer scenes, GPU timestamps, visual comparisons. A clean Git merge is NOT proof of source compatibility or behavior.

Next: audit merged Renderer3D/ModelShader API interactions, ownership of per-world cameras and lighting quality, and GLES3/Vulkan shader resource slot limits. Fix any source-visible breaks and update the test plan. Only after feature freeze run Mac Metal and Linux Vulkan/GLES acceptance scripts once, then report actual test errors instead of claiming prior 33/33 on this SHA. Earlier statements in this handoff about 467 commits behind main are **historical** and superseded by this stage.

## Stage 8 — merge-era batched GI, temporal-route safety, portable CPU cost (SOURCE COMMITTED; TESTS STILL DEFERRED)

Source commits:
- `d0db56b31112e5073275a2ac8c8769980b78ce09` — `Renderer3DModels.cpp::dynamicGiEligible`: remove unintended `gpuInstances || instancePalette` rejection; world-space per-instance PBR GI should be composable with CPU/GPU MDI, static material palettes and Metal ICB instead of disappearing when rendering was batched. Remaining HDR/Medium+/unlit/Shader Graph resource guards preserved.
- `a68bac6804abe8fe7815f7288f9c16731a58cd9d` — `LumaWeaveNativeCoordinator.h`: `backendUnavailable` now distinguishes absent GPU support from quality=Off, inactive HDR or missing BVH.
- `999c29702a32962fe735c1f0f3e1e80e22206b6f` — `gi_vulkan_glsl_contract.cpp` now also assembles and compiles REAL `ModelShaders.h` instanced, material-palette and dual-cascade PBR GLSL permutations into Vulkan SPIR-V (source-only).
- `819b8fd3149917d836015de9e5d3de27441c26f7` — `LumaWeaveScreenMetal.h`: new explicit `invalidateHistory()`, monotonic route/history epoch and pending-dispatch epoch prevent async successful Metal fences resurrecting a no-longer-valid history.
- `78a7a5002ae01d582808b260a8cd837d056653ca` — `LumaWeavePostProcess.h` exposes explicit history invalidation (not inside `setEnabled(false)`, which is used at the beginning of EVERY active frame).
- `42d7461ff580b4807bd839866835be02084655bc` — `LumaWeaveNativeCoordinator.h` calls history invalidation ONLY when the previous frame was MetalGpu and this frame abandons that route. Live GPU work must still drain the real fence; re-enabling cannot inherit ghost lighting from earlier Metal route.
- `ab97da91d8ad698e6d956312f41c95745d508c3d` and `44615fd246017d12be6821ae8ec15884e3b2d717` — new `lumaweave_screen_history_epoch` FakeDevice regression source and CTest registration (GPU completion simulated by explicit external fence, not real GPU pixels).
- `77d85776567a13a750f1b938304aea287faf2a07` and `fe5a57642bfafa972f1579f4c889eb79699d31c6` — mobile/Vulkan/GLES3 raster path no longer builds the expensive Metal-only world-space GPU cluster CSR each frame as lights/camera move. Cluster reconstruction is scoped inside the actual Metal GPU candidate branch.

Source-level compatibility notes: Current `Renderer3D.h` has the receiver APIs, native frame driver and post-process hooks after the non-destructive Stage7 main merge; `Renderer3DModels.cpp` has U6/tex13 L1, U7/tex15 dual and Metal-only U8/tex16 triple shadow bounce binding, plus the new batched GI eligibility guard. Shader Graph texture-slot conflicts are still explicitly excluded. No claim of actual instanced or Vulkan GPU pixels until on-device tests.

Actual verification: GitHub source reads, line-level review, source changes and CMake registration only. **NOT RUN:** local compilation, CTest, Metal MSL/Vulkan SPIR-V compile, GLES3/WebGL2 hardware shader validation, pixel readbacks, API Validation or GPU-ms timings. FakeDevice code is authored but not executed. Production merge signoff pending.

Next: audit and fix any remaining shader/backend capability corner cases (GLSL ES3 sampler/uniform limits, Vulkan compiler full instanced variants, LumaWeave exclusivity/fence), implement an actual opted-in native GI+direct visual fixture across platforms, and then run ONE consolidated native acceptance per Metal and Vulkan/GLES. The user explicitly prefers all feature work first, tests afterwards.

## Stage 9 — production GLES3 shader export and strict parser/link gate (SOURCE COMMITTED; TESTS NOT RUN)

Source commits:
- `cd91a6883f1807de9976707e1a1f2d4b1f0f48e0`: `tools/lumaweave_shader_dump.cpp` now exposes `--gles3-list`, `--gles3-vertex <variant>`, `--gles3-fragment <variant>`, while preserving existing Metal `--list` and MSL variants. Uses the EXACT production `shaders::kModelVS/kModelFS` concatenated GLSL bodies with actual `#version 300 es`, `precision highp` and renderer's GI permutation defines. Variants: single L1 GI, validity L1, dual cascades, instanced GI and material-palette GI; no misleading Metal source for GLES.
- `ab75958a6f5f86c5f74aafc23436aa678c4bb29f`: `tools/lumaweave_portable_acceptance.sh` now requires the external `glslangValidator` (Linux package often `glslang-tools`), exports each vertex+fragment ES3 permutation, compiles/links with `glslangValidator -l` and stores per-variant logs and status before Vulkan/GLES screenshot smoke. It still fails closed when missing dependencies and remains inert without explicit `--run`.

Why: Vulkan's GLSL-to-SPIR-V adapter is not equivalent to an actual GLES3/WebGL2 shader compile. The prepared acceptance now checks both Vulkan glslang SPIR-V and production GLSL ES 3.00 syntax/link. On-device GLES3/WebGL2 actual drivers and CPU GI/direct game scene visual parity still need real testing.

Actual verification: GitHub reads/creates/updates only. **NOT RUN** the exporter executable, glslangValidator, CTest, Vulkan/GLES runtime, Metal or benchmarks. All tests remain deferred per owner. Keep test SHA and exact failure logs during one final acceptance pass.

Next: audit GLES3 sampler / UBO caps and the renderer's own material receiver/lighting paths on low capability GPUs, then freeze changes and conduct real native validation. Do not call static shader dump 'GLES production signed off'.
