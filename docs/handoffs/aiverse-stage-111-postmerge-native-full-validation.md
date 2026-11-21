# AIVERSE Stage 111 — Trigger one-pass post-merge native build and full CTest

Date: 2026-11-20
Repository: chromealex/AIVERSE
Target branch: main
Starting main SHA: d6942dde307814696f5060a51088cff5c013e5fe
Previous handoff: docs/handoffs/aiverse-stage-110-prisma-motionweave-main-merged.md

## Goal
Validate already-merged PRISMA, MotionWeave, generic View, RHI and Editor
after PR #67, avoiding repeated expensive local CMake/CTest loops.

## Reality at start
- Existing main CI run 44708840785 on SHA d6942dde307814696f5060a51088cff5c013e5fe was queued, with
  Linux arm64 build in progress at query time; its standard workflow
  builds several platforms and runs the aggregate aiverse_tests but
  does not run all CTest cases.
- Remote source can be inspected through GitHub integration; no
  writable native macOS repository is available to this agent.
- Direct container git access to github.com failed due DNS/network;
  no local native build/CTest/GPU tests can be claimed.

## Change
Add `.github/workflows/aiverse-postmerge-full-validation.yml` as
a **one-shot** main-push-triggered workflow (push path filter is its own
YAML file, plus workflow_dispatch for intentional future reruns).
Jobs on macos-15 and ubuntu-24.04 configure Release with all tests,
Editor and HDR fixture enabled, build all targets ONCE and execute the
COMPLETE CTest ONCE. Require at least 201 registered CTest cases,
record GPU Metal/Vulkan test registration and explicit skips without
claiming skipped hardware tests passed, upload logs on failure.
Linux installs Vulkan/GLES/EGL and X11 development packages.
The workflow does not claim Vulkan/GLES runtime parity or actual GPU
device capability if those tests were skipped.

## Tests actually performed in this stage
- Reviewed full existing `.github/workflows/ci.yml`, PRISMA
  integration validation workflow, and merged CMake test entries.
- Verified CI run and its job states through GitHub API.
- Workflow file authored with correct existing CMake option and CTest
  naming. Native build/CTest: **NOT RUN YET**, GPU: **NOT RUN YET**.
- After the workflow commit reaches main, inspect the actual GitHub
  Actions run ID, status, logs and first failing case; do not report
  PASSED before completed.

## Follow-up
If any job fails, fix in a narrow branch, add a handoff, and re-run the
same workflow via workflow_dispatch on the repaired revision.
On real Mac verify Metal/SpectraFX non-skipped GPU tests, then Vulkan
and GLES runtime separately. Existing broad main CI covers other
platform builds.
