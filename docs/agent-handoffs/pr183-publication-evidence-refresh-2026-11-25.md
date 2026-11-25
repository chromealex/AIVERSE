# PR #183 current-tree evidence refresh

Stage commit: `e32d526298710f2051b4e944bbe4ec1b40789db3`. Integration branch: `integrate/pr183-publication-20261125`.

Changed paths: `docs/features/graph-framework.json`, `docs/evidence/pr183-publication-current-f73d31f26.json`, `docs/evidence/pr183-publication-current-f73d31f26.log.txt`.

On clean commit `f73d31f26c53dd48c3909d8bbc17f90090c702c0`, `ctest --test-dir /Volumes/KINGSTON/AIVERSE-builds/pr183-publication-20261125/mac --output-on-failure -R '^(vfx_graph_ops|studio_session)$'` passed 2/2. The report and log are committed. The previous full Mac quick gate passed 590/590 on code-identical commit `8f6aef2f4dc80fe18b9c5df3da3f4462ac1151c3` and is documented in `pr183-publication-mac-gate-2026-11-25.md`.

The manifest now points to the current test report rather than reports from an unreachable source branch. Implementation, performance, platform runtime acceptance and full feature testing remain `not_run`; no readiness claim is made. Next: complete these checks on a synced integration tree, rerun strict gate and Mac quick gate, then squash into main.
