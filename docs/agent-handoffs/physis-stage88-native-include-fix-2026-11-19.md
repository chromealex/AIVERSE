# PHYSIS Stage 88 — native AppleClang include fix for Stage 87 host tests

Date: 2026-11-19
Repository: `chromealex/AIVERSE`
Branch: `feature/physis-terraweave-fracture-host`
Target: Draft PR #38 -> `feature/physis-fracture-delivery` (main unchanged).
Prior handoff: `docs/agent-handoffs/physis-stage87-terraweave-fall-host-2026-11-19.md`.
Code commit: `83a4d2dfc67ea097f3566b2307de998057b098ea`.
Documentation commit: `git log -1 -- docs/agent-handoffs/physis-stage88-native-include-fix-2026-11-19.md`.

## Reported actual native failure

Owner built the detached worktree at Stage87 head `cfee80151`, using
`python3 tools/aiverse-progress.py --mode all` with
`aiverse_physis_nature_fall_host_test` and
`aiverse_physis_fracture_outbox_test`, `AIVERSE_ENABLE_PHYSICS=ON`
and `^physis_(nature_fall_host|fracture_outbox)$` filter.

The monitor ended **FAIL (exit 2)** in 23 s, **0 warnings, 2 compiler error lines**:
```text
physis_nature_fall_host_test.cpp:120: no type named 'InstallOptions' in namespace 'aiverse::physis'
physis_nature_fall_host_test.cpp:126: no member named 'install' in namespace 'aiverse::physis'
```
Source report:
`/Users/aleksandrfeer/Projects/AIVERSE-physis87/build/physis87-validation/aiverse-reports/20261119-193323-84554`.
CTest did **not** run in the failed build; no test result can be inferred.

## Cause and exact code change

`tests/physis_nature_fall_host_test.cpp` included
`engine/world/PhysisNatureFallHost.h`, which depends on
`PhysisFractureDelivery.h` + `Simulation.h`, but did not include
`engine/physis/PhysisIntegration.h`. The native test's
`NativeFallHost::setup` uses `physis::InstallOptions` and
`physis::install`, both declared only in `PhysisIntegration.h`.

Added exactly one direct include in the test:
```cpp
#include "engine/physis/PhysisIntegration.h"
#include "engine/world/PhysisNatureFallHost.h"
```
No runtime algorithm, API, ECS schema, serialization, TerraWeave/Box3D,
or build flags changed. The fix avoids introducing a PHYSIS installer
dependency into the host header.

## Actual verification status

Source-edit pushed to GitHub; **native compilation and CTest after the
new commit are NOT RUN**. The earlier Stage86b 28/28 PASS applies to the
parent branch, not Stage87/88. The optional real TerraWeave/Box3D test
is still not compiled in this PHYSIS-only worktree; it requires a
coordinated combined checkout.

## Next exact native command, retaining detached worktree

```sh
git -C ~/Projects/AIVERSE fetch origin
cd ~/Projects/AIVERSE-physis87
git status --short
git switch --detach origin/feature/physis-terraweave-fracture-host
python3 tools/aiverse-progress.py --mode all \
  --build-dir build/physis87-validation \
  --define AIVERSE_BUILD_TESTS=ON \
  --define AIVERSE_ENABLE_PHYSICS=ON \
  --target aiverse_physis_nature_fall_host_test \
  --target aiverse_physis_fracture_outbox_test \
  --test-regex '^physis_(nature_fall_host|fracture_outbox)$' \
  --jobs 4
```

If the worktree contains uncommitted changes, **do not switch/reset** before
inspecting them. Record first new compiler/test failure in a further handoff.
On successful focused test, run broad `^physis_` suite and subsequently
the optional **real** TerraWeave+Box3D composite test in a safely
coordinated integration checkout. Keep PR #38 Draft and main untouched.
