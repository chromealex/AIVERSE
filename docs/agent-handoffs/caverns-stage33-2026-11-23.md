# TerraWeave Caverns — Stage 33 build evidence handoff (2026-11-23)

## Branch and commits
Repository: `chromealex/AIVERSE`; feature branch: `feature/terraweave-caverns-core-20261123`; **NOT merged to main**.

Stage33 commits:
- `8ce09e2cf9921220a1f39689358a0a54adc7f5e7`: new `tests/caverns/CMakeLists.txt` containing four dependency-light Caverns tests.
- `6fa8ec0e56181c934c703c4df9190d6e568e2253`: root `CMakeLists.txt` registers the suite under `AIVERSE_BUILD_TESTS AND NOT EMSCRIPTEN`.
- `8bd4786a1e9675e2d94de7551213680e6beb8260`: new `tools/caverns-stage33-verify.sh`, native acceptance script with provenance and error-carrying logs.
- `a721e7cc5ed6beb7a2041a315934adc77038d26d`: updated `skills/world/aiverse-terraweave-caverns/SKILL.md`.
- This handoff is committed next; inspect branch HEAD for final SHA.

## Goal and actual changes
The feature branch now exposes a CMake/CTest suite for four standalone C++20 contracts:
- `tests/terraweave_caverns_mesh_test.cpp` — deterministic sampled/marching tetrahedra cave mesh.
- `tests/terraweave_caverns_swap_policy_test.cpp` — revision and GPU owner budget guards.
- `tests/terraweave_caverns_owner_barrier_test.cpp` — four-owner staging bookkeeping, stale ticket rejection.
- `tests/terraweave_caverns_preview_pacer_test.cpp` — at-most-one upload/frame and cooldown.
The tests are registered via `tests/caverns/CMakeLists.txt`, preserve asserts under Release, are labelled Caverns/TerraWeave/CPU and have no optional native Box3D/Renderer dependency.

`tools/caverns-stage33-verify.sh` is intended to be run IN A FULL NATIVE AIVERSE CHECKOUT: it produces git provenance, configures Debug, compiles the four targets and runs their CTest labels; setting `AIVERSE_CAVERNS_FULL_BUILD=1` also builds all configured targets and runs full CTest. It sets `AIVERSE_FETCH_TEMPLATES=OFF` to avoid surprising network fetches. The script does NOT edit or merge main.

## Verified versus unverified
- **PASS in locally reconstructed minimal CMake harness:** CMake 3.31.6 configured successfully, `cmake --build ... -j4` succeeded and `ctest -L Caverns --output-on-failure` reported:
  `4/4 tests passed, 0 tests failed` (mesh 0.28 s; swap-policy/owner-barrier/preview-pacer each ~0.00 s).
  Reconstructed tree: `/mnt/data/caverns-stage33-smoke`, containing locally saved copies of four CPU test sources/headers from earlier stages. Actual `ctest` output captured in `/mnt/data/caverns-stage33-smoke/ctest.log`.
- Independently `clang++ -std=c++20 -Wall -Wextra -Werror` recompiled and executed the local Stage2 mesh test and Stage32 preview pacer test, both PASS. Previously built Stage25 owner-barrier binary was executed and printed PASS.
- **Full AIVERSE clone failed:** `git ls-remote https://github.com/chromealex/AIVERSE.git HEAD` returned `fatal: unable to access ... Could not resolve host: github.com`. Thus the actual full repository was not cloned to this container, and the native script could not run against it.
- GitHub connector `get_commit_combined_status` and `fetch_commit_workflow_runs` for the Stage32 SHA returned empty results; no CI success was inferred.
- **NOT RUN:** full AIVERSE CMake configure, native editor C++ compile/link, Stage17–32 integration test executables, real Box3D/SpatialWorld/NavMesh, GeometryScene/STRATA GPU on Metal/Vulkan/GLES, Scene screenshots, device-loss/fence-retirement tests. Stage33 does not establish that any of these systems work in a full build.

## Existing technical issues to verify in a full checkout
1. The new four smoke targets are the *first* CMake registration, not 32 stages of end-to-end test coverage. Stage27–31 tests need actual renderer/Box3D/nav implementation libs and possibly platform conditional handling. Add them incrementally after real linking is available.
2. `CaveEditRuntimeBridge` and `CaveShardGpuHost` remain sensitive to graphics device ownership, source revision and seam certification; Editor Stage31 visual-only mode intentionally does not publish Box3D, SpatialWorld or NavMesh.
3. Stage32's one-upload-per-frame pacing limits calls, not GPU upload duration or bytes. The renderer's STRATA sidecars may still require true device idle at retirement. Test actual editor frame times and resource lifetime after project switch.
4. Physics state publication still lacks guaranteed four-owner atomicity; Stage25 barrier is readiness bookkeeping and Box3D creates active bodies on `Scene::create`. Never mark game edit committed from a visual-only callback.
5. Run CMake in the real repo and fix actual compilation errors before merging. Preserve the dated handoff and skill updates at EVERY stage.

## Next concrete step
On an authorized Mac full checkout of branch `feature/terraweave-caverns-core-20261123`, run:

```bash
bash tools/caverns-stage33-verify.sh
# Then full native build + all available CTest:
AIVERSE_CAVERNS_FULL_BUILD=1 bash tools/caverns-stage33-verify.sh
```

Review `build/caverns-stage33/acceptance/` logs. Fix native compiler errors, register remaining Caverns integration tests with proper targets, verify actual editor visual capture / Box3D collisions / authored NavMesh gameplay before a main merge. Maintain `skills/world/aiverse-terraweave-caverns/SKILL.md` and a new dated stage handoff for each next stage.
