# W1.27 — integrated Open World / Studio acceptance, not yet verified

Area W1. Integration branch `feature/aiv20261124_open_world_integration`. Source Open World e15f2bc403bdd68e87d8ee04742850e298b0cb2e; Studio b1dc375064e17773d56afa4e863667b40b9bef16; main base 71aa5c93afa5993c4e45ba39f1bf5454343389ea.

## Integration and fixes
- 4c1f745930d085aafd14e6f37747dc0e7aa97d87: actual two-parent merge of main and Open World. PR #134 merged into the integration branch only. GitHub compare confirms 213 Open World paths relative to main, and 43 main paths relative to Open World. Preserved exact blob/mode for nonoverlapping main files; applied additive changes in CMakeLists.txt, Workspace.cpp, systems.json and skills/catalog.json. JSON parsing and duplicate skill-ID checks passed in the integration script.
- f5870d0e3c894ce3c02516f8b53aa4085847394c: actual two-parent merge of Studio delta into above. Compare showed 11 modified/added paths; the twelfth (StudioTools.h) already contained the shared allowlist. Preserve newer Studio registrations and project guard, no duplicate group.
- 61053ca9: fix missing RegionChangeCursor::last_ member found during source review.
- 44c0eab0, 1c59f020: real stdio MCP acceptance fixture and CTest registration; link aiverse_pack to ledger fixture for SHA-256 symbols.
- 618ba794, 1177ed1b: dedicated authority_world_persistence CTest mode (static, async and streamed restart fixtures without external game content).
- 6cc9acc7: focused W1 workflow includes authority, operator jobs and actual MCP executable and test.
- 476d8a04: acceptance roadmap remains in-progress.

## Acceptance evidence / manual feature checklist
PASS: Git merge ancestry and exact expected changed-path counts; merged JSON parsed and catalog uniqueness checked; handoff written. These are source/merge checks only.
NOT RUN: native compile, CTest, MCP registry/calls, Studio typecheck/build/test:web, actual WebView and Mac screenshot, headless gameplay, save migration execution, performance/long-duration and cross-platform/independent host acceptance. No workflows reported for f5870d0e at the initial check; do not interpret this as green. No access to the owner's macOS/KINGSTON build environment in this session. Public APIs, ECS authority, persistence, skill/MCP/Studio, dependency and platform claims remain provisional until acceptance; graph/Inspector coverage must be reviewed per applicability, not assumed. Existing legacy actor adapter is not arbitrary game-state support. Do not mark W1 core or publish success devlog.

## Next exact commands (Mac owner gate)
From a clean checkout of the integration branch, ensure /Volumes/KINGSTON is mounted and writable, then run:

```bash
git fetch origin
git switch feature/aiv20261124_open_world_integration
git pull --ff-only
git rev-parse HEAD
test -d /Volumes/KINGSTON && test -w /Volumes/KINGSTON
./run-tests.sh --quick --build-dir /Volumes/KINGSTON/AIVERSE-builds/open-world-integration/quick
```

Use the actual SHA and test reports; inspect failures and retain CI URLs/logs. Separately run Studio typecheck/build/WebView acceptance with output/caches on KINGSTON, plus built MCP registry audit. Source tests alone are not acceptance. Main remains unchanged, and no merge into main before successful merged-tree Mac quick gate.
