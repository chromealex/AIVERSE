# TerraWeave Caverns — Stage 24 GPU handoff (2026-11-23)

## Provenance
Repository: `chromealex/AIVERSE`.
Feature branch: `feature/terraweave-caverns-core-20261123`, not merged to main.

Commits:
- `1d8aee4201313f6edeb1a09bd388ba09cd1c5633` — create `engine/render/TerraWeaveCavernsGpuSwap.h`.
- `876d4be07e3a92b342446e8ecdfdffe5ba3bcd56` — bound all live GPU owner containers and preserve them on shutdown failure.
- `e4a77a29a3015bcb02c90c91c4a39c42f1eb95d2` — create `engine/render/TerraWeaveCavernsSwapPolicy.h`.
- `a4e4e284d3160c06c065f6feeea269372816faa2` — connect the pure revision guards to the actual scene owner.
- `8b0524b321e2f34e4a804d966c6ffe2e36f7120c` — create `tests/terraweave_caverns_swap_policy_test.cpp`.
- `f6a7e47cfa7687655401f80b889b861cfa9d87a9` — update `skills/world/aiverse-terraweave-caverns/SKILL.md`.
- This handoff follows; inspect branch HEAD for the final commit.

## Real engine APIs inspected
Actual `engine/render/GeometryScene.h`, `engine/render/GeometryScene.cpp`, `engine/render/NativePresentationRetirement.h` and `engine/render/GpuResourceRetirement.h` on the feature branch.
- `GeometryScene::loadPrepared(device,json,models,vertexBudget)` uploads immutable prepared CPU Model sources to the real GeometryScene/STRATA path.
- `GeometryScene::draw(renderer)` is the one draw owner for that scene.
- `GeometryScene::shutdown(device)` retires GPU resources, may require GPU idle and can throw if barriers fail; `collectRetired` handles its own deferred model owner queue.
- STRATA index/metadata/culling sidecars currently require `device.waitForResourceIdle()` in portions of `GeometryScene::retireResident`. Merely waiting for a CPU frame boundary is not sufficient.

## Implemented
`CaveGeometrySwapOwner` owns independent `active`, `staged`, and bounded `retired` GeometryScene objects.
- `stage(device,prepared,revision,enableStrata,budget)`: stage a real new GeometryScene with `loadPrepared`; it neither unpublishes nor deletes the previous active scene. Failed/partially uploaded scenes are retained for explicit recovery. Staging rejects stale or zero revisions and too many live GPU owners.
- `activate(hostCommit)`: accepts the staged revision only if host confirms its wider collision/navigation/renderer owner commit succeeded. Failure keeps old draw active; success moves old active owner to retirement and makes the staged owner active.
- `draw(renderer)`: draws the one currently active GeometryScene, avoiding two competing render submission paths.
- `cancelStage()`: retains abandoned staged GPU ownership.
- `collectAfterGpuIdle(device)`: requires real device-idle proof, shuts down old scenes, and retains any owner for which shutdown fails. This is conservative (can cause hitches), NOT yet asynchronous fence retirement.
- `shutdownAfterGpuIdle(device)`: explicit final cleanup after draw has stopped and device idle is proved. On failure the host must retain this CaveGeometrySwapOwner and retry; destructor alone is NOT an allowed GPU teardown guarantee.
- Pure `caveCanStageScene` and `caveCanActivateScene` guards in a self-contained no-RHI header drive these decisions.

## Tests actually executed
- **PASS:** Local isolated C++20 build of a faithfully reproduced `engine/render/TerraWeaveCavernsSwapPolicy.h` and `tests/terraweave_caverns_swap_policy_test.cpp` in `/mnt/data/caverns-stage24-check`.
  Command: `c++ -std=c++20 -Wall -Wextra -Werror -I/mnt/data/caverns-stage24-check /mnt/data/caverns-stage24-check/tests/terraweave_caverns_swap_policy_test.cpp -o /mnt/data/caverns-stage24-check/swap-policy-test && /mnt/data/caverns-stage24-check/swap-policy-test`.
  Actual console: `caverns stage24 swap policy PASS`. Covers monotonic revisions, stale staged revisions, owner cap, valid activation and invalid zero values.
- GitHub connector acknowledged all Stage24 source/test/skill commits.
- **NOT RUN:** complete AIVERSE checkout C++ compiler/linker, GeometryScene/STRATA real GPU upload, editor Scene integration, Metal/Vulkan/GLES screenshot, Box3D/SpatialWorld/navmesh transaction, device-idle failure injection, shaders or CMake/CTest. Do NOT claim these passed.

## Important limitations
1. This stage adds a real GPU swap **adapter** but does not yet wire it into the native editor's draw path. Editor Stage23 still runs CPU preparation only and explicitly shows `visualCommitConnected=false`.
2. A `hostCommit` callback must provide a genuine all-or-none graphics, SpatialWorld, Box3D and NavMesh visibility barrier and revision match. This class cannot infer or implement physics ownership. Do not call `activate([](auto){return true;})` without that proof.
3. `collectAfterGpuIdle` and `shutdownAfterGpuIdle` are blocking/idle-guarded by design. They are NOT the requested nonblocking GPU-fenced incremental streaming. Do not invoke every editor brush dab.
4. The actual `GeometryScene::loadPrepared` load path can fail after allocating resources. The owner retains a failed candidate instead of dropping it prematurely, but a lost device still requires careful recovery and a retained owner lifetime.
5. Source geometry may be rejected by STRATA and fall back to ordinary GeometryScene rendering; this is an opt-in source preference, not proof of STRATA selection.
6. Stage16 seam gate still fails closed for incompatible neighboring borders and Stage22 runtime bridge does not yet automatically schedule all mixed-LOD fallback jobs.

## Next concrete stage
1. Build/link the full AIVERSE repository and register/execute Caverns stages 1–24 CMake/CTest; fix missing source types, editor includes, GPU API mismatches before merge.
2. Implement a host-owned, genuinely staged cross-owner Render/SpatialWorld/Box3D/NavMesh swap API and hook Stage23 `CaveEditRuntimeBridge::publishReady` only when all subsystems are ready; observe GPU fences and avoid double draw.
3. Implement asynchronous GPU submission-fence retirement of whole GeometryScene owners (including STRATA sidecars). The current GeometryScene shutdown requirements impose a real idle/retirement constraint.
4. Connect native editor Scene display to the new active GeometryScene while keeping the previous resident visible until valid activation. Verify on Metal and a second backend.
5. Continue skill and dated handoff after every stage; do not merge main without actual acceptance evidence.
