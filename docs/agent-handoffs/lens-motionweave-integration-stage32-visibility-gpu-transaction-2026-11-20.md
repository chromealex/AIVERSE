# AIVERSE LENS Stage 32 — transactional VisibilityField GPU initialization and fog sheet rebuild (2026-11-20)

## Branch, commits and changed files
- Repository: `chromealex/AIVERSE`; branch `feature/lens-motionweave-integration`; `main` unchanged.
- `d21a75cb651adfc5081b41dff711b7a3862199c5` — `engine/view/VisibilityField.h`: document staged init/rebuild and add `shutdownAfterGpuIdle()` for retryable GPU-safe cleanup.
- `7e63ddedd16a9013a155df730cee419ad10aa541` — `engine/view/VisibilityField.cpp`: transactional shader/pipeline creation; prevent cross-device initialization; guard direct shutdown with a separate explicit idle API.
- `36e357dbc0e2e04bb599428f1f8a61844048e630` — `engine/view/VisibilityField.cpp`: build new vertex/index GPU buffers and optional R8 texture before changing any published resource; verify GPU idle before destroying previous in-flight buffers/textures. Failure preserves old GPU handles, fog visibility arrays, map dimensions, and mesh index count.
- `d9bd55dd33de1ed746d11e418cf42010169707a0` and `5fbd0161e2e9e64160aa03859028d019219f34b8` — `tests/tests.cpp`: CPU mock RHI fault injection and resource cleanup assertions for failed shader, pipeline, vertex buffer, index buffer, R8 texture creation, and rejected GPU idle. Tests also cover retry and wrong-device init.
- `02cae02828aedadec352509b7934e79377934172` — `engine/view/VisibilityField.cpp`: ensure pipeline-failure shader cleanup does not attempt the same destroy twice if a backend throws.
- Previous handoff: `docs/agent-handoffs/lens-motionweave-integration-stage31-cross-owner-reload-preflight-2026-11-20.md`.

## Design and GPU ownership contract
- `VisibilityField::init(dev)` now creates shader and pipeline in unpublished local variables. On failures, local resources are destroyed directly because no renderer could have submitted them. The member device pointer and handles are published only after both are valid. Already-initialized calls on the same device are idempotent; a different device throws `logic_error`.
- `VisibilityField::build(dev,w,h,height)` generates CPU vertices, indices and replacement zero-initialized visibility arrays first. It creates a private replacement VB, IB, and (only when changing dimensions or no texture exists) R8 texture. Any create failure or thrown exception destroys only *unpublished* handles, then propagates; previously published sheet and visibility state are unaffected.
- A successful `dev.waitForResourceIdle()` is mandatory *if* previously published fog mesh buffers or a replaced old texture exist. Only then are old handles destroyed and the new handles and optional visibility arrays committed. The barrier may block; invoke rebuild at a safe render-owner frame boundary outside an open pass. This stage intentionally chooses safety over an asynchronous token queue; optimize with retirement tokens later after device-backed validation.
- `shutdownAfterGpuIdle()` returns false and retains GPU handles if the device cannot establish GPU idle. Regular `shutdown()` remains the low-level immediate cleanup for call sites like `RtsView::shutdown(dev)`, which already waits for full-device idle first.
- `VisibilityField::update`, shading behavior and gameplay/simulation visibility masks were not changed. No genre-specific logic was introduced into the generic engine-level `VisibilityField`.
- A failure in a backend's own `destroyBuffer/destroyTexture` *after* a successful idle barrier is not yet made fully exception-transactional; real RHI resource-destroy failures require separate host/device-loss policy.

## Tests authored but NOT RUN
1. `visibility_field_gpu_init_failure_is_retryable`: mock shader allocation failure, mock pipeline allocation failure with shader cleanup, subsequent successful initialization, idempotence, device mismatch and safe shutdown.
2. `visibility_field_gpu_build_rolls_back_buffers_texture_and_idle_refusal`: successful 2×2 sheet build and visibility update; fail replacement IB, fail replacement R8 texture, refuse GPU idle after successful staged allocations, and verify original texture and visibility arrays persist. Assert exactly the expected temporary and previous resource destruction counts, then allow idle and verify successful 3×3 publish and guarded shutdown.
- Tests are in existing `tests/tests.cpp` using a derived `entityfxtest::RecordingDevice`; no new CMake targets.
- This is a simulated synchronous RHI, NOT a real device test and NOT proof of Metal/Vulkan/GLES parity.

## Source checks actually performed
- Read current `VisibilityField.h/.cpp`, `Gfx.h`, `RtsView` call sites, and the existing recording test device.
- GitHub commits and post-commit fetch verified new API, staged shader/pipeline ownership, staging/idle/commit order, wrong-device check, and both regression test functions.
- **NOT RUN:** CMake configuration, native C++ compile, any CTest, Metal/Vulkan/GLES build/GPU test, pixel-parity regression, benchmarks. Do not claim those pass.

## Risks and next exact work
1. Check under native C++ compiler and execute the new tests. In particular verify `RecordingDevice` handle counters, shader/pipeline failure injection and resource destruction; fix any failures before final acceptance.
2. For Fog, consider an async GPU-retirement queue for old VB/IB/texture when terrain is rebuilt frequently; current safe fallback waits for a full device idle barrier on rebuild. Retain old handles on failed GPU idle.
3. `RtsView::init` still calls `fog_.init(dev)` after publishing sprites and does not inspect the return value; a future whole-RtsView transaction should stage fog initialization before publication and define fatal vs optional fog failure policy.
4. `RtsOverlays::initGpu` still partially creates shader, pipelines, textures and VB without an atomic publication/rollback guarantee. Road/Shore kit texture initialization and TerrainRenderer GPU construction also need explicit staging/recovery. Continue Stage 33 with these owners, avoiding destruction of in-flight resources.
5. After all features are complete, reconcile latest `feature/universal-ecs-animation-pipeline` via conflict-aware merge, then perform one consolidated native build/CTest and supported Metal/Vulkan/GLES verification before merging `main`.
