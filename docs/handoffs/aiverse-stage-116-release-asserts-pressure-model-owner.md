# AIVERSE Stage 116 — Release-only assert elision: pressure and model owner tests

Date: 2026-11-20
Repository: chromealex/AIVERSE
Branch: `fix/postmerge-release-asserts-stage116-20261120`
Parent SHA: `ad175f624ef6f25e7c6dc19cabe199cb30623201`
Previous integration handoff: docs/handoffs/aiverse-stage-114-release-retirement-tests.md
Related Stage 115: docs/aiverse-stage115-release-retirement-validation-20261120 (documentation branch; not merged into main).

## Confirmed failure and root cause
Current main CI run:
https://github.com/chromealex/AIVERSE/actions/runs/44725283646
Linux ARM64 GCC job 136251028811, Configure successful, Build failed:
- `tests/render/retirement_pressure.cpp` lines 15-19 and 54:
  unused-but-set-variable for issue, done, close, wait, release, noToken.
- `tests/render/model_resource_provider_contract.cpp` lines 20 and 25:
  unused-variable for failed and hero.
- Compiler `-O3 -DNDEBUG -std=c++20 -Wall -Wextra -Wpedantic -Werror`.

Root cause: both tests wrap assertions and essential side-effectful
queue operations in standard `assert`, which disappears entirely in
Release `-DNDEBUG`. This both hides tests and removes usage of the
variables checked; `-Werror` then correctly fails the compiler.

## Code change
- tests/render/retirement_pressure.cpp:
  replace every `assert(expr)` with `AIVERSE_TEST_CHECK(expr)`
  using the already-shared `tests/AlwaysCheck.h` header. Retains all
  checks, queue collection calls and ordering.
- tests/render/model_resource_provider_contract.cpp:
  same conversion; cache insertion and ownership checks now execute
  under Release.
- No production queue/cache changes, no `NDEBUG` disable, no global
  warnings-as-errors relaxation.

## Verification actually performed
- Fetched and inspected exact two affected files, shared
  `tests/AlwaysCheck.h`, and CI job failure logs on the GitHub backend.
- Source conversion checked: `assert(` count preserved and zero raw
  `assert` remains in the two modified test files.
- GitHub compare verified only two C++ sources plus this handoff
  are changed in this fix branch.
- Real full native compile, CTest, macOS Metal, Vulkan, GLES: **NOT RUN**
  for this change when authoring Stage 116. Do not mark them PASS.

## Next action
Merge the focused fix only after verifying PR mergeability/head SHA;
inspect newest CI on merged `main`. If compiler advances and fails
on another Release `assert`-related diagnostic, fix precisely with a
new stage/handoff. Once complete Build passes, execute the full CTest
on the **same latest** `main` SHA; note that the older full workflow
run 44709805590 targets obsolete `fc42156b`. Avoid touching
unrelated parallel AIVERSE branches/worktrees.
