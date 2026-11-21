# STRATA Stage20 — batching integration (2026-11-20)

## Source and scope
User authorized integrating batching into STRATA and main only when ready.
Branch: `codex/strata-batching-integration`, isolated worktree `/private/tmp/aiverse-strata-batching`.
STRATA source: `60820d5485696053e30ccb9e769f06fcc19d782e` (PR70).
Batching source: `b2b5c180d8cf1fa0b3c87b445167a6d77a7d0057` (Stage142 / PR94, including its ancestor stack).
Implementation merge: `8d383ba84f54f2b90e19cd354f3744f56efd713a`.

## Conflict review
Only Gfx.h and GfxMetal.mm conflicted. Preserved both independent additive APIs:
STRATA/main GPU scope timing and batching model-pass raw GPU counters; preserved both cmath/iterator includes.
Renderer3D.h/Renderer3DModels.cpp merged automatically; further native validation required.
No production flags, gameplay/save formats or existing API semantics intentionally changed.
Batching experiments remain opt-in; Stage142 reports GPU regression, not a universal performance win.

## Verification
`git diff --check`: PASS. Native build, CTest and current combined GPU tests: NOT RUN yet.
Prior STRATA Mac results and batching Stage142 captures apply to their original trees only.
CI for old PR70 was still queued at inspection; no CI pass claimed.

## Next step
Merge latest origin/main after this handoff commit; review shared rendering conflicts, build and run native tests, record exact tested SHA and results, then fast-forward the existing STRATA integration branch. Main merge remains conditional on readiness. Preserve uncommitted work in the primary Studio checkout.

## Stage20.1 — current main reconciliation
Main `5aba82c35` merged without textual conflicts in implementation `5cd7055b08a9046bd5497646c57609579f75972b`. Preserves physical sky/local fog and Release retirement test fixes. No manual source edits in this merge. Native combined checks are next; NOT RUN at this checkpoint.

## Stage20.2 — fix observed GCC CI blocker
Implementation `1b8b58cabfe5d27b278a73344dc4894708d562e8` removes unused anonymous `validId` and the unused Editor parameter from SpectraGraphWindow::properties, including its only call. No validation behavior or UI changes: the removed helper had no callers. Exact failing CI: https://github.com/chromealex/AIVERSE/actions/runs/44733446948/job/136278777703 on STRATA source 60820d548; downloaded raw log `/private/tmp/strata-arm64-failure.log` reports -Werror=unused-function and -Werror=unused-parameter at lines 38/447. `git diff --check` PASS. Combined Mac Debug build is in progress; Linux GCC rerun NOT RUN. Next: finish build/CTest and publish branch for fresh CI.

## Stage20.3 — native build interruption and KINGSTON relocation
The first combined native Debug build through `bash run-tests.sh --quick --build-dir /private/tmp/aiverse-strata-batching-build --jobs 8` stopped at 76% with linker errno=28 and `No space left on device`. CTest NOT RUN. Six Python self-tests passed: strata-stage18-image-audit and batching-stage129/132/137/139/141-analyze, each with `--self-test`.
An attempted jobs=4 continuation was stopped after the user required **all builds on KINGSTON**. Build files/logs moved to `/Volumes/KINGSTON/AIVERSE-builds/strata-batching-20261120/`; the old cache is retained under `interrupted-debug` for evidence only. Some old debug symbols were stripped to reclaim internal-disk space; do not reuse that interrupted build for final validation.
Next: fresh `debug` build under KINGSTON, Debug assertions enabled with `CMAKE_CXX_FLAGS_DEBUG=-g0` and `CMAKE_C_FLAGS_DEBUG=-g0` (only debugger symbols omitted), STRATA/Metal/HDR fixtures ON. All future build output for this task belongs on KINGSTON. Current Linux ARM64 CI on source 5a0acb77f is running; others queued, no pass claimed.

## Stage20.4 — native acceptance before concurrent Texture RHI main
Tested source `d8e083f6fc3b958698280f2b3eecbc316176e616` (runtime code from 1b8b58cab). Full Debug build PASS without compiler warning/error lines. All 6 STRATA native Metal executables PASS, including real depth/reflection and temporal selection. Material batching unit test: 38 checks PASS plus mesh/coverage checks. CPU MDI (2 submissions/64 commands), static packets (1 hit), GPU ICB (1 GPU-authored submission/32 commands) all PASS with changed_channels=0 and no pipeline failures. Reflection fixture PNG visually inspected. Seven offline Python analyzer/comparator self-tests PASS.
Artifacts/provenance: `/Volumes/KINGSTON/AIVERSE-builds/strata-batching-20261120/gpu/`. Full CTest on the same binaries has 279/280 PASS; `aiverse_tests` still running with active CPU, so full suite is NOT yet declared PASS. The first CTest run was stopped to enforce TMPDIR on KINGSTON for nested builds; authoritative rerun report: `debug/test-reports/20261120-142758-75504/`.
During validation main advanced to `1dc657128` through Texture RHI PR86/95. Read-only merge preflight found incompatible Vulkan compute implementations (STRATA buffer/push-constant and texture storage/sample/uniform layouts), plus additive shared RHI conflicts. No generic backend may be discarded. Next stage must reconcile layouts explicitly, preserve STRATA async readback/barriers and texture compute, then rebuild/retest the new combined source. Linux ARM64 CI for 5a0acb77f is still building; other jobs queued.

## 2026-11-20 — concurrent Texture RHI main integration

- Branch: `codex/strata-batching-integration`, publishing to PR #70 STRATA branch. Merge code: `eaa52a7ee888d9ee513f99aed3049b2c154ccc4c`; main parent `1dc657128` (Texture RHI PR #95).
- Preserved Metal depth/color diagnostics alongside texture readback, Renderer3D timing/cache fields, and generic Texture RHI features. Vulkan uses one pipeline pool and explicit appended `ComputeDesc::vulkanLayout`: default TextureResources retains image0..3/sample4..11/buffer12..15/uniform16 ABI; BufferPushConstants preserves STRATA storage0..7/push128 ABI. STRATA opts into the latter. Checked dispatch, async buffer readback and transfer/compute/indirect barriers preserved. No save changes; existing three-field ComputeDesc initializers retain default texture ABI; experimental production flags remain OFF.
- Added same-device/same-frame Vulkan compute ABI coexistence regression (37+5 readback and rejected-slot dispatch). Vulkan native GPU execution NOT RUN yet.
- Previous code d8e083f6 completed `run-tests.sh --quick --no-build ... --ctest-jobs 4`: 280/280 PASS, 907.40 s. This predates Texture integration and is not evidence for this merge.
- KINGSTON configure with Vulkan ON and seven Texture Metal GPU test options PASS. First incremental build FAILED: missing Vulkan drawIndexedGpuIndirect declaration during header conflict resolution, restored before this commit. Rebuild and final tests pending. Logs `/Volumes/KINGSTON/AIVERSE-builds/strata-batching-20261120/texture-*.log`.
- CI run https://github.com/chromealex/AIVERSE/actions/runs/44750116787 at prior head5a0acb77 failed ARM64 compilation on unchecked PHYSIS test pipe read/write results; other jobs queued, not passing. Next step: fix those six ignored results, rebuild on KINGSTON, rerun all applicable native and Metal tests, inspect fresh CI before main merge.

## 2026-11-20 — GCC test compile blockers

Code `62b295196a0e5c600bdf59cf3a781b391c169d8f`, same integration branch: tests/physis_admin_journal_test.cpp and tests/physis_journal_crash_recovery_test.cpp check one-byte pipe reads/writes, retry EINTR, reject short/failed results; child fails before waiting if status delivery fails. Removed unused anonymous EXR u64 fixture helper reported by Clang. No engine/public API/save changes. Evidence: ARM64 CI job136335497364 errors -Werror=unused-result; KINGSTON texture-build.log unused-function warning. Rebuild/tests pending, NOT yet passing. Next: full KINGSTON build, affected PHYSIS/EXR tests and merged renderer acceptance.

## 2026-11-20 — pending Vulkan readback lifetime

Code `3b212557766e7afd37234feb02d60aa57085fd71`: avoid self-move of PendingComputeRead when no completed entries precede a still-pending read. A self-moved std::function need not preserve its callback; retain unchanged entries in place. Resource ownership, serial completion and public API unchanged. Existing/new Vulkan parity fixtures require actual callback delivery after repeated frames; runtime GPU check pending CI, native compilation underway on KINGSTON. Next step unchanged: full integration acceptance and Linux Vulkan CI.

## 2026-11-20 — indexed VT follow-up from concurrent main

Merge `e077940b85a6b09b6b8e01d16048c9ce90bdf062` includes main `d19180bc6c9ef9d79dce646d7c006854ee928663` (PR #96), conflict-free. Changes: Renderer3D rejects indexed VT before legacy ICB admission; strengthened bilinear and indexed-fallback GPU oracles; eight Texture GPU CTests are serial. No new production default/API/save changes. Read indexed-admission handoff first.

Before this merge, source d52dbd0c0: full Debug build with Vulkan backend PASS with no warnings; 6 STRATA Metal + batching unit/CPU MDI/static packets/GPU ICB + 7 Texture Metal tests = 17/17 PASS, actual pixel/ICB execution markers inspected. Reflection PNG visually inspected. Evidence `gpu-final/`, `final-build.log` on KINGSTON. Full CTest currently 308/309 PASS; only aiverse_tests still running against the pre-merge binary (do not claim final completion yet). Local strata_vulkan_kernel compiles GLSL but GPU runtime SKIP: loader not found. Linux CI ARM64 compiling; focused Vulkan queued.

Next: rebuild affected Renderer3D/GPU binaries on KINGSTON and rerun 6 STRATA + 4 batching + 8 Texture cases, collect pending full regression and CI, then final integration handoff. Do not confuse the pending old-binary CTest run with this delta validation.

## 2026-11-20 — final local acceptance and publication boundary

Final runtime merge `e077940b85a6b09b6b8e01d16048c9ce90bdf062`, verified source `3d9a5c1550441a861df3a73e03be5ec6f1ccc903`; prior implementation fixes `eaa52a7ee888d9ee513f99aed3049b2c154ccc4c`, `62b295196a0e5c600bdf59cf3a781b391c169d8f`, `3b212557766e7afd37234feb02d60aa57085fd71`. Branch `codex/strata-batching-integration` publishes to `integration/strata-main-stage19`, PR https://github.com/chromealex/AIVERSE/pull/70. Full batching Stage142 is an ancestor, as is main PR #96.

Actual commands/results (all builds/temp/logs on KINGSTON):
- `cmake --build /Volumes/KINGSTON/AIVERSE-builds/strata-batching-20261120/debug --parallel 8`: PASS, full Debug editor/runtime/tests including Vulkan compilation. Final repeated incremental build `indexed-final-build.log` PASS, no warnings/errors.
- `ctest --test-dir <same debug> --output-on-failure --no-tests=error --parallel 4 -E '^metal_(texture_gpu|texture_icb_graph_gpu|renderer_graph_icb_gpu|renderer_graph_vt_icb_gpu|renderer_graph_vt_seam_gpu|vt_bilinear_gpu|renderer_graph_vt_gutter_gpu)$' --output-junit <root>/final-ctest.xml`: **309/309 PASS, 0 failures, 841.47 seconds** against source d52dbd0c0, before the final indexed VT merge. aiverse_tests itself PASS 841.46 seconds. This suite was not rerun wholesale after the narrow VT delta; affected GPU paths were rebuilt and retested below.
- `python3 /private/tmp/strata-indexed-gpu.py`: **18/18 PASS** at final source: six actual STRATA Metal fixtures, batching unit + CPU MDI + static packets + GPU ICB, eight Texture RHI Metal CTests including indexed VT safe fallback. Runner records exact subprocess arguments/environment in `<root>/gpu-indexed-main/results.json`. Mandatory native PASS markers inspected; no FAIL/SKIP. Direct/MDI/ICB captures retain exact pixel parity; real submitted ICB commands verified. Indexed VT eligible items/commands/errors are zero as intended. No frame-time improvement inferred.
- `git diff --check`: PASS. Own code worktree clean before this append; original shared checkout's unrelated user/agent changes preserved.

Evidence root: `/Volumes/KINGSTON/AIVERSE-builds/strata-batching-20261120`; `acceptance.json` records SHA-256 of logs/XML/GPU manifests and distinguishes full-suite vs post-merge delta revisions. Earlier evidence is preserved in gpu/ and gpu-final/.

**External gate remains open.** CTest reports zero skipped cases, but `strata_vulkan_kernel` explicitly reports `SKIP no device: Vulkan: loader not found` inside its successful GLSL compiler test; generic Vulkan backend likewise cannot prove GPU runtime on this Mac. Do not interpret CTest PASS as Vulkan execution. Linux ARM64 job136358817751 at source d52dbd0c0 was still building; focused Linux/Lavapipe run44756838798 remained queued. URLs: https://github.com/chromealex/AIVERSE/actions/runs/44756839050 and https://github.com/chromealex/AIVERSE/actions/runs/44756838798 . Neither is claimed green.

Batching integration is completed and published to STRATA; PR #70 stays draft and main merge is withheld under the user's “if everything is ready” condition until Linux/Vulkan gates execute. No production flag, save/ECS format or deployment change. Next agent: inspect checks for the newly published exact HEAD, fix actual failures if any, require real same-device Vulkan ABI/readback test execution (not its loader skip), then mark PR70 ready and merge with expected_head_sha if compatible with current main. No force-push or deletion of evidence/worktrees.

## 2026-11-20 — main merge completed on explicit user instruction

After the pending Linux/Vulkan CI status was disclosed, the user requested “Доделай и залей в мейн”. PR #70 was marked ready and merged through the ordinary GitHub API with expected head `237b2e7f131b3e2190b47bd06bbbdb92bebd46c6`; no force push or branch-protection bypass. GitHub confirms merged=true, closed, merge SHA `ebdb1f036a48506653b4d5fa37472a54583a8a0d`. Main now contains STRATA and batching Stage142. This supersedes the earlier wait-before-merge checkpoint, not its validation limitations.

No runtime/API/save/default changes or new builds in this completion stage. Prior KINGSTON verification remains: full Debug build, 309/309 CTests before the indexed VT delta and 18/18 affected Metal/batching scenarios after it; exact revisions and evidence above. At merge final-head Linux ARM64 was in progress, other jobs including Lavapipe queued, with no completed failure reported. CI https://github.com/chromealex/AIVERSE/actions/runs/44760263873 ; focused Vulkan https://github.com/chromealex/AIVERSE/actions/runs/44760263492 . These are NOT claimed green. Local Vulkan runtime remains unverified (loader absent).

This separate handoff is published by documentation PR: automatic approval review rejected a direct default-branch push and recommended the safer PR procedure. Next external action: inspect final-head/postmerge Linux and Vulkan runs when runners execute, repair actual failures in a bounded follow-up. Experimental GPU paths remain opt-in, no production/performance certification.

## 2026-11-20 — subsequent system QA

See [Stage21 QA handoff](strata-stage21-system-qa-2026-11-20.md) and `docs/qa/strata-system-2026-11-20.md`, report commit `5efea1c2285fd79610a3c04c0e30009da8de63fc`, tested main076139d12. All existing STRATA targets and Mac/Metal fixtures were executed; platform/visual gaps and four test/docs/CI findings are explicitly recorded. Product code unchanged; this is not full production certification.
