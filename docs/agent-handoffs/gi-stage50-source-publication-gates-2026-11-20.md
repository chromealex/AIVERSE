# AIVERSE GI — Stage 50: fail-closed point/spot/environment publication and idempotent source frames (2026-11-20)

## Branch, source commits and actual verification

Repository: `chromealex/AIVERSE`. Branch: `feature/gi-lighting-portable-backends-20261119`. Draft PR #47 targets `feature/hybrid-dynamic-gi`, NOT current divergent `main`.

- `31cbad361f23f93e10333e58a35239f217db3585` — `engine/render/GiGpuDynamicRuntime.h`: compare sorted stable-key light sources to already accepted lights BEFORE checking whether a GPU dispatch is busy. An unchanged snapshot is idempotent, requires no storage-buffer upload or invalidation, and is accepted even while an old Metal/Vulkan job is in flight; a changed snapshot still waits for actual GPU completion.
- `dc361d2254dcc13802617b9897aa751b7d0406c0` — `engine/render/GiLightingController.h`: per-source `pendingLights_` and `pendingEnvironment_` flags. Invalid/over-budget/unaccepted newer source sets block the old dynamic GI overlay until a valid set is accepted. `presentationBlocked()`, CPU/GPU `StepStats::canPresent`, and fail-closed `bind(renderer)` now reflect geometry, lights, and environment independently; `shutdown` resets flags. `setAuthoredLights` marks the light unsafe BEFORE converting stable IDs, so invalid-input exceptions cannot leave stale bounce visible.
- `cf74f9ed04f68dec4756843cedb415d6f403ca42` — `tests/render/gi_gpu_dynamic_runtime.cpp`: idempotent same-source submission during in-flight GPU work succeeds, but changed/deleted lights still refuse until GPU fence confirmation.
- `788b43717a32ba5d85142e78ea67c384af0bb3dc` — `tests/render/gi_lighting_controller.cpp`: changed over-budget light list, excessive moving-light radius, invalid environment, fail-closed visibility and subsequent recovery after valid source revision.
- `572203d2e217f86919b0ab176ea51eef4b8675fd` — `GiLightingController::syncRendererEnvironment` now marks the prior environmental atlas unsafe BEFORE `giEnvironmentFromLighting` conversion, since malformed authoring can throw before the backend receives a valid revision.

**ACTUALLY RUN:** GitHub source inspection and commits only. **NOT RUN:** native CMake/Clang/GCC, all CTest tests, MSL/SPIR-V/GLES GLSL compilation, Metal/Vulkan/GLES pixel and performance validation, complete acceptance. FakeDevice test code is authored but NOT executed. Report no PASS.

## Correctness invariant

Original point/spot publication in `GiGpuDynamicRuntime::setLights` rejected all calls when its previous GPU batch was in flight, even when the caller submitted the **same unchanged** stable-key source set. The host `syncRendererLights` is intended to run each scene frame; refusing a no-op while busy made a safe frame appear like a failed update. The new code sorts by stable key and returns true on exact equivalent sources before checking busy, without touching any GPU resource. Changes in position, radius, color, intensity, spotlight direction/angles remain true revisions and require safe synchronization.

Conversely, an authored point/spot, sun or sky change can fail due to Metal/Vulkan job still working, a moving-light radius outside the bounded BVH source reach, too many lights, invalid numeric data, duplicate stable IDs or malformed environment authoring. When that happens authoritative/direct scene lighting may already be different; even a *completed* old GI atlas is stale. The controller now sets the corresponding source gate BEFORE backend submission/conversion, refuses `bind`, and hides stale indirect light until that exact category is corrected. Distinct geometry/lights/environment flags prevent an unrelated environment update from erroneously clearing an unaccepted light revision or vice versa. Back-end GPU scheduler/fence safety remains intact; this gate is the missing host-level presentation condition.

## Next specific actions

1. Native build/execute `gi_gpu_dynamic_runtime`, `gi_lighting_controller`, `gi_gpu_residency_planner`, `gi_world_presentation_sync`, `gi_model_batch_policy`, and `gi_terrain_compact` CTests, recording exact SHA and results. Then add actual Metal/Vulkan/FakeDevice visual status fixture after producer rejection.
2. Reconcile the active main/TerraWeave/PHYSIS branch trees without overwriting parallel agents; wire proper read-only event publishers and ensure a duplicate/invalid authored update cannot be silently ignored.
3. Validate full GL ES3/WebGL2 compact terrain + CPU GI, rich Metal/Vulkan GI and actual terrain/foliage ModelDraw batching and per-instance palette pixel parity on devices.
4. Reduce remaining GPU local source budgets via real chunk LOD/proxy paging without deleting relevant blockers; run the final complete AIVERSE acceptance suite after remaining functional work, and only then merge to main.
