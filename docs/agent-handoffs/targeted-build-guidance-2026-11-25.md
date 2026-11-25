# Targeted build guidance — 2026-11-25

- Owner/branch: Codex, `feature/aiv20261125_targeted_build_guidance`.
- Documentation commit: `60636de33`.
- Paths: `AGENTS.md`, `docs/agents/contracts/build-testing.md`.
- Change: agents select affected build targets and tests for local verification, reuse compatible KINGSTON CMake trees and their object/static-library outputs, may use an optional KINGSTON compiler cache, and preserve the full merged-tree Mac quick gate before `main`.
- Architecture/API/save compatibility: documentation only; no runtime, public API or data-format change.
- Verification: `git diff --check` PASS before commit; native build and CTest NOT RUN because no executable code changed. No CI requested or run.
- Next step: integrate the documentation branch via the required squash process, including the merged-tree Mac quick gate before `main`.

## Integration attempt — 2026-11-25

- Squash staged on `main` at `73a749b64`; staged tree contained only this branch's three documentation files. No squash commit was made. The staged changes were removed after the gate failed.
- Mac gate command: `TMPDIR=/Volumes/KINGSTON/AIVERSE-builds/targeted-build-guidance/tmp CLANG_MODULE_CACHE_PATH=/Volumes/KINGSTON/AIVERSE-builds/targeted-build-guidance/modules ./run-tests.sh --quick --jobs 8 --build-dir /Volumes/KINGSTON/AIVERSE-builds/targeted-build-guidance/all-tests`. Configure PASS, build PASS, CTest FAIL (exit 8): 1 of 481 tests failed (`aiverse_tests`), 1 skipped (`lens_metal_frame`). Report: `/Volumes/KINGSTON/AIVERSE-builds/targeted-build-guidance/all-tests/test-reports/20261124-210519-69794`.
- `aiverse_tests` failed project creation and game wizard checks. The checkout has a `templates/` directory containing no game templates; `templatesDir()` selects it ahead of the installed `~/.aiverse/packages/templates`. No executable code was changed by this branch.
- Retried CTest with `AIVERSE_TEMPLATES_DIR=/Users/aleksandrfeer/.aiverse/packages/templates` and `--no-build` against the same binaries. `skill_library_isolation` timed out at 601.27 seconds while several other Mac gates were running concurrently. The already red retry was stopped; CTest exit 130. Report: `/Volumes/KINGSTON/AIVERSE-builds/targeted-build-guidance/all-tests/test-reports/20261124-220339-93802`. The `aiverse_tests` result from this retry is NOT RUN to completion.
- Blocker: the mandatory merged-tree Mac quick gate is not green. Next: rerun the quick gate with the explicit template environment when concurrent builds have ended, then squash this branch into up-to-date `main` only after a full green result.

## Follow-up scope and acceptance — 2026-11-25

- In progress: repair template directory selection so an empty repository `templates/` directory cannot hide installed package templates. Keep explicit `AIVERSE_TEMPLATES_DIR` authoritative and retain packaged engines that ship actual templates beside the engine.
- Acceptance: a direct native probe resolves installed templates with an empty engine-local folder; packaged local templates still resolve; the exact merged tree passes the required Mac quick gate. Record real results below before integration.

## Template selection repair — 2026-11-25

- Code commit: `77d78682b`; path `engine/platform/TemplatesDir.h`. Prefer a configured directory only when it contains a project template; otherwise use an engine-local directory only when it contains a project template. Explicit environment override remains authoritative. Public API and save formats are unchanged.
- Native probes: two `clang++ -std=c++20 -Wall -Wextra` stdin builds and executions, both exit 0. First checked an empty local folder resolves `/Users/aleksandrfeer/.aiverse/packages/templates` and an explicit environment override wins. Second checked a packaged engine with `templates/sample/game.json` resolves its local directory when the configured path is absent. Binaries and temporary files: `/Volumes/KINGSTON/AIVERSE-builds/targeted-build-guidance/probe/`.
- `git diff --check` PASS. Full CMake/CTest on this code SHA NOT RUN yet. Next: verify the exact combined tree on Mac before main integration.

## Updated-main gate attempt — 2026-11-25

- Synced branch with `origin/main` at `8511f3094`; staged a squash on that exact `main` tree, then ran `./run-tests.sh --quick --jobs 4 --ctest-jobs 1 --build-dir /Volumes/KINGSTON/AIVERSE-builds/targeted-build-guidance/all-tests` with KINGSTON `TMPDIR` and `CLANG_MODULE_CACHE_PATH`.
- Configure PASS. Build stopped at about 27% with exit 1 when KINGSTON reached less than 1 GiB free and the entire build directory disappeared during concurrent external cleanup. The runner's `build.log` and report were removed with the directory, so the final compiler line cannot be recovered. CTest NOT RUN. This is not a green gate.
- KINGSTON later recovered to about 84 GiB free, but two other full Mac gates were already running from fresh trees. The staged squash was removed from `main`; the source branch and code/test evidence remain intact.
- Next: run one isolated full quick gate from the current `main` plus this branch when the other gates have finished and disk space is stable. If green, squash into up-to-date `main` and push. Do not reuse the deleted build tree or treat the direct probes as a substitute for the gate.

## Mac gate follow-up — 2026-11-25

- Synced the staged squash to `origin/main` at `86cd5c08e`. The full quick gate configured successfully and built to 89%, then failed because Studio Metal lighting and SpectraFX preview test targets could not find `box3d/box3d.h` through `Scene.h`. CTest NOT RUN. Report: `/Volumes/KINGSTON/AIVERSE-builds/targeted-build-guidance-final/all-tests/test-reports/20261125-094305-57651`.
- Synced to `origin/main` at `5abd584e2`. Added the existing Box3D public include directory to those two Studio test targets in `apps/studio-native/CMakeLists.txt`. Next: rerun the full merged-tree Mac quick gate and integrate only after green results.

## Full gate and focused repairs — 2026-11-25

- Full Mac quick gate on staged tree at `5abd584e2`: configure PASS, build PASS, CTest FAIL (576/578 executed, two failed; `lens_metal_frame` skipped). `aiverse_tests`, `studio_metal_lighting`, `studio_spectrafx_preview` and `skill_library_isolation` passed. Failures: `studio_session` could not resolve an engine-local `fixture` template, and `studio_render_lighting` rejected a root-level map save. Report: `/Volumes/KINGSTON/AIVERSE-builds/targeted-build-guidance-final/all-tests/test-reports/20261125-102445-86025`.
- Template repair: prefer an engine-local `templates/` directory when it contains actual `*/game.json`; use configured template directory if the local directory is empty. This preserves packaged and test-local templates while avoiding the original empty-directory shadow. Focused `studio_session` CTest PASS (13.19 seconds).
- Studio lighting fixture now creates `maps/map.json` and references that path from `game.json`, matching the coordinated save contract. Focused `studio_render_lighting` CTest PASS (4.53 seconds). Its failure message now exposes the returned error.
- Next: full Mac quick gate on the final updated merged tree, then squash and push only if green.

## Final merged-tree Mac gate — 2026-11-25

- Exact staged merged tree on `main` at `942b03ba9` passed `TMPDIR=/Volumes/KINGSTON/AIVERSE-builds/targeted-build-guidance-final/tmp CLANG_MODULE_CACHE_PATH=/Volumes/KINGSTON/AIVERSE-builds/targeted-build-guidance-final/modules ./run-tests.sh --quick --jobs 4 --ctest-jobs 1 --build-dir /Volumes/KINGSTON/AIVERSE-builds/targeted-build-guidance-final/all-tests`: configure PASS, build PASS, CTest PASS. 578 tests listed, 0 failed; `lens_metal_frame` skipped. `studio_render_lighting` and `studio_session` both passed. Report: `/Volumes/KINGSTON/AIVERSE-builds/targeted-build-guidance-final/all-tests/test-reports/20261125-114427-83154`.
- The sole post-gate edit is this evidence paragraph in the handoff; executable source and test fixture are unchanged. Next: one squash commit on `main`, then push without force.
