# PHYSIS Stage 96 — native combined Box3D compile: installer include and TerraWeave warning

Date: 2026-11-19
Repo: `chromealex/AIVERSE`
Branch: `integration/physis-terraweave-complete-20261119`
PR: https://github.com/chromealex/AIVERSE/pull/40 (Draft)
Previous handoff: `docs/agent-handoffs/physis-stage95-real-box3d-nereid-include-fix-2026-11-19.md`

## Native evidence actually supplied by owner

The user ran the full one-pass build from isolated
`~/Projects/AIVERSE-physis-terra-final`, after Stage95's
`AIVERSE::lib_nereid_water` target dependency fix.
The missing `NereidErosion.h` include was **not observed** again.
The full build advanced to ~60%; two AppleClang errors stopped
`aiverse_physis_terraweave_fall_bridge_test`:

```text
tests/physis_terraweave_fall_bridge_test.cpp:27:13:
error: no type named 'InstallOptions' in namespace 'aiverse::physis'
tests/physis_terraweave_fall_bridge_test.cpp:34:9:
error: no member named 'install' in namespace 'aiverse::physis'
```

One separate warning was visible while building
`aiverse_terraweave_model_test`:

```text
tests/terraweave_model_test.cpp:114:27: warning:
comparison of integers of different signs:
'const int' and 'size_t' [-Wsign-compare]
```

The native build stopped with `make: *** [all] Error 2`.
The report was
`/Users/aleksandrfeer/Projects/AIVERSE-physis-terra-final/build/physis-terraweave-onepass/onepass-reports/20261119-183639-46473`.
**CTest did not run** on this source; no combined Box3D runtime success
or full-repo PASS is claimed. The build reaching ~60% does not prove the
remaining executable targets link or run.

## Root cause and exact code changes

The PHYSIS -> TerraWeave test uses `physis::InstallOptions` and
`physis::install`, both declared in
`engine/physis/PhysisIntegration.h`. The test included only
`engine/world/PhysisTerraWeaveFallBridge.h`, which deliberately does
not include the PHYSIS installer (its dependencies are lower-level).
This is the same missing-direct-include category previously fixed in
`tests/physis_nature_fall_host_test.cpp` Stage88.

- Code commit `126e8bdb07811032b73f9506ccea6e9885abbe31`:
  Add `#include "engine/physis/PhysisIntegration.h"` explicitly
  before `#include "engine/world/PhysisTerraWeaveFallBridge.h"`.
  No engine runtime, ECS schema or API change.
- Code commit `53f302befbfb50e997dd49bb05a510cbfa6bcb30`:
  Change `tests/terraweave_model_test.cpp` condition from
  `assert(p.material == layer)` to
  `assert(p.material >= 0 && static_cast<size_t>(p.material) == layer)`.
  This rejects negative material indices and eliminates the
  signed/unsigned comparison warning without relaxing the assertion.
  No canonical TerraWeave implementation change; only the combined
  integration branch's test fixture is modified.

Both files were read back from GitHub and verified to contain the
respective edits. Native compilation after the fixes: **NOT RUN**.

## Remaining work and run gate

The user's stated goal is to complete source fixes before the next one-pass
full acceptance; do not run isolated little CTests or claim all features
finished based on static reading alone. Further actual compiler failures
(if any) must be fixed without loosening test conditions and recorded in
a new handoff. Current change only addresses observed native evidence.

For the owner, reuse the existing clean detached worktree and its partial
build cache, after fetching latest source:

```bash
cd ~/Projects/AIVERSE-physis-terra-final
git status --short
git fetch origin
git switch --detach origin/integration/physis-terraweave-complete-20261119
bash tools/physis-terraweave-final-validation.sh
```

Do **not** switch if tracked working files have uncommitted edits.
The runner configures a Debug Box3D build, builds all native test
targets once, verifies real `physis_terraweave_fall_bridge`
registration, then runs a single unfiltered CTest if build succeeds.
Do not merge into `main` before full native acceptance.
This handoff commit SHA can be found with
`git log -1 -- docs/agent-handoffs/physis-stage96-real-box3d-installer-header-2026-11-19.md`.
