# PRISMA Stage 87 — Integrated MotionWeave full native validation CI

Date: 2026-11-20
Repository: chromealex/AIVERSE
Branch: integration/prisma-motionweave-view
Previous handoff: docs/handoffs/prisma-stage-86-animation-static-audit.md

## Work completed
Added .github/workflows/prisma-motionweave-integration-validation.yml
at commit 0c58688cacf6c114ad9ca1533f8e70ca3aa31066.

The workflow covers pushes with engine, libraries, tools, modules, tests,
apps, CMake or workflow changes on the integrated branch, and supports
workflow_dispatch. It checks out submodules and runs independently on
macos-15 and ubuntu-24.04. On each runner:
1. Configure complete editor + HDR fixture + native tests in Release.
2. Build ALL CMake targets with four jobs; set -o pipefail ensures
   compiler errors fail the job even through tee.
3. Run ONE full CTest after successful build, with output-on-failure.
4. Upload true configure/build/CTest output and CTest LastTest.log.

## Verification / caveats
- PASS: GitHub create_file, subsequent readback of workflow YAML.
- GitHub combined statuses were empty when checked for workflow commit.
  This does NOT prove any runner executed or that any test passed.
- Build, CTest and native Metal/GLES/Vulkan GPU validation: NOT RUN by
  assistant or confirmed by GitHub CI in this stage.
- The static code audit in Stage 86 touched only MotionWeave/Animation/
  engine View sources, not every one of the repo's 5908 tracked entries.
- A hosted macOS runner is not the user's Apple M3 Ultra and must not
  be treated as a substitute for local Metal GPU validation.

## Next steps
Inspect GitHub Actions tab for the new integration workflow, collect
actual per-platform diagnostics if it runs. Otherwise execute one
incremental full build and one consolidated CTest on the Mac worktree at
the latest origin/integration/prisma-motionweave-view SHA. Only fix real
compiler/runtime blockers and document each bounded stage. Leave main
and source feature branches untouched.
