# Review fixes: CI / build / tests hygiene + LENS cameras + client shutdown (2026-11-21)

Branch: `feature/aiv20261121_fix_ci_lens` (worktree `.worktrees/feature/aiv20261121_fix_ci_lens`),
base `origin/main` 973b32970. Not pushed, not merged.

## Commits

| SHA | Finding | Summary |
|---|---|---|
| 5c06e8f | 1 | ci.yml: full CTest on Linux (xvfb-run), Windows MSVC, macOS; post-merge validation runs on every push to main |
| f043ad4 | 11 | studio-html5 CI: + studio_render_lighting, studio_material_schema |
| 2c79b07 | 2 | `aiverse_keep_test_asserts()` CMake helper (-UNDEBUG + MSVC /UNDEBUG); asserts -> AIVERSE_TEST_CHECK in two tests |
| 06f7dcb | 3 | run-tests.sh: empty array under `set -u` on bash < 4.4, suites get `</dev/null` |
| fc0ef18 | 4 | removed 18 `*.studio-v12.bak`, `.gitignore` `*.bak`, `*.studio-v*.bak` |
| 93048c4 | 7 (pool part), 10 | RT pool: creation-failure backoff, 64 Mpix budget, dtor leak warning, `<stdexcept>`; stale screen-composite RTs released; `<algorithm>` in CameraFrameRecorder.h |
| 2164adc | 7 (loader part), 8 | loadCameraScene rejects w xor h = 0, duplicate texture keys, over-budget pixels; mid-way attach failure leaves no partial World |
| 5c454d0 | 5 | LENS writes: opaque `revision` + `expectedRevision`; `expectedScene` compared by value; new `apps/editor/core/LensOps.h` shared helper; docs/skill |
| bf63c8b | 6 | LENS / Cameras window saves via the shared helper (revision check, validation, symlink refusal, last-good kept, Overwrite on conflict) |
| 469bec9 | 9 | GameClient::shutdown logs and leaks the LENS RTs on idle failure instead of throwing |

## Findings

1. **FIXED** (HIGH). `.github/workflows/ci.yml`: the `Tests` step of linux (3 matrix
   entries), windows-msvc and macos now runs
   `ctest --test-dir <build> [-C Release] --output-on-failure --no-tests=error -j<n> -LE "$CTEST_GPU_LABELS"`
   with `CTEST_GPU_LABELS='^(GPU|METAL|Metal|MetalGPU)$'` (the only GPU-device labels in
   CMake; matched per label). Linux is wrapped in `xvfb-run -a` (xvfb already installed).
   `aiverse_tests` is itself a CTest case, so its direct run was replaced. All tests are in the
   default build target (no EXCLUDE_FROM_ALL on test executables). Local configure: 343 tests
   registered on the Linux default config, none with a GPU label there (vulkan_backend skips
   without a driver; lavapipe is installed in CI).
   **MinGW/Wine job keeps only `wine aiverse_tests.exe`** (DEFERRED): the other CTest cases are
   Python drivers / tools that would invoke the Windows .exe without Wine; the MSVC job covers
   Windows. Expect the first CI runs to surface tests that never ran in CI before (that is the
   point); exclude only by label, do not regex-skip failures.
   `aiverse-postmerge-full-validation.yml`: `paths` filter removed (runs on every push to main
   and on dispatch), `--no-tests=error` added. It still runs GPU-labelled tests (unchanged).
   Workflows that only trigger on push to a feature/integration branch (no pull_request
   trigger), effectively dead once that branch is merged / gone - **not deleted**, owner's call:
   `physis-terraweave-onepass.yml` (integration/physis-terraweave-complete-20261119),
   `prisma-final-validation.yml` (feature/prisma-postfx, + dispatch),
   `prisma-motionweave-integration-validation.yml` (integration/prisma-motionweave-view, + dispatch),
   `spectrafx-validation.yml` (feature/spectrafx-generic-cue-queue, + dispatch).
   Their branches are not present in this clone (only `origin/main` fetched), so "merged" could not be
   checked here. Workflows with push to a feature branch **and** pull_request paths
   (ecs-experiments, enhanced-input, nereid-regression, physis, physis-journal,
   spectrafx-diagnostics, studio-html5-web, terraweave, texture-rhi) still fire on PRs.
2. **FIXED** (MED). New `aiverse_keep_test_asserts(target)` (CMakeLists.txt, before the first
   test block): `$<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-UNDEBUG>` +
   `$<$<CXX_COMPILER_ID:MSVC>:/UNDEBUG>`. All 15 old per-target copies (GCC/Clang only)
   replaced; added for `aiverse_camera_model_snapshot_tests`, `aiverse_view_camera_lod_tests`,
   `aiverse_vfx_spfx_diagnostics_test`. `tests/render/camera_model_snapshot.cpp` (36) and
   `tests/vfx_spfx_diagnostics_test.cpp` (53) asserts -> `AIVERSE_TEST_CHECK`
   (`tests/AlwaysCheck.h`). camera_lod.cpp asserts left to the PRISMA agent.
3. **FIXED** (LOW). `run-tests.sh`: `${suite_args[@]+"${suite_args[@]}"} </dev/null`.
4. **FIXED** (LOW). 18 `*.studio-v12.bak` removed (no reference: `git grep studio-v12` empty;
   CMake globs are `*.cpp` / `*.lua`). `.gitignore`: `*.bak`, `*.studio-v*.bak`.
   Left untouched (owner's decision): `ratings.txt`, `reusable-genre-aiverse.patch`,
   `reusable-libraries-foundation.patch` in the repo root.
5. **FIXED** (MED). `lens_scene_get` returns `revision` (FNV-1a 64 hex of the file bytes,
   `"missing"` without a file). `lens_scene_set` / `lens_camera_set` / `lens_camera_delete` accept
   `expectedRevision` (preferred) or `expectedScene`; `expectedScene` is compared by
   `lens::sameScene` (members sorted by key, numbers by literal or numeric value) - Json.cpp not
   touched. The revision is checked again right before the staged rename; successful writes return
   the new `revision`. JSON schema: `expectedRevision` / `expectedScene` no longer in `required`
   (code requires one of them). Old callers that send `expectedScene` keep working.
   Docs: `skills/visuals/aiverse-lens/SKILL.md`, `docs/camera-system.md`, one instruction string in
   `tools/mcp/main.cpp` (outside the area, minimal). AGENTS.md line ~5479 still says
   "exact expectedScene" - left for the integrator (shared file).
6. **FIXED** (MED). `StudioCamerasWindow::save()` uses `lens::saveScene(project, scene,
   loadedRevision_)`; `load()` uses `lens::readSceneFile` (1 MiB, symlink refusal). On a changed
   file nothing is written and the window offers Reload / Overwrite; a file that failed to load
   can only be replaced with Overwrite. No fixed `.tmp`, never deletes the last-good file.
7. **FIXED** (MED). Loader (`engine/sim/CameraSceneAsset.h`): width xor height = 0, duplicate
   texture output keys and summed texture pixels > `maxTotalTexturePixels` (new trailing default
   parameter, `kCameraSceneMaxTexturePixels` = 64 Mi px) fail before any Entity is created.
   Runtime (`engine/render/RenderTargetPool.h`): live-pixel budget (`setPixelBudget`, default
   `kDefaultPixelBudget` = 64 Mi px = one 8192^2 target; replacing a key counts only the new
   size); a key whose color or depth `createTexture` failed is refused for the same spec without
   calling the device until the spec changes, `release(key)` or `retryFailedTargets()`.
   Applies to every caller of acquire (pipeline, captures, panorama, HDR resolve).
8. **FIXED** (LOW). Mid-way `SceneHierarchy::attach` failure: the new entity and all entities
   created by this call are detached + destroyed, `*created` restored to its previous size.
9. **FIXED** (LOW). `GameClient::shutdown()`: idle failure or a throwing LENS shutdown is logged
   (`[lens] shutdown ...`), the LENS RT teardown is skipped (leak), the rest of the shutdown runs.
10. **FIXED** (LOW). Pool destructor: if not shut down, prints a leak warning and does not
    destroy (GPU idle unknown). `<stdexcept>` in RenderTargetPool.h, `<algorithm>` in
    CameraFrameRecorder.h. CameraRenderPipeline: after each execute, screen-composite RTs whose
    entity is no longer a composited screen camera are released via `releaseExcept` (GPU-fenced
    retirement; destroyed at a later `collectRetiredTargets()`).
11. **FIXED** (LOW). studio-html5 `-R` list + `material_schema`, `render_lighting`
    (`aiverse_studio_render_lighting_tests` added to the build list), `--no-tests=error`.
    `studio_metal_lighting` stays out: opens an NSWindow and needs a Metal device.
12. **NOT CHANGED** (as instructed): `apps/studio-native/CMakeLists.txt` FATAL_ERROR
    "HTML5 bundle missing: build on KINGSTON" enforces the owner's build-location rule.
13. Other agents' CMake registrations: not done here (integrator).

## Verification actually run (this Linux sandbox, no full build)

- `cmake -S . -B /tmp/cilens-cfg -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release` (configure only,
  cmake 3.28): OK; `ctest -N`: 343 tests; `ctest --print-labels`: no GPU labels on this config;
  `UNDEBUG` present in flags.make of all 18 helper targets.
- YAML parse (`python3 yaml.safe_load`) of ci.yml, postmerge, studio-html5-web.yml: OK.
- `bash -n run-tests.sh` OK; standalone bash repro (2-line manifest, suite that reads stdin,
  empty args under `set -Eeuo pipefail`): both suites run.
- `g++ -std=c++20 -O2 -DNDEBUG -Wall -Wextra -I. tests/vfx_spfx_diagnostics_test.cpp` + run: PASS.
- `g++ -std=c++20 -O1 -DNDEBUG -Wall -Wextra -I. -Ilibraries/include -Ithird_party/meshoptimizer/src tests/render/camera_model_snapshot.cpp` + run: PASS (rc 0).
- `g++ -std=c++20 -O1 -Wall -Wextra -I. -Ilibraries/include tests/render/camera_rt_pool.cpp` + run
  (extended: backoff, budget, releaseExcept): PASS.
- `g++ ... tests/render/camera_scene_asset.cpp engine/data/Json.cpp engine/core/Fixed.cpp` + run
  (extended: duplicate key, w xor h, 0x0, budget, mid-way attach failure): PASS; with the old
  header the new test aborts at "duplicate texture key rejected".
- `tests/lens_ops_test.cpp` built with `apps/editor/core/LensOps.cpp`, Json.cpp, Fixed.cpp and a
  test-only Workspace op-registry stub (registerOp/call/ctor; not committed) + run: PASS
  ("PASS LENS operations ..."). Separate helper repro (conflict, external edit, invalid write,
  overwrite, symlink, Json::operator== vs sameScene on a reordered/respelled scene): PASS.
- `-fsyntax-only -Wall -Wextra` with the real target include/defines: LensOps.cpp
  (aiverse_editor_core), StudioCamerasWindow.cpp (aiverse_editor), GameClient.cpp
  (aiverse_client), a TU including CameraRenderPipeline.h + CameraFrameRecorder.h: no errors or
  warnings in changed files.
- `python3 tools/skills/catalog.py check` and `tools/skills/audit.py --strict`: 0 errors.

## UNVERIFIED

- Full CMake build, full CTest, the new CI jobs themselves (first run may expose long-unrun
  failing tests; Windows/macOS ctest never run here), MSVC `/UNDEBUG`.
- `lens_mcp_stdio` (needs aiversemcp), real Workspace.cpp link of lens_ops_test, editor ImGui
  interaction, client shutdown at runtime, CameraRenderPipeline runtime (GPU), Metal/GPU tests.
- No Metal / .mm / shader files were changed.

## CMake

No new test registrations needed from this branch (only existing tests were extended).
CMakeLists.txt changes on this branch: helper `aiverse_keep_test_asserts` + its 18 call sites.

## Next step

Integrator: merge; watch the first `CI` run of the ctest steps and triage newly visible
failures by owner (do not widen `-LE`); update the AGENTS.md LENS line (~5479) to mention
`expectedRevision`; decide on the dead branch-only workflows listed in finding 1.
