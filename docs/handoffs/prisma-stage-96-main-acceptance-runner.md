# PRISMA Stage 96 — post-main native and GPU acceptance runner

Date: 2026-11-20
Repository: chromealex/AIVERSE
Source baseline: main at f9aed9a705982e2c6b77b29869e309995ce8b163
Working branch: feature/prisma-main-acceptance-stage96-20261120
Previous PRISMA handoff: docs/handoffs/prisma-stage-95-view-null-entity-namespace.md
Related integration handoff: docs/handoffs/aiverse-stage-110-prisma-motionweave-main-merged.md
Related current CI fix: docs/handoffs/aiverse-stage-116-release-asserts-pressure-model-owner.md

## Confirmed baseline, not to redo

- PR #67 was merged, with PRISMA, MotionWeave and universal View on main.
- GitHub ancestry comparison: feature/prisma-postfx is an ancestor of main
  (as checked 2026-11-20). Do not attempt to merge the old feature again.
- Earlier user-run Apple M3 Ultra evidence on older PRISMA revisions:
  115/115 native CTest, 54/54 GL33 HDR probes, 54/54 Metal HDR probes,
  a Metal TAA stationary centroid test. Stage 74 additionally confirmed
  Metal terrain high-detail shader compilation, while GL33 needed a
  documented low-detail terrain fallback. These are NOT results for
  today's combined main HEAD.
- On main, broad CI run 44729504603 for f9aed9a7 is QUEUED at inspection.
  Old one-shot full postmerge run 44709805590 (fc42156b) has both
  macOS and Linux jobs QUEUED; it targets an obsolete SHA and cannot
  certify current main. Prior main CI 44725283646 was CANCELLED after
  Linux ARM64 GCC Release build failure; Stage 116 fixed its specific
  NDEBUG-elided assert diagnostics. No current green main full CTest
  has been independently observed.

## Code change

tools/prisma/acceptance.sh is a NEW Bash helper, separate from the
existing AIVERSE CI and from tools/spectrafx/acceptance.sh. It never
modifies git branches, deletes a worktree, or merges main.

1. Record exact HEAD, branch, dirty-path listing and timestamped commit.
   Optional AIVERSE_PRISMA_EXPECT_SHA makes a stale checkout fail early.
2. Configure Release with tests, editor, HDR fixture enabled; perform
   ONE complete build. Refuse to execute stale CTest binaries if build
   fails. Count registered tests, require >=100, then run ONE complete
   unfiltered CTest; preserve exit code, skipped-case evidence and logs.
3. On macOS by default execute three independent GPU contracts:
   native Metal HDR 54+ GPU probes, real Metal TAA centroid PASS,
   native GL33 HDR 54+ probes. Assert actual device/backend marker and
   shader fixture PASS rather than inferring the backend from macOS.
4. Importantly, AIVERSE_HDR_VERIFY produces probe results but does NOT
   write a scene image. Therefore run a SEPARATE ordinary scene capture
   per backend, validate nonempty binary P6 PPM and preserve both images.
   Mixing verification and capture env flags would falsely flag an
   otherwise valid GPU run as missing PPM.
5. Save logs/summary as build-dir/prisma-acceptance/{summary.md,*.log,
   commit.txt,git-status.txt,ctest-summary.txt,metal-hdr.ppm,gl33-hdr.ppm}.
   Report failed and skipped gates. A REQUIRE_GPU=1 switch turns missing
   macOS GPU fixtures into an explicit failure.
6. Linux remains a CPU/native build + CTest runner. This script does NOT
   claim Vulkan, GLES, WebGL GPU runtime parity, dual-camera history,
   fenced retirement stress or image-quality parity.

## Invocation on the Mac Studio

Use a clean, separate worktree or checkout for this branch. Do not
switch away from someone else's dirty/unmerged worktree.

```bash
cd ~/Projects/AIVERSE
git fetch origin
git worktree add ../AIVERSE-prisma-stage96 origin/feature/prisma-main-acceptance-stage96-20261120
cd ../AIVERSE-prisma-stage96
bash tools/prisma/acceptance.sh build/prisma-main-acceptance
```

Optional:
- AIVERSE_PRISMA_JOBS=4: parallel compiler/CTest workers (default 4).
- AIVERSE_PRISMA_EXPECT_SHA=<full current checkout SHA>: reject wrong source.
- AIVERSE_PRISMA_RUN_GPU=0: explicit native-only run (GPU SKIP reported).
- AIVERSE_PRISMA_REQUIRE_GPU=1: GPU omission is a failure.

## Verification actually performed in this stage

- GitHub readback of source baseline, existing HDR scene/GL fixture,
  Stage 71/72/74 GPU evidence, current main and current CI.
- Inspected HdrScene.h, hdr_scene.cpp and hdr_gl.mm: Metal/GL are distinct
  binaries, HDR_VERIFY uses drawProbe, TAA_VERIFY uses drawTemporal, and
  only normal draw writes P6 scene capture. The harness mirrors that
  actual code behavior rather than relying on documentation guesswork.
- Script committed on a dedicated branch and read back via GitHub.
- NOT RUN: local shell syntax check, actual CMake/configuration, full
  native C++ build, CTest, Metal/GL capture or runtime. This environment
  does not mount the user's native Mac Studio project. Source inspection
  does not amount to GPU acceptance.

## Next handoff

1. Check out the exact Stage 96 branch, execute bash -n
   tools/prisma/acceptance.sh and the ONE native acceptance run on the
   actual Mac, preserving the complete summary and first failure log.
2. Fix only errors confirmed on that current SHA, not older cancelled
   main or pre-merge feature builds; add an independent handoff per fix.
3. After current integrated main is green, add and execute real Metal
   multi-camera cut/resize/temporal history/fence stress. Validate Vulkan
   and GLES independently on suitable devices/runners.
4. Do NOT merge this test-runner branch to main or label it GPU-PASS until
   native validation and explicit user approval.

## Final change log for Stage 96

- tools/prisma/acceptance.sh: new Release/CTest and real backend fixture
  acceptance script; final source commit 4fa4beda488672f562c34e8efd21b77962501452.
- skills/visuals/aiverse-prisma/SKILL.md: updated old Stage 66 default
  handoff and static-only native guidance to Stage 96, actual post-main
  status, correct probe-vs-PPM separation, new handoff naming requirement.
  Skill update commit b3906b9a592c4f25d3e243b3a6bedd08c4917dd9.
- This document records current branch state. No production engine or
  simulation sources were edited and main was not mutated.

### Static review before local execution
Fetched and inspected the final script content from GitHub (166 lines),
confirmed expected configure/build/CTest stages, pinned-SHA guard, explicit
backend assertions and fail/skip reporting. A native `bash -n` command
was attempted through a container download, but GitHub raw content
could not be retrieved because DNS resolution failed. Therefore shell
syntax is **NOT VERIFIED**, and it remains the first real acceptance gate.
