# TerraWeave — current agent handoff entry point

> **Stage71 open-world editor terrain + flat single quad (2026-11-20):**
> New shared `SparseTerrainAuthoring` gives mouse-click auto-chunks at
> arbitrary signed world coordinates, cross-chunk paint from ONE
> Earthworks height source, nested camera LOD 33/17/9/5, seam stitching
> and **flat chunk = 4 indexed vertices / 2 triangles at any distance**.
> Runtime adaptive visual-only model swaps preserve fixed-resolution
> SpatialWorld collision/navmesh; Editor Scene previews streamed sparse
> chunks via Renderer3D + existing GPU-safe model retirement.
> The Terrain panel now exposes Create/Raise/Lower brushes and
> `Workspace::call("terraweave_sparse_paint")` for native/MCP/HTML5.
> Sparse saved data round-trips through both RTS and FPS map documents.
> Two NEW tests: `terraweave_sparse_authoring_lod`,
> `terraweave_sparse_editor`.
> [**Full standalone agent handoff: changed code, APIs, tests and next steps**](stage-71-sparse-editor-auto-chunks-camera-lod-flat-quad-handoff-2026-11-20.md).
> **NEW AppleClang compile, CTest, Editor GPU preview NOT RUN**;
> previous full 271/271 and Metal hardware checks are tied to older
> SHAs. Production runtime must opt into adaptive plan and sparse map
> data explicitly. Linux Vulkan/GLES and sustained four-owner test
> still open. PR #53 remains Draft.
>
>
> **Stage70 postmerge Stage116 Mac Release CTest 2/2 PASS (2026-11-20):**
> User confirms `model_resource_owner_contract` PASS 0.43 sec
> and `render_retirement_pressure` PASS 0.31 sec;
> **2/2 PASS, 0 failures, 0.75 sec wall** in
> `build/terraweave-retirement-release`.
> [Committed executed-test QA handoff](qa-2026-11-20-stage70-stage116-mac-release-green-handoff.md).
> The pasted log lacks explicit `git_sha`; source came from
> Stage116 two-parent main merge `910461a6d910dc0583762cb6dedfa4e9b97858b7`.
> Linux ARM64 GCC -Werror and real Vulkan/GLES3
> `terraweave_linux_forest_backends` are NOT VERIFIED.
> Linux GPU CI #44729549836 was queued at check time.
> Long authentic PHYSIS/NEREID/Terra/Box3D runtime and
> Studio forest screenshot review remain open; PR #53 stays Draft.
>
>
> **Stage70 latest main Stage116 Release test sync (2026-11-20):**
> Real two-parent merge `910461a6d910dc0583762cb6dedfa4e9b97858b7`
> incorporates main `f9aed9a705982e2c6b77b29869e309995ce8b163`
> while preserving the Terra Vulkan/GLES + Metal forest source.
> Three exact upstream paths: two Release-only render retirement/
> model owner tests and one Stage116 handoff. GitHub compare
> confirmed **main behind=0** at merge.
> [New standalone merge handoff](qa-2026-11-20-stage70-stage116-main-merge-handoff.md).
> **Postmerge Stage116 CMake/CTest NOT RUN.** Build
> `aiverse_retirement_pressure_tests` and
> `aiverse_model_resource_owner_tests` in Release;
> Linux Vulkan/GLES acceptance and long four-owner integration
> remain unverified. Existing Mac hardware forest PASS stays
> attributed to `0133eb21`; PR #53 remains Draft.
>
>
> **Stage70 Vulkan + GLES3 Linux forest acceptance authored (2026-11-20):**
> Real Metal forest was **1/1 PASS** on tested
> `0133eb21b211af62d205952db77c50589ae55a13`
> with six hardware PPM captures, 0 pipeline failures.
> New Linux backend source `48414952`, CMake tests `75e37067`,
> strict runner `bf01d955` and isolated Mesa/Xvfb CI
> `15a1fbee` now provide real Vulkan offscreen and EGL/GLES3
> `Renderer3D`+Arbor forest rendering, three readback PPMs each
> and non-skippable backend identity checks.
> [New standalone Linux handoff](qa-2026-11-20-stage70-linux-vulkan-gles-forest-handoff.md).
> **Linux configure, compiler, Vulkan/GLES run and CI NOT VERIFIED**;
> Stage70 v3 simulation and renderer architecture unchanged.
> Sustained authentic PHYSIS/NEREID/TerraWeave/Box3D remains open.
> Draft PR #53 is not ready for promotion.
>
>
> **Stage70 real Metal forest GPU acceptance PASS (2026-11-20):**
> Mac Studio tested **exact SHA**
> `0133eb21b211af62d205952db77c50589ae55a13`.
> `terraweave_metal_forest`: **1/1 PASS, 0 errors, 2.43s**,
> real BranchLOD/impostor/resized RT readback, different azimuth
> slices, explicit GPU-idle retirement. Six PPM frames captured;
> pixel differences near=6480, far=3891, holes=7729,
> resized=1502 and 0 pipeline failures.
> [Exact executed-result handoff](qa-2026-11-20-stage70-metal-forest-hardware-pass-handoff.md).
> **Hardware render checked; subjective screenshot quality,
> Studio viewport lifecycle, Vulkan/GLES and extended real
> four-owner simulation still unverified.** PR #53 stays Draft.
>
>
> **Stage70 real TerraWeave forest Metal GPU fixture committed (2026-11-20):**
> Source commits `510d1390`, `d7439549`, `f872f5f2`,
> `03cefb9f`, `47e9d4a1` add the hardware
> `terraweave_metal_forest` CTest plus a strict one-command
> Mac acceptance runner. The new test uploads production Arbor
> BranchLOD + 8-azimuth alpha-mask impostor, reads actual Metal
> GPU pixels from two camera directions and resized RTs, checks
> transparency/LOD policy, and releases models only after the
> explicit RHI GPU-idle barrier.
> [New dedicated code/QA handoff](qa-2026-11-20-stage70-real-metal-forest-render-handoff.md).
> **New AppleClang C++/Objective-C++ build, GPU test and PPM
> captures NOT RUN**. Previous 4/4 SpectraFX Metal PASS and 2/2
> Stage114 Release PASS remain valid only for their tested
> versions; do not claim forest GPU success before hardware QA.
> Vulkan/GLES and long real PHYSIS/NEREID/Terra/Box3D erosion
> remain separate gates. PR #53 stays Draft.
>
>
> **Stage70 postmerge Stage114 Mac Release tests 2/2 PASS (2026-11-20):**
> User's local Release build compiled
> `aiverse_resource_retirement_tests` and
> `aiverse_async_resource_retirement_tests`, then CTest
> `resource_retirement_queue` and
> `async_resource_retirement_queue` both PASS (0 failed,
> 0.88 sec). [Executed-test handoff](qa-2026-11-20-stage70-stage114-mac-release-retirement-green-handoff.md).
> The terminal excerpt did not contain a new git SHA; branch head
> at documentation time was `2a8fb8bd8ee405eda670adb6a847a9f2c12cabf2`
> and `main` was an ancestor (behind=0).
> Linux ARM64 GCC `-Werror` CI, forest GPU, Vulkan/GLES and
> sustained real four-owner ecology are still unverified.
> PR #53 remains Draft.
>
>
> **Stage70 strict GPU/runtime PASS + latest-main sync (2026-11-20):**
> Mac-tested exact `b1bb27c8f1e9f803ff49af29ea7dc0769b9cb264`:
> focused runtime PASS, **4/4 actual Metal GPU PASS** (0 SKIP),
> **5/5 real PPM visual checks PASS**, changed A/B/PRISMA
> 192/322/232 pixels. [Evidence](qa-2026-11-20-stage70-strict-metal-runtime-green-handoff.md).
> A subsequent true two-parent merge `25828ffce44f27d449f15f3cf992e2ee736fa2e8`
> integrated new main `ad175f624ef6f25e7c6dc19cabe199cb30623201`
> (Stage114 Release-retirement **test-only** fixes), preserving
> Terra source and exact upstream three-file blobs. GitHub
> compare confirmed **main behind=0**.
> [Main integration handoff and focused Release test commands](qa-2026-11-20-stage70-main-stage114-release-retirement-sync-handoff.md).
> **Postmerge Release/native CTest NOT RUN**. Previous 271/271 full
> and 4/4 Metal results are tied to their exact earlier SHAs.
> Forest GPU visual parity, Vulkan/GLES, sustained live four-owner
> erosion and completed CI remain open; PR #53 stays Draft.
>
>
> **Stage70 strict focused GPU/runtime acceptance authored (2026-11-20):**
> New QA-only stress fixture `bf06b609` runs 128 due iterations
> (32,768 virtual ticks) and checks v3 mass conservation, bounded
> scheduling and byte-identical midpoint replay. CMake registration
> `35c618f4` adds `terraweave_stage70_sustained`; focused Mac runner
> `99721c44` reuses the existing acceptance build and enforces
> real 4/4 MetalGPU hardware PASS plus seven PPM frame checks.
> [Dedicated implementation/QA handoff](qa-2026-11-20-stage70-focused-gpu-runtime-acceptance-handoff.md).
> **SOURCE COMMITTED; new C++ compile, targeted CTest and Metal
> visual execution NOT RUN.** Previous 271/271 PASS is tied only to
> `b17e775a`. Vulkan/GLES and long live 4-owner simulation
> remain open. PR #53 stays Draft.
>
>
> **Stage70 exact Mac native provenance CONFIRMED (2026-11-20):**
> User provided `acceptance-reports/provenance.txt` with tested
> `git_sha=b17e775a922d9dc50981011c8f6e25d85fc7adec`,
> required/observed `main=d6c8fa4646cf6aa205eea5f6a0b7c0c34ac479cb`.
> The same isolated checkout reported **271/271 PASS, zero failures,
> 882.51s**. GitHub comparison from that exact tested SHA to
> `ddb7f2ef` confirmed **only two documentation files changed**
> afterward. No native retest needed for documentation-only commits.
> [Standalone verification and outstanding gates](qa-2026-11-20-stage70-exact-provenance-271-confirmed-handoff.md).
> **Native CTest accepted on the exact SHA**; Metal visual parity,
> Vulkan/GLES runtime, sustained ecology/world simulations and
> completed CI are still NOT VERIFIED. PR #53 remains Draft.
>
>
> **Stage70 full Mac native/editor/Box3D CTest 271/271 PASS (2026-11-20):**
> User supplied real unfiltered acceptance output: **271/271 tests
> passed, 0 failed, 882.51s wall**. `aiverse_tests` (856.37s),
> `place` (26.12s), PHYSIS, NEREID, MetalGPU, SpectraFX and editor
> groups all passed. This confirms the earlier shared Metal and
> SpectraFX viewport compiler blockers were passed by this local build.
> [New standalone 271-test QA handoff](qa-2026-11-20-stage70-mac-271-full-ctest-pass-handoff.md).
> **Exact tested SHA must still be read from the Mac acceptance
> `provenance.txt`**; current remote HEAD at report time was
> `b17e775a922d9dc50981011c8f6e25d85fc7adec`.
> GPU pixel parity, Vulkan/GLES, sustained four-owner ecology,
> and exact-SHA CI remain separate gates. PR #53 remains Draft.
>
>
> **Stage70 SpectraFX preview/editor compile fix (2026-11-20):**
> User's full Mac build reached 95% but FAILED on two outdated
> `Viewport::ensure` call sites in SpectraFX GPU Assets and Graph windows.
> Source commits `0eabbfec` and `95ed023d` bind the Editor GPU lease
> and `ViewportGpuRetirement` owner for both previews without modifying
> shared `Viewport` lifetime semantics.
> [Source, exact failure, QA and retry](qa-2026-11-20-stage70-spectrafx-viewport-retirement-fix-handoff.md).
> **Post-fix native compile and CTest NOT RUN**. Draft PR #53 stays blocked.
>
>
> **Stage70 Mac Metal compile blocker repaired in source (2026-11-20):**
> Actual AppleClang full-build **FAIL** at ~29–30% on
> `aiverse_spectrafx_metal_rhi_test`: `MetalDevice` methods were
> misparsed because `completedFrameSerial()` lacked its closing brace.
> One-line shared Metal fix `0aa3321f37d1af16be442ff96c274205c9ad4e93`
> is committed on the Terra integration branch; same original defect
> remains on `main` pending upstream owner review.
> [Failure, root cause, exact diff and targeted Mac retry](qa-2026-11-20-stage70-metal-device-missing-brace-handoff.md).
> **Post-fix compile and CTest NOT RUN**. Draft PR #53 remains blocked.
>
>
> **Stage70 native acceptance gate hardened (2026-11-20):**
> Source `62d0566853d5a37a233fcf18d3325247ecc9d1fc` now refuses
> dirty/stale/unexpected-HEAD checkouts and fails if any of the four
> mandatory native CTests is absent before the one full unfiltered CTest.
> [Exact implementation, status and Mac command](qa-2026-11-20-stage70-acceptance-fail-closed-handoff.md).
> **NOT RUN on this new source**. The 191/191 historical Mac PASS remains
> tied to `346dc66a`. Do not mark Draft PR #53 ready without fresh
> full-suite and GPU/live-owner acceptance.
>
>

> **Stage70 latest main sync, GCC ARM64 fix (2026-11-20):**
> Real two-parent merge `3db80ef62fe` incorporates `main`
> `d6c8fa4646cf` over Terra `3d657d976670`, changing only
> `tests/animation/model_dependencies.cpp` and the upstream
> Stage112 GCC compatibility handoff. GitHub compare confirmed
> **ahead=144, behind=0** at merge time.
> [New merge handoff](qa-2026-11-20-stage70-gcc-arm64-latest-main-sync-handoff.md).
> **No CMake/CTest on this new source yet**; historical 191/191 PASS
> remains tied to `346dc66a`. PR #53 stays Draft.



> **Stage70 new-main Prisma/MotionWeave two-parent integration (2026-11-20):**
> Merge `c27085af` has real parents main
> `fc42156b` and Terra `e8656add`. Full-tree three-way audit:
> **4 overlapping paths resolved** (CMake, Renderer3DModels,
> ECS replay checks, monolithic FBX tests), all new Prisma,
> MotionWeave and universal View targets preserved.
> `main` is ancestor, behind=0 at merge.
> [Standalone merge/QA handoff](qa-2026-11-20-stage70-prisma-motionweave-main-merge-handoff.md).
> **NEW MERGE CMAKE / C++ / CTEST NOT RUN.** Historical 191/191 PASS
> belongs to exact premerge tested source `346dc66a`.
> Test first native PHYSIS/Terra fall bridge, then all-target CTest.
> GPU backend parity and prolonged ecology simulation remain open.
>


> **Stage70 Mac 191/191 full native acceptance (2026-11-20):**
> User reported a fresh **one-pass unfiltered 191/191 PASS**, 0 fail,
> wall 945.76s for merged SpectraFX + TerraWeave / native/editor/Box3D
> checkout. This resolves the prior ENOSPC-contaminated 188/191 run
> on the tested build, with no test filter or weakened assertions.
> PHYSIS, NEREID, TerraWeave, SpectraFX and 4 MetalGPU CTests passed.
> [Executed full-suite QA handoff](qa-2026-11-20-stage70-mac-191-unfiltered-ctest-pass-handoff.md).
> **Verified exact Mac tested SHA:**
> `346dc66aeb0a89e1dc69ad889134fe7079ecb169` from user-supplied
> `acceptance-reports/provenance.txt` (detached HEAD worktree).
> Remote comparison confirmed the four then-later branch commits
> modified **docs only**, no engine/CMake source after the PASS.
> Newer GitHub `main` `d6942dde` is NOT included in that
> accepted SpectraFX checkpoint; initial comparison found 2177
> newer main commits. Latest-main integration and GPU Vulkan/GLES
> still require separate review/testing. Do NOT merge PR #53 yet.
>


> **Focused native live-nutrient+fall CTest PASS — 2026-11-20:**
> Mac Studio user reports `aiverse_physis_terraweave_fall_bridge_test`
> built successfully; `physis_terraweave_fall_bridge` **1/1 PASS,
> 0.84s**, covering PHYSIS weather, NEREID 4m body IDs, nutrient
> conservation/archive/COW replay and the existing real Box3D fall
> bridge. [Executed-test handoff](qa-2026-11-20-stage70-live-native-micro-fall-ctest-green-handoff.md).
> **Full CTest of SpectraFX-current-main merge has NOT RUN yet**.
> Historical Mac 181/181 PASS is older `f55d8d4e`.
>

> **SpectraFX main two-parent merge + Stage70 live weather/water QA (2026-11-20):**
> Two-parent merge `40ed8606` incorporates new main
> `b80601eb` (SpectraFX and related concurrent changes) while
> preserving all Stage70 files. Only CMake overlapped; its two
> disjoint Terra inserts were composed with all main SpectraFX
> registrations. GitHub compare confirmed `behind=0`.
> Live PHYSIS+NEREID 4m nutrient COW/rollback test source
> `5ccba305` is now integrated into the merged tree.
> [Full handoff](qa-2026-11-20-stage70-spectrafx-main-merge-live-nutrients-handoff.md).
> **IMPORTANT: NEW MERGE AND TEST HAVE NOT BEEN COMPILED/TESTED.**
> Previous 181/181 Mac PASS was on premerge `f55d8d4e`.
> Run focused `physis_terraweave_fall_bridge` then new full CTest.
>



> **Stage70 live NEREID/PHYSIS nutrient test — 2026-11-20:**
> C++ source `2a7c5b66` and lifetime/soil comparison correction
> `5ccba305` extend `physis_terraweave_fall_bridge` with
> real one-World PHYSIS weather, two overlapping NEREID bodies,
> 4m conserved nutrients, v3 Archive and ordinary/COW rollback.
> [Handoff](qa-2026-11-20-stage70-live-physis-nereid-nutrients-rollback-handoff.md).
> **Code committed; test has NOT been compiled or executed**.
> The earlier user-verified 181/181 Mac PASS was on `f55d8d4e`,
> before this test was added. Do not claim post-edit PASS.
>

> **Latest main-sync and native QA checkpoint (2026-11-20):**
> Current `main` `e4129c5062a6` is already an ancestor
> of TerraWeave; GitHub comparison showed `behind=0` — no merge.
> User's local Mac full CTest reported **181/181 PASS in 886.46s**.
> Dedicated PHYSIS, TerraWeave and Box3D tests are included.
> Provenance now confirms tested SHA
> `f55d8d4e8622d297c4c27266f9289f82cc59baed`;
> the later integration commit `36fd38db` changed docs only.
> Full details: [main ancestry / Mac 181 handoff](qa-2026-11-20-main-ancestor-mac181-acceptance-handoff.md).
> GPU backend parity and live cross-owner sim are **NOT VERIFIED**.
>


> **Native Mac QA: 180/181 PASS, FBX=OFF fix staged (2026-11-20).**
> User provided a full CTest summary: 180/181 passed, all dedicated
> TerraWeave, PHYSIS and Place tests passed; only monolithic
> `aiverse_tests` aborted loading `cube.fbx` with `AIVERSE_FBX=OFF`.
> Code commit `92758e07` aligns the test runner's compile flag with
> the disabled runtime FBX loader, preserving full FBX-on tests and
> asserting an actionable disabled-loader error in FBX-off builds.
> See [standalone full native QA handoff](qa-2026-11-20-fbx-off-aiverse-tests-180-of-181-handoff.md).
> **After-fix compile/CTest NOT RUN.** PR #53 stays Draft.
>


> **Stage70 native NEREID search bound — 2026-11-20:**
> `4cd0ca4d` guards the public nearest-4m-cell helper against
> excessive radius/invalid body ID/missing sampler. `1316a51c`
> adds headless native regression cases. Read the
> [bounded NEREID search handoff](qa-2026-11-20-stage70-bounded-nereid-search-handoff.md).
> **Source committed; Clang/GCC, CMake, CTest, PHYSIS/NEREID/Box3D live
> integration and GPU QA NOT RUN.** Draft PR #53 remains open.
>


> **Stage70 65k cap regression (2026-11-20):**
> `0f3ec2da` adds a pure CTest fixture covering the full
> 65,536 body-tagged 4m cell limit, 1,024-cell bounded scheduler
> slices and rejection of a 65,537th cell before mutation.
> [Standalone handoff](qa-2026-11-20-stage70-sparse-cap-65536-handoff.md).
> **Source committed; native compiler/CTest and performance NOT RUN.**
>


> **Stage70 native NEREID coordinate hardening (2026-11-20):**
> overflow guard source `730271f7`, authored mouth regression
> `83c45279`, direct empty-ECS signed-boundary native smoke
> `09ff998b`. Read
> [coordinate guard handoff](qa-2026-11-20-stage70-nereid-coordinate-guard-handoff.md)
> and
> [native sampler test handoff](qa-2026-11-20-stage70-native-sampler-boundary-test-handoff.md).
> **Source committed, C++ compiler and CTest NOT RUN.**
> PR #53 stays Draft until CI and live cross-owner acceptance.
>

> **QA/PR gate update:** [Stage70 PHYSIS CI trigger and Draft PR #53](qa-2026-11-20-stage70-physis-ci-trigger-handoff.md).
> [CI run #44624069071](https://github.com/chromealex/AIVERSE/actions/runs/44624069071)
> remained **QUEUED** at the last check. The PR is draft, and
> compilation, full CTest, renderer and multi-owner world acceptance
> are **NOT VERIFIED**.
>

> **New PHYSIS-current-main integration staging (2026-11-20):**
> [Stage70 + PHYSIS main merge handoff](qa-2026-11-20-stage70-physis-main-merge-handoff.md).
> Two-parent merge `97b4e165cae1ab74b907ba2b934665a530715432`
> on `integration/terraweave-physis-stage70-20261120`, with
> `e4129c50` main and `cbc7f0f6` prior TerraWeave as parents.
> 14 divergent paths reviewed; all main paths preserved and the
> Stage70 native CMake tests brought forward. **SOURCE ONLY:
> native CMake/CTest and PHYSIS/NEREID/Box3D integration NOT RUN.**
> Do not merge to main before real acceptance.
>

> **Integration QA checkpoint (2026-11-20):** On the staging-only
> `integration/terraweave-main-20261120` branch, the sparse Stage70
> soil/water scheduler resume-key fix is committed in `34b557ad`,
> with negative-coordinate, v3 migration and compaction tests in
> `d6d03d74`. See
> [Stage70 sparse cursor QA handoff](qa-2026-11-20-stage70-sparse-resume-cursors-handoff.md).
> **No completed CI/native build on this integration head yet.**
> Previous pre-merge 136/136 CTest does not validate this change.
> Do not merge to main before full native/runtime acceptance.
>

## Latest main-integration staging branch — QA not yet green

**Read first:** [2026-11-20 TerraWeave + main two-parent merge and full native QA](qa-2026-11-20-main-integration-handoff.md).
Branch `integration/terraweave-main-20261120` contains a genuine
merge of TerraWeave `d9de76dcd8aa4e7b4e01a42e183a0b9c98c4854b`
and main `d7d9b3f4180dcba98b9238fd56eedc7ccb420bb1`
in `ad982326f6b52e728a689f0c53c43d50f5ad9e02`,
resolving 11 overlapping source paths while retaining both histories.
GitHub Actions [new merged native check](https://github.com/chromealex/AIVERSE/actions/runs/44619590001)
was queued at creation; no CMake/full CTest result on this
merged source is yet confirmed.
Prior Mac `136/136` is for pre-merge TerraWeave only.
The original `main` and `feature/terraweave` were not modified.
Do not merge PR #24 before fresh native/editor/GPU and combined
NEREID/PHYSIS/Box3D QA.


Last source stage: **70** (2026-11-19), branch `feature/terraweave`.
PR #24 is Draft, and must not merge to `main` before native QA and
cross-branch conflict reconciliation.

## Latest acceptance evidence: FULL native CTest 136/136 PASSED

**Read first:** [Mac Studio full CTest 136/136 PASS handoff](qa-2026-11-20-mac-136-ctest-pass.md).
The user reported the final unfiltered native CTest summary
`100% tests passed, 0 tests failed out of 136`
(SpectraFX 3 tests, 1.47 sec*proc).
This succeeds after the prior Place hot-path fix, Box3D native
header smoke, and Stage70 nutrient regressions.
The exact local tested Git SHA and effective CMake cache flags
are **not in the supplied final excerpt**; obtain them before
attributing acceptance to a commit or particular build preset.
Earlier independent Place optimized Debug measurement was
0.766ms average / 1.729ms worst against the unchanged <5ms
budget, and replica suite PASS 1/1.
**Next gate is resolving current main/feature divergence in an
isolated integration worktree and real cross-owner NEREID/PHYSIS/
Box3D/terrain GPU acceptance.** This full CPU CTest does not
establish those separately. PR #24 remains Draft; no merge done.

## Latest native performance evidence — Place and replica passed

**Read first:** [Mac Studio user-verified Place 0.766ms and replica 1/1 PASS](qa-2026-11-19-place-perf-native-green.md).
In the optimized Debug QA build, the original 16-client 4G/523-body
strict `tickAvg < 5.0` scenario now **PASSES** at 0.766ms average,
1.729ms worst (11 checks, 0 failed; 12.7s real wall time).
The independent `aiverse_replica_tests` CTest executable also
**PASSES 1/1** (2.36s real). Source case field/bandwidth/
convergence/late join match the earlier workload with
31,954 B/s/client and 0.0069m resting/late discrepancy.
This is real user-provided evidence, NOT a full unfiltered CTest
or proof of ordinary -O0 Debug perf. Exact tested local SHA
was not included in the pasted output; verify with `git rev-parse HEAD`.
Next: full all-target build and one unfiltered CTest with
`AIVERSE_PLACE_FAST_DEBUG=ON`, then actual integration/renderer QA.

## Latest PHYSIS-proven Place performance QA backport

**Read first:** [2026-11-19 PHYSIS Stage100/101 Place performance backport and replica parity](qa-2026-11-19-physis-place-performance-backport.md).
The user's complete native CTest found a `place` server timing regression
(**8.256ms mean, 5ms limit**) despite successful gameplay convergence.
PHYSIS Stage101 already reported **0.789ms average, 1.905ms worst**
for the SAME scenario on optimized Debug, with batch replica field
updates, allocation-free viewer, and opt-in `-O2` hot modules.
Those source changes are now selectively backported into this
TerraWeave branch, along with five-phase diagnostics, a replica-wire
parity regression and a dedicated Linux/macOS Place workflow.
**This backport is source-only and has not yet passed new native
compiler/CTest on this branch**. PHYSIS's previous PASS does not prove
the backport is passing. Do not change the 5ms limit or merge main.

## Latest Mac Studio evidence: three native TerraWeave checks passed

**Read first:** [Box3D native 3/3 passing smoke and warning QA fix](qa-2026-11-19-box3d-smoke-green-and-warning.md).
The user-supplied macOS CTest excerpt confirms three actual passes:
`terraweave_native_micro_compile`,
`terraweave_native_micro_physics_compile`, and
`terraweave_model` (100%, 3/3, 1.49 sec). The only Clang warning
was logical-op precedence in `TerraWeaveNereidObstacleBridge.h`;
its parentheses and corresponding physics-smoke edge cases are now
committed. Those latest source commits have **not yet been rebuilt**
by the user. Full build/full CTest and live Box3D/NEREID/PHYSIS
integration remain pending; PR #24 stays Draft.

## Latest QA blocker fixed: optional Box3D native smoke

**Read first:** [Box3D native CMake/header fix and Mac Studio commands](qa-2026-11-19-box3d-native-smoke-fix.md).
A real AppleClang build from `AIVERSE-terraweave-qa-fix` reported
`fatal error: 'box3d/box3d.h' file not found` from the
unconditionally included physics adapter in
`terraweave_native_micro_compile_test.cpp`.
The basic smoke is now physics-off-safe; a SEPARATE smoke target links
`AIVERSE::physics` when Box3D is enabled. Full Linux/macOS QA enables
physics, while isolated GCC/Clang checks cover physics-off.
A signedness warning in `terraweave_model_test.cpp` was also corrected.
These are committed source fixes, NOT yet independently compiled or
CTest-verified after the user's failure.

## Current integration/QA stage — source features frozen

**Latest Mac CTest triage:** [2026-11-19 Mac Studio 131-test diagnostics](qa-2026-11-19-mac-ctest-triage.md): 94 passed, 33 Not Run from missing binaries, three importer tests missing `aiverse_headless`, and `skill_audit` three broken paths fixed in GitHub. GCC/Clang and full native CI were re-triggered; source fixes require a real rerun before being marked green.

**Original QA kickoff:** [2026-11-19 full native QA handoff](qa-2026-11-19-full-integration-handoff.md).
The existing TerraWeave GCC/Clang workflow had an unterminated CTest
regex; this has been fixed. It now builds the Stage70 pure nutrient
test and a native NEREID/ECS/optional-PHYSIS header compile smoke.
A second Linux+macOS workflow configures/builds all enabled native
targets and runs all registered CTests in one batch. At creation,
both GitHub Actions runs were QUEUED, so **no new tests are yet
confirmed passing**. Do not begin Stage71 before real QA results,
source fixes, and cross-branch integration.

## Where to start

1. [Stage 70: sparse body-tagged 4m nutrient ledger and v3 save migration](stage-70-sparse-micro-nutrients-handoff.md)
   — latest source, exact commits, new CMake test target, native APIs,
   explicit lossless migration, actual QA status and next acceptance.
2. [Stage 69: linked NEREID confluences/cascades, 4m creek sampling, PHYSIS nature](stage-69-nereid-physis-nutrient-handoff.md)
   — previous coarse-water Stage69 topology and weather implementation.
3. [Stage 68: nutrient cycle, PHYSIS weather and NEREID flow](stage-68-nutrient-circulation-handoff.md)
   — conserved v2 nutrient ledger, rain leaching, uptake and same-body transport.
4. [Stage 67: deadwood/soil nutrition](stage-67-deadwood-soil-nutrients-handoff.md)
   — prior organic ledger, fallen Box3D retirement, forest/grass fertility.
5. [Stage 66: authoritative harvest](stage-66-authoritative-harvest-handoff.md)
   — persisted tree removals/replanting and ECS native authority.
6. [Stage 65: rooted forest ecology](stage-65-rooted-forest-ecology-handoff.md)
   — growth, mortality, sun competition and native seed recruitment.
7. [Full historical handoff](AGENT_HANDOFF.md) and
   [TerraWeave architecture](TERRAWEAVE.md).

## Immutable design boundaries

- NEREID alone owns water flow, flood, waves and sediment; TerraWeave can
  follow water currents for *nutrient tracer* movement, but cannot create
  another hydrological authority.
- PHYSIS alone owns weather simulation; Stage68 accepts deterministic
  callbacks for temperature, rain and soil moisture from a completed tick.
- TerraWeave owns Earthworks height and persistent root/grass/ecology/
  harvesting/deadwood nutrient data. Rendering/Box3D/NavMesh operate at
  their respective post-ECS owner barriers.
- Native game modules configure optional features; never hardcode RTS/FPS
  into generic TerraWeave.
- Stage70 v3 source, native mode and the `terraweave_micro_nutrient`
  test **source** are committed to GitHub. C++ compilation, execution,
  native v1/v2→v3 save migration and NEREID/PHYSIS regression verification
  have NOT been observed; previous green tests apply only to older commits.
- Historical v1/v2 32m dissolved water stays a fully conserved UNASSIGNED
  legacy pool. Body-tagged 4m migration is explicit and validates every
  NEREID wet tile; never automatically give an old coarse amount to one
  of two possible rivers.
- PHYSIS's real nature API was inspected on a separate PHYSIS branch;
  the TerraWeave-only feature checkout does NOT yet contain that header.
  The Stage69 native adapter is optional, compiled conditionally, and
  explicitly refuses native opt-in when PHYSIS is unavailable.
- Do NOT merge divergent `main`/TerraWeave renderer paths blindly.
- Each new stage MUST append a standalone handoff with actual commits,
  precise APIs, tested commands and truthful statuses before work is
  passed to the next agent.

## First next action

Use an isolated worktree on `feature/terraweave`; first compile
`aiverse_terraweave_micro_nutrient_test` and execute CTest
`terraweave_micro_nutrient`, then add native ECS NEREID two-stream
tests with a real confluence inside one former 32m tile, exact
v1/v2→v3 archive migrations, rollback, input-order determinism,
legacy claim failure/atomicity, strict mass conservation, 65k
memory/performance bounds and coarse-mode opt-out regression.
Do not declare Stage70 passing without exact logs.

Only after the separate QA agent fixes failures should the engine's
larger previously deferred TerraWeave/PHYSIS/NEREID tests be
run as a coordinated batch and `main` merge conflicts reconciled.

The next feature agent may consider Stage71 per-body flow weighting
and optional 1m local nutrient tiles, but must not invent a dense
world grid or silently change the v3 archive. Each stage requires
its own accurate handoff and committed SHA.
