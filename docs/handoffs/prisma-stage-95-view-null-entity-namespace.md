# PRISMA Stage 95 — Resolve View packets C++ namespace failure

Date: 2026-11-20
Repository: chromealex/AIVERSE
Branch: integration/prisma-motionweave-view
Previous: docs/handoffs/prisma-stage-94-motionweave-hybrid-native-operation-parse.md

## User-reported native build failure
At 66% of macOS integrated Release build:
tests/view/packets.cpp lines 54, 79, 80:
```
error: use of undeclared identifier 'kNullEntity';
did you mean 'aiverse::kNullEntity'?
```
The tests use `using namespace aiverse::view;`, while the ECS null
entity is declared in `namespace aiverse` (engine/core/ECS.h).
With active `AIVERSE_TEST_CHECK`, the stale unresolved token is compiled
in Release instead of being discarded like the former assert expression.

## Actual source change
Commit e26d34615dacce7f585388447cb02fd6046d37fd:
- tests/view/packets.cpp: qualify exactly three references as
  `aiverse::kNullEntity`.
- The test semantics are unchanged. No ECS/View production APIs modified.

## Verification performed
- Read the original file and identified exactly 3 unqualified uses.
- Read back the GitHub updated file: exactly 3 qualified uses,
  0 unqualified uses.
- Inspected the other 18 `tests/view/*.cpp` files for `kNullEntity`:
  no other uses found.
- Native compile, CTest, graphics tests: NOT RUN by assistant.
  The reported failure is from earlier local checkout and remains
  subject to a macOS rebuild after updating the branch.

## Next step
Once the current build attempt ends:
```sh
cd ~/Projects/AIVERSE-prisma-motionweave-clean
git status --short
git fetch origin
git switch --detach origin/integration/prisma-motionweave-view
cmake --build build/merge-validation --parallel 4
```
Then run one consolidated CTest after all targets link. Preserve any
uncommitted worktrees; do not change main/source agent branches.
