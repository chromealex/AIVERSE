# Hybrid GI — Stage 27: portable moving-light invalidation (2026-11-19)

## Source, branch, and status

- Repository: `chromealex/AIVERSE`
- Branch: `feature/gi-lighting-portable-backends-20261119`, created from `feature/hybrid-dynamic-gi` HEAD.
- **SOURCE COMMITTED, NOT NATIVE-TESTED. NOT PRODUCTION COMPLETE.** The new branch has NOT been merged into `main`. The original GI feature had diverged from current main (159 commits ahead, 467 behind at inspection), so do not assume a clean cherry-pick/rebase; first inspect and reconcile all overlapping renderer files.
- Code commits:
  - `868c5f03f1574c2027effd55ece3fd68c15f96b5` — `engine/render/GiDynamicProbeReference.h`
  - `183c4de72c68156810d2c6448a1d15d74bccd178` — `engine/render/GiDynamicRuntime.h`
  - `84da5aaee60c4633debd46128cda7a052abfd64b` — `tests/render/gi_dynamic_runtime.cpp`
  - `affce5ea71d03629ddb19111998ef136cff6f8d0` — explicit test header include.

## What was changed

1. `GiDynamicProbeReference::setLights(lights, cleared=nullptr)` now clears CPU L1 indirect coefficients for the affected union of OLD and NEW moving-light volumes immediately, rather than leaving the previous bounced illumination visible until probes are retraced. It optionally returns all invalidated probe IDs. Existing callers retain source compatibility via the optional argument. Changed, deleted and added lights use their conservative radius + ray trace distance. The scheduler still retains revision-safe dirty work and fairness.
2. `GiDynamicRuntime::setLights` queues these cleared IDs into the existing sparse atlas update path. The renderer's opt-in `bind()` rejects presentation while a full or partial invalidation remains unflushed. Calling `update(0,camera)` uploads the invalidated zero coefficients on the RHI, even with NO CPU tracing budget; subsequent bounded updates repopulate the independent dynamic GI layer.
3. The `gi_dynamic_runtime` regression fixture now checks moving-light changes, source removal, CPU/GPU stale clearing, zero-budget sparse uploads, and the generic CPU-RHI path with `FakeDevice` identities `GLES3`, `Vulkan`, and `Metal`. FakeDevice identity tests are NOT hardware shader or driver tests.

## Compatibility, backend boundaries, and risks

- Existing baked lightmaps, AVGIC005 probe assets, direct sun/point/spot lighting, ECS/gameplay saves, network state and shader descriptor slots have not changed.
- GLES3 and Vulkan still have a **CPU tracer + dynamic RGBA32F atlas upload + GLSL fragment sampling** opt-in path where the device supports the required texture format and HDR renderer/material tier. **Neither GLES3 nor Vulkan has the native GPU GI compute path implemented by this stage.**
- The existing dedicated `GiGpuDynamicRuntime`/`GiGpuCascadeRuntime` compute implementation remains Metal-only. Metal three-cascade tex16 is NOT a GLES/WebGL2 feature. GLES/WebGL2 dual GLSL consumes fragment texture slots 13,14,15; GL/Vulkan use U6/U7. Shader graphs with overlapping texture slots remain excluded. No backend GPU performance, shader compile, pixel comparison or temporal stability is asserted.
- A renderer is still required to call `update` after changing light/geometry inputs and before `bind`. The dirty scheduler API can be invoked directly outside the `setLights`/regional geometry wrappers; direct callers must own their validity/publication discipline.
- Weak devices may require a lower-memory format or a low-quality/off default; CPU tracing for a large clipmap is not a production mobile performance solution.
- `GiDynamicRuntime` is an opt-in presentation layer and not automatically connected to all native ECS lights or TerraWeave/PHYSIS vegetation and terrain changes.

## Actual validation

- **DONE:** GitHub source fetch, precise content updates and commit acknowledgements.
- **NOT RUN:** local CMake configure, C++ syntax/build, Debug/Release CTest, `./run-tests.sh`, Metal GPU captures, Vulkan GPU captures, Android/GLES/WebGL2 shader compilation/runtime, Vulkan compute pipeline and GPU timing. This workspace has no mounted AIVERSE clone and the container cannot resolve github.com, so source-level test registration cannot be treated as a PASS.

## Next precise actions

1. On the development Mac/CI, check out this branch and run `cmake -S . -B build/gi-portable -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=OFF -DAIVERSE_BUILD_CLIENT=OFF`; `cmake --build build/gi-portable --target aiverse_gi_dynamic_runtime_test aiverse_gi_dynamic_probe_reference_test aiverse_gi_dynamic_probe_texture_test -j8`; `ctest --test-dir build/gi-portable --output-on-failure -R '^gi_(dynamic_runtime|dynamic_probe_reference|dynamic_probe_texture)$'`. Fix any errors with a new code commit and a separate handoff. Next run all GI tests and full acceptance once functionality is complete.
2. Implement proper Vulkan GPU compute for GI by porting the Metal-only `GiGpuSmallSceneShaders.h` kernel to SPIR-V via backend-compatible compute source, RHI storage image/barrier/buffer bindings, completion fences and real `VkDevice` validation. Do not simulate a PASS. For GLES 3.0 (without compute), keep the bounded CPU probe/update path or a shader-based fallback; first validate format, sampler and uniform budgets on GLES hardware and WebGL2.
3. Generalize clipmap/cascade binding for non-Metal CPU and GPU paths without a texture slot 16. Add dual-level pixel parity, light-occluder/deformation invalidation, source ownership, and shader graph/terrain/foliage coverage before enabling globally.
4. Reconcile with latest `main` carefully; the in-flight renderer, texture and ECS changes from other agents overlap this feature. Avoid force-push. Record the exact tested SHA and actual results before considering a merge.
