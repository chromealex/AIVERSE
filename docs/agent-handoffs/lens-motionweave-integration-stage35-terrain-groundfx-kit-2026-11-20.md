# AIVERSE LENS Stage 35 — transactional Terrain/GroundFX GPU kit reload (2026-11-20)

## Repository, branch and actual commits
- Repository `chromealex/AIVERSE`; branch `feature/lens-motionweave-integration`; `main` unchanged.
- `c63ee235f3b10a85bbc5fe44c07de289f1f5715f` — `modules/rts/view/RtsTerrainRender.cpp`: parse terrain look into a CPU-only temporary, stage three required fallback textures before old live resources are destroyed, require device idle on reload, and roll back unpublished allocations on failure.
- `3863fb7f04a812e84db922db45e995613594065c` — `modules/rts/view/RtsTerrainRender.cpp`: prevent cross-device release, make shutdown idempotent, and release all SIX potential terrain shader/pipeline variants (prior implementation only looped over three).
- `e16ad8c9eebbe1232a9fef05c3d4d9cababbd54c` — `modules/rts/view/RtsGroundFx.h`: private unpublished-GPU-kit initialization API.
- `1a9ae5574beca7aa07768aaad9d9e847f029897a` — `modules/rts/view/RtsGroundFx.cpp`: build a separate never-submitted GroundFX instance with parsed CPU looks, TextureCache/icon handles, shader/pipeline and white texture; publish only after all succeed and the previous device is idle, with cleanup of failed staging resources.
- `d2d47958f6eb6b1993ea71b2e6e23db56f93be5c`, `dcc69a454c1f133dd55348e32abb07d696d4e9a8` — `RtsTerrainRender.h` and `RtsGroundFx.h`: explicit guarded `shutdownAfterGpuIdle(gfx::Device&)` and `ownsGpuResources()` diagnostics.
- `e4ae27954755887ea69b00e6c070bdc3f2f29e4d`, `2eb54009c3672af0990852c6daf229a3205edb7e` — `RtsTerrainRender.cpp` and `RtsGroundFx.cpp`: idle-gated shutdown entry points and stronger GPU owner identity checks, including GroundFX no-op when not initialized.
- `81805fe014e3357f5a83a23c7915da0b0c27fa13` and `82540ebf76e9cd3f2f9f7876868bdb9fa5d1e18c` — `tests/tests.cpp`: Terrain/GroundFX fault-injection tests for texture, pipeline and idle refusal, retry, teardown, wrong-device and preservation of published GroundFX look.
- Previous handoff `docs/agent-handoffs/lens-motionweave-integration-stage34-road-shore-staging-2026-11-20.md`.

## Architecture and API
- `TerrainRenderer::init` checks device-generation lease and previous RHI owner, parses its look in an unpublished temporary renderer, and creates three essential GPU textures (`white_`, `flatN_`, `gray_`) in local handles before touching old state. Missing GPU resources raise an error. After a successful full-device idle, the existing terrain is shut down and the prepared look/callback/textures become active. If allocation or idle fails, only the new unpublished textures are destroyed; old live terrain and CPU visual look survive.
- `RtsGroundFx::init` creates `std::unique_ptr<RtsGroundFx> staged`, populates its CPU looks and optional texture cache, builds the shader/pipeline/white fallback and checks required handles. It then obtains a GPU idle barrier if replacing a previously published instance, destroys the old instance and transfers prepared handles/configuration into the live owner. If staging or idle fails, it cleans only unpublished staging resources. The existing configured GroundFX and its mesh state remain untouched on a refused barrier.
- Both classes expose `ownsGpuResources()` and `shutdownAfterGpuIdle()`; the latter leaves resources allocated and returns false on a refused GPU idle proof. `RtsView::shutdown(dev)` continues to establish a device-wide idle barrier before using existing immediate shutdown methods.
- The Stage 35 changes are in RTS presentation adapters but do not change genre-neutral camera/ECS/render data schemas, serialized content or animation clocks.
- `TerrainRenderer::shutdown` now covers all six `pipes_` / `shaders_` slots, not merely 0–2.

## Tests written (NOT executed)
1. `terrain_gpu_kit_staging_recovers_from_allocation_and_idle_failure`: fail fallback texture #2 after texture #1 was allocated, confirm unpublished texture cleanup and no live owner; retry, reject idle during live reload, preserve previous owner, reject a foreign device, then successful retry and safe shutdown.
2. `groundfx_gpu_kit_staging_recovers_from_pipeline_texture_idle_failure`: fail first pipeline then white texture during initialization and verify staged shader cleanup; retry, reject idle during reload and preserve previous ownership/look, reject foreign device, then successful retry and safe shutdown.
- Both are in existing `tests/tests.cpp` and use `fogfaulttest::Device` as a CPU fake RHI.

## Actual validation
- Retrieved full `RtsTerrainRender.h/.cpp`, `RtsGroundFx.h/.cpp`, `RtsView.h/.cpp`, and existing tests from the active GitHub feature branch; examined init, resource ownership, GPU lease checks and shutdown.
- Committed implementation and test changes through GitHub, then re-fetched source to inspect the edited methods.
- A **standalone tiny C++20 syntax check** of `std::exchange(handle,{})` and `std::function::swap` passed using the available compiler. This is not an AIVERSE build.
- **NOT RUN**: native CMake configuration, any AIVERSE C++ compilation, `aiverse_tests`, CTest, Metal/Vulkan/GLES backends or GPU runtime tests, pixel parity and performance measurements. Do not claim these tests passed.

## Open issues and next exact steps
1. `TerrainRenderer::build` and `TerrainRenderer::upload` still rebuild and destroy published chunk VBs/IBs and dynamic textures in-place; Stage 35 covers the **initial GPU kit**, not atomic chunk updates. Implement staged mesh upload/retirement or a host-safe replacement at a frame boundary, including device-loss partial allocation paths and bounded memory use. Also audit detail/normal/splat texture ownership and six shader variants on actual compiler.
2. `RtsGroundFx::setMesh/freeMesh/resetGrid` still immediately destroy old chunk meshes during ordinary runtime rebuild. Use a GPU retirement queue and per-camera snapshot semantics before claiming complete lifecycle safety. Audit shutdown-on-backend-error exceptions.
3. GroundFX and Terrain re-read parts of visual rules configuration during independent initialization. Unify this with the `RtsView` single parsed config snapshot to prevent cross-owner mixed look state.
4. `RtsView::init` currently publishes the other owners (sprites/Fog/Overlays/Roads/Shores) before GroundFX and Terrain are re-initialized. Therefore **RtsView-wide reload is NOT yet atomic** when a late Terrain/GroundFX error occurs; stage all owners behind a common publish phase before claiming it is.
5. Native compile and run all Stage 20–35 tests in the requested consolidated validation, correct any CMake/CTest and C++ regressions, then reconcile `feature/universal-ecs-animation-pipeline` with a real conflict-aware merge, preserving `apps/modelview/main.cpp`. Metal/Vulkan/GLES backend parity needs real compilation/device evidence before `main`.
