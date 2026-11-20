# TerraWeave Stage70 — current main ancestry + Mac 181/181 native acceptance

**Date:** 2026-11-20.
**Branch:** `integration/terraweave-physis-stage70-20261120`.
**Working PR:** [Draft #53](https://github.com/chromealex/AIVERSE/pull/53).
**Verified stage base HEAD (before this docs-only commit):** `f55d8d4e8622d297c4c27266f9289f82cc59baed`.
**Current main:** `e4129c5062a6ab47e7c70f3fdd483392fde1f679`.

## Branch-integrity check — NO MERGE REQUIRED

GitHub compare results directly against both refs:

```text
base = main; head = integration/terraweave-physis-stage70-20261120:
  status=ahead; ahead_by=126; behind_by=0
  merge_base=e4129c5062a6ab47e7c70f3fdd483392fde1f679

base = integration/terraweave-physis-stage70-20261120; head = main:
  status=behind; ahead_by=0; behind_by=126
  merge_base=e4129c5062a6ab47e7c70f3fdd483392fde1f679
```

**Conclusion:** Main's exact latest commit is already a Git ancestor of
the TerraWeave integration HEAD. No missing main commit, merge conflict
or source synchronization remains. A synthetic merge would not bring
new changes and was intentionally NOT created. Do not touch main or
feature branches. Prior genuine two-parent PHYSIS/Stage70 merge is
`97b4e165cae1ab74b907ba2b934665a530715432`.

## Real native Mac Studio acceptance reported by user

The isolated checkout was
`~/Projects/AIVERSE-terraweave-stage70-local-qa`.
The user's latest complete terminal summary says:

```text
179/181 ... meta_liveops_schema ...... Passed
180/181 Test #34: aiverse_tests ...... Passed 858.94 sec
181/181 Test #110: place ............ Passed 27.51 sec
100% tests passed, 0 tests failed out of 181
Total Test time (real) = 886.46 sec
PASS: merged native/editor/Box3D CTest
```

The acceptance script output points to
`build/terraweave-main-acceptance/acceptance-reports`.
The previously observed 180/181 Mac failure was
`aiverse_tests` aborting on an FBX fixture with
`AIVERSE_FBX=OFF`; source commit
`92758e07a8edbb6d14f47787b3729f6cff1390a1`
corrected that contract. The post-fix monolithic test was also
independently reported PASS (851.30 sec). The full user-reported
**181/181** run proves this registration set executed without CTest
failures in that native Mac build. It does not prove GPU correctness.

**Provenance resolved (user-supplied `provenance.txt`, 2026-11-20):**
The complete 181/181 native Mac test run was performed from
`/Users/aleksandrfeer/Projects/AIVERSE-terraweave-stage70-local-qa`
at exact source SHA
`f55d8d4e8622d297c4c27266f9289f82cc59baed`.
`git_branch=` was empty because the isolated worktree was in
detached HEAD mode. The user ran
`git merge-base --is-ancestor origin/main HEAD`, which printed
`PASS: main уже включён в TerraWeave`. GitHub independent compare
confirmed that current `main` at `e4129c5062a6ab47e7c70f3fdd483392fde1f679`
is an ancestor. The branch's subsequent commit
`36fd38db0f1198064a58a3e59ac533fcaf3f30c1`
changed only documentation (two handoff files), not the code that
passed. The user's excerpt does not show the actual CMakeCache flags
or JUnit XML; preserve the acceptance-report archive for later audit.
Source has also been read back from GitHub for FBX-off, micro nutrient,
65k cap, and native test registration. No new code was fetched
from `main`.

## Read-only source contract review at stage HEAD

- `CMakeLists.txt` registers both
  `aiverse_terraweave_micro_nutrient_test` and
  `aiverse_terraweave_native_micro_compile_test`.
- `tests/terraweave_micro_nutrient_test.cpp` contains bounded
  65,536-cell regression (`capacitySettings`).
- `engine/world/TerraWeaveNereidMicroNutrients.h` checks bounded
  native NEREID micro search settings.
- `tests/tests.cpp` registers
  `models_fbx_disabled_gives_actionable_error`, while CMake
  propagates `AIVERSE_NO_FBX=1` to `aiverse_tests` when disabled.
- `.github/workflows/terraweave-main-integration-qa.yml`
  performs a complete unfiltered native CTest as a separate
  GitHub CI acceptance gate.
This is a source review, not independent native execution.

## Remaining gates

1. From the local QA worktree collect exact
   `acceptance-reports/provenance.txt`, `cmake-cache-flags.txt`,
   and `ctest.xml`; compare their SHA/flags with the PR source.
2. Validate cross-system **live** TerraWeave + NEREID + PHYSIS +
   Box3D behavior under one ECS World: forest death, wind/fall,
   sediment and nutrient movement, NEREID body identity,
   save/replay/rollback and sustained resource accounting.
3. Run Metal/Vulkan/GLES RHI-specific integration and screenshot/
   GPU parity checks. Their validation is not implied by 181 CPU CTests.
4. Check latest GitHub Actions against current HEAD, including
   Linux GCC/Clang, editors and GPU where available; don't equate
   QUEUED or CANCELLED with PASSED.
5. Merge Draft PR #53 into main only after those checks and
   explicit user approval. Keep per-stage agent handoffs complete.

**Actions in this stage:** GitHub ancestry and source checks plus a
docs-only QA handoff. **No source merge or gameplay code edits.**
