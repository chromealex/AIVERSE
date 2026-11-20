# TerraWeave Stage70 — full Mac 191/191 native CTest PASS after SpectraFX/main merge

**Date:** 2026-11-20
**Branch:** `integration/terraweave-physis-stage70-20261120`
**Draft PR:** [#53](https://github.com/chromealex/AIVERSE/pull/53)
**Prior native source merge:** `40ed8606e834fb7c75a1c16901280300191980ec` (main `b80601eb3fdcafddedbe9a9387f78a2fea0e499f` + TerraWeave).
**Branch HEAD before docs-only recording:** `72fe4c22b51ce788b3fc7a5b11520484281bc772`.
**Verified exact Mac native tested SHA (user-supplied acceptance provenance):** `346dc66aeb0a89e1dc69ad889134fe7079ecb169`.

## Real Mac native acceptance reported by user

From `~/Projects/AIVERSE-terraweave-stage70-local-qa` and the
`build/terraweave-main-acceptance` acceptance script, the user
reported this completed, **single unfiltered CTest**:

```text
190/191 Test #34: aiverse_tests .............. Passed 918.19 sec
191/191 Test #110: place ..................... Passed 27.55 sec
100% tests passed, 0 tests failed out of 191
Total Test time (real) = 945.76 sec
PASS: merged native/editor/Box3D CTest
```

Selected CTest label summaries (sec*proc) reported by user:

| Label | Result |
| --- | ---: |
| Architecture | 0.74 |
| ECS | 1.67 |
| EDITOR | 5.60 |
| MetalGPU | 2.53 (4 tests) |
| NEREID | 0.31 |
| NETWORK | 0.70 |
| PERSISTENCE | 1.62 |
| PHYSICS | 0.85 |
| PHYSIS | 12.49 (31 tests) |
| RHI | 1.91 (2 tests) |
| Renderer3D | 1.26 |
| SpectraFX | 7.19 (15 tests) |
| TERRAWEAVE | 0.50 |
| TOOLS | 1.15 |

Previously the same 191-test suite failed 3 tests while macOS
reported `No space left on device` (ENOSPC): the skills isolation
compiler could not write an object file; monolithic `aiverse_tests`
failed to create temporary directories and `aiverse_net_tests`
failed replay-related assertions under the disk exhaustion. The
later clean one-pass 191/191 CTest is direct evidence these **three
tests passed on the rerun**, without a code change attributed to ENOSPC.
Do not claim all possible disk/RHI/performance problems are solved.

The additional focused `physis_terraweave_fall_bridge` AppleClang
build/CTest was previously independently user-reported **1/1 PASS in
0.84 s**, including the newer live PHYSIS weather/NEREID body-keyed
4m nutrient conservation/COW rollback fixture. See companion
[executed focused test handoff](qa-2026-11-20-stage70-live-native-micro-fall-ctest-green-handoff.md).

## Critical provenance and moving main

- **Provenance now confirmed** from the user's actual
  `build/terraweave-main-acceptance/acceptance-reports/provenance.txt`:
  ```text
  repo=/Users/aleksandrfeer/Projects/AIVERSE-terraweave-stage70-local-qa
  git_sha=346dc66aeb0a89e1dc69ad889134fe7079ecb169
  git_branch=
  ```
  The empty branch name is expected in a detached-HEAD test worktree.
  GitHub compared the proven tested SHA to the integration branch
  before this handoff: four later commits touched **only**
  `docs/terrain/CURRENT_HANDOFF.md` and two standalone
  `docs/terrain/qa-...` handoff files. Hence no post-test
  C++/CMake changes had occurred on that branch. The actual
  `cmake-cache-flags.txt` and `ctest.xml` have not been supplied;
  preserve them as additional audit evidence.
- When this QA result was recorded, GitHub `main` had already
  advanced to `d6942dde307814696f5060a51088cff5c013e5fe`,
  while TerraWeave was still at
  `72fe4c22b51ce788b3fc7a5b11520484281bc772`. GitHub
  compare showed **ahead=137, behind=2177**, base ancestor
  `b80601eb3fdcafddedbe9a9387f78a2fea0e499f`. Therefore,
  the 191/191 result must NOT be presented as acceptance of the
  **newer main `d6942dde`**. Need another deliberate main-sync
  conflict review and retest if integration into latest main is
  required.
- The prior CMake merge preserved both SpectraFX and TerraWeave
  targets; the 191 test inventory includes those registrations.
- No production/test C++ code changed in this *documentation stage*.

## Next agent / required gates

1. Preserve native evidence: exact worktree SHA and CMakeCache,
   JUnit XML, `df -h` where useful; do not delete the QA records.
2. On an isolated integration branch/worktree, review the latest
   `main` vs TerraWeave branch, including changes in newer
   SpectraFX/Prisma/MotionWeave/PHYSIS/ECS/RHI. Do NOT assume zero
   conflicts from the older `b80601eb` comparison.
3. Merge only after full 3-way source conflict analysis and retain
   both systems, test registrations, performance constraints and
   authored handoffs. Never overwrite `main` or other agents'
   branches. After new source merge, compile focused cross-owner
   tests, then one complete unfiltered CTest.
4. Explicitly validate hardware Metal, Vulkan and GLES as supported
   by their platform environments. A successful `MetalGPU` label
   in CTest is valuable but does not establish screenshot parity
   of TerraWeave's renderer or Vulkan/GLES runtime correctness.
5. Run longer multi-tick integrated Earthworks erosion, PHYSIS
   climate/wind, NEREID water, TerraWeave vegetation/deadwood/
   4m nutrients, Box3D falling-tree host reconciliation and
   rollback/replay with mass and stability checks.
6. Keep [Draft PR #53](https://github.com/chromealex/AIVERSE/pull/53)
   unmerged until new-main acceptance is completed and approved.

**Status: Mac native merged SpectraFX/Stage70 checkpoint 191/191 PASS.
Newer current-main branch coverage and GPU parity remain UNVERIFIED.**
