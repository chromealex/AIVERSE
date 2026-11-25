# Windows MSVC portability — 2026-11-25

Branch: `integrate/pr203-msvc-20261125`, based on `origin/main` `b6cbc3133`. Area: Builds and platforms. Source: PR #203, `fix/msvc-windows-build-2`, code commit `43d93b72c`, handoff `docs/agent-handoffs/msvc-windows-build-2026-11-25.md`.

## Delivery record before implementation

PR #203 reports four Windows-only compiler failures: two oversized embedded shader literals, two locals named `near` shadowed by the Windows macro, and `MoveFile` macro expansion in the editor history header. The source branch has four code paths plus a separate handoff. Its diff applies cleanly to this main tree. The author reports successful MSVC 19.44 Release builds of `aiversemcp`, `aiversebuild` and `aiverse_studio`, a packaged Windows client smoke run and a live MCP request. Those are source-branch evidence, not checks rerun by this integrator. CTest, Mac quick and Linux were NOT RUN on that branch. Other known MSVC failures in `TurnSearch.h` and `NativeRegistrar` remain outside this bounded PR.

This integration will preserve existing runtime behavior, API, save formats and shader bytes. Roadmap item `Восстановить сборку инструментов и Studio в Windows MSVC` is in progress in `docs/devlog/systems.json`; feature manifest `docs/features/windows-msvc-portability.json` records applicability and evidence. No new authoring operation, UI, skill, MCP method, graph, example or visible effect is intended. Windows compilation and the mandatory merged-tree Mac quick gate are acceptance checks. Build outputs remain on KINGSTON.

Next: commit this preparatory record and roadmap, import the exact PR #203 code/handoff, verify shader byte identity and source checks, then run focused targets and the merged-tree Mac quick gate before squash integration.

## Preparatory stage handoff

Commit `bd73a8e0d` on `integrate/pr203-msvc-20261125` changed `docs/devlog/systems.json`, `docs/features/windows-msvc-portability.json`, and this handoff. `python3.12 tools/site/build.py check` PASS (72 entries, 0 errors); `git diff --check` PASS before commit. No native build or tests run. The manifest is intentionally pending and is not ready. Next: import PR #203 code and its original handoff, then verify exact behavior and test.

## PR #203 code stage handoff

Code commit `f83800b91` imported `apps/editor/core/ProjectDocumentJournal.h`, `engine/fx/SpectraGpuShaders.h`, `engine/game/MapGrid.h`, `engine/mechanics/MobBehaviour.h` and the original PR handoff `docs/agent-handoffs/msvc-windows-build-2026-11-25.md`. API, save format, shader semantics and runtime behavior are intended to remain unchanged. `git diff --cached --check` PASS before commit. Initial raw byte comparison of shader header against the Git blob failed because this checkout uses CRLF while the blob uses LF; the corrected comparison normalized line endings and verified that removing the four adjacent raw-literal boundaries restores the original text exactly: PASS. No native build or CTest run yet on this integration branch. The original PR's Windows MSVC results are retained as external source-branch evidence, not rerun by this integrator. Next: build the smallest affected targets, run focused tests, update the manifest and run the merged-tree Mac quick gate.

## Manifest classification stage handoff

Commit `74a7ffd8d` classified the existing Windows-only compiler repair in `docs/features/windows-msvc-portability.json`. The checks for behavior-preserving implementation, ownership, API stability, Windows source-branch builds, dependencies and documentation are supported by the source handoff and integration diff. New skill, MCP, Studio behavior, persistence, graph, example and publishing work are not applicable. The manifest intentionally leaves `tests` NOT RUN until a repository evidence report is recorded. On the same code tree, the mandatory Mac quick gate completed successfully: build PASS; CTest 551/551 tests passed, 0 failed, five GPU frame tests marked Skipped by CTest. Report: `/Volumes/KINGSTON/AIVERSE-builds/pr203-msvc-integration-20261125/mac/test-reports/20261125-150628-31388`. The focused Mac `aiversemcp` and `aiversebuild` targets also built; attempted `aiverse_studio` was unavailable without the Studio web distribution and a configure attempt with `AIVERSE_BUILD_STUDIO_HTML5=ON` failed for that missing bundle. The original source-branch Windows Studio build remains the Windows evidence. Next: commit this handoff, record focused CTest provenance, finalize the manifest and roadmap, then squash into main.

## Verified integration evidence stage handoff

Commit `6c6e4f43b` updated `docs/features/windows-msvc-portability.json`, `docs/devlog/systems.json` and recorded `docs/evidence/windows-msvc-portability-mac-20261125.{json,log.txt}`. The evidence command ran focused `spectrafx_contract` and `spectrafx_metal_gpu` CTests on this clean branch and exited 0; the evidence report binds its SHA and log digest. The mandatory merged-tree Mac quick gate on the unchanged code tree passed configure, build and CTest: 551/551 passed, 0 failed, five GPU frame tests Skipped, report `/Volumes/KINGSTON/AIVERSE-builds/pr203-msvc-integration-20261125/mac/test-reports/20261125-150628-31388`. `python3.12 tools/agents/check_feature.py` structural check and `python3.12 tools/site/build.py check` passed before this commit. Next: commit this separate handoff, run strict ready/compliance checks on a clean tree, then squash into current main and push.

## Evidence provenance correction handoff

Commit `62cc46d0e` replaced the focused evidence report and its manifest reference with `docs/evidence/windows-msvc-portability-final-mac-20261125.{json,log.txt}`. The first strict readiness check rejected the earlier report solely because `docs/devlog/systems.json` changed after its tested SHA. The focused CTest command was rerun on clean commit `fc04dd35f` after the roadmap update and exited 0. No source code changed after the full Mac quick gate. Next: commit this handoff, rerun strict readiness and compliance, then squash into main.

## Feature policy registration handoff

Commit `8d8f7a17a` changed `docs/agents/feature-policy.json` and `docs/devlog/systems.json` to register the Windows MSVC repair and map it to the Builds and platforms roadmap item. Strict `check_feature --ready` had rejected the previous clean tree with `unknown_dependencies` and `missing roadmap mapping`; these were structural registration gaps, not native test failures. No source code changed. Next: commit this handoff, rerun focused test evidence on the final metadata tree, update its manifest reference, then repeat strict checks.

## Final readiness provenance handoff

Commit `42893a90e` updated `docs/features/windows-msvc-portability.json` to reference `docs/evidence/windows-msvc-portability-ready-mac-20261125.{json,log.txt}`. Its focused SpectraFX CTest command exited 0 on clean commit `8abd4b737` after feature policy and roadmap mapping were fixed. The previous evidence was removed to avoid stale provenance. No source code changed after the 551/551 Mac quick gate. Next: commit this handoff and run strict feature readiness, compliance audit and site check before main.

## Roadmap link correction handoff

Commit `cc75523a1` changed only `docs/agents/feature-policy.json`: the Builds and platforms system ID is `build`, so the previous `agents` link was invalid. The strict readiness check identified this exact mapping error. No implementation or test code changed. Next: commit handoff, record focused evidence on the corrected metadata tree and rerun strict checks.

## Mapped-tree evidence handoff

Commit `b59d4839f` changed `docs/features/windows-msvc-portability.json` and `docs/evidence/windows-msvc-portability-integrated-mac-20261125.{json,log.txt}`. Focused SpectraFX CTest ran successfully on clean commit `4173ad935`, which includes the corrected `build` roadmap mapping. The previous evidence was removed. No source code changed after the full Mac quick gate. Next: commit handoff and run strict readiness/compliance checks.

## Exact changed-path scope handoff

Commit `ea3644f9f` added `docs/agents/feature-policy.json` to the feature manifest's exact `changed_paths`. Strict readiness passed before this stage, while the compliance audit reported that one policy path as uncovered. This stage changes only the manifest, which is allowed to follow the tested evidence SHA. Next: commit handoff, rerun both gates and site check, then squash into main.

## Current-main merge and evidence stage

Merge commit `562d48a2e` brought `origin/main` `f7bc9bc51` into this integration branch after the first Mac quick gate. Main added Mac client/Studio build repairs and test-runner dependencies; the merge was clean, including feature policy and roadmap. Because the actual main tree advanced, a second mandatory Mac quick gate was launched on this exact merged code tree at `/Volumes/KINGSTON/AIVERSE-builds/pr203-msvc-integration-20261125/mac/test-reports/20261125-154307-94664`. The focused `spectrafx_contract` and `spectrafx_metal_gpu` CTests passed on clean merge commit `562d48a2e`; their new provenance report is `docs/evidence/windows-msvc-portability-merged-mac-20261125.json`. The previous report was removed. The squash candidate tree was checked byte-for-byte against this branch's tree before this docs-only evidence update. Next: wait for the second full gate, commit this evidence stage and its separate handoff, update the squash candidate to the final docs tree, then push.

## Current-main evidence handoff

Commit `57558b725` changed `docs/features/windows-msvc-portability.json`, `docs/evidence/windows-msvc-portability-merged-mac-20261125.{json,log.txt}` and this handoff. The focused CTests exited 0 on clean merged code SHA `562d48a2e`. The full Mac quick gate for that same code is still running; no result is claimed yet. No source code changed after launching the gate. Next: commit this separate handoff, finish the gate and validate strict readiness/compliance before squash.

## Final merged-tree Mac gate and main integration handoff

On current `origin/main` `f7bc9bc51` plus this integration branch, the mandatory `./run-tests.sh --quick --build-dir /Volumes/KINGSTON/AIVERSE-builds/pr203-msvc-integration-20261125/mac --jobs 12 --ctest-jobs 8` passed configure, build and CTest. Report: `/Volumes/KINGSTON/AIVERSE-builds/pr203-msvc-integration-20261125/mac/test-reports/20261125-154307-94664`; 551/551 tests passed, zero failed, five GPU frame tests marked Skipped. The code tree was commit `562d48a2e`; later commits changed only feature manifest, evidence and handoff files. Strict `check_feature --ready` and `audit_compliance --base origin/main` both returned zero errors on commit `ca839c2a9`; site check returned 72 entries, zero errors on the matching squash candidate. The source branch's reported Windows MSVC builds were not independently rerun by this integrator. Next: squash this branch into main as one commit, push without force, and close PR #203 as integrated.
