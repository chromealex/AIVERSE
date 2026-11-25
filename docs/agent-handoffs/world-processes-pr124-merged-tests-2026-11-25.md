# PR #124 merged-tree test repair

Date 2026-11-25. Branch `fix/pr124-arm64-acceptance`; implementation `faafdde3b`.

- `tests/geometry/surface_proxy.cpp`: create and load real project documents before geometry LOD save; use canonical temp root for the project journal. Focused `surface_proxy` CTest 1/1 PASS.
- `tools/skills/catalog.py`, `skills/catalog.json`, `skills/core/aiverse-random/SKILL.md`: register Spatial, persistent-world and region-lifecycle skills already present in `main`, preserve their routing metadata, and state the random library target. `catalog.py check` validated 286 skills; routing fixture PASS.
- `tools/skills/audit.py` and World Processes skill files: recognize the existing `addLinked`/`addForest` operation registration, distinguish MCP protocol methods and Studio panel/module IDs from repository paths/tools, and replace placeholder handoff paths with real glob references. Strict audit 0 errors/0 warnings. No MCP tool or authorization behavior changed.

Merged-tree test commit `81d8bb0b3` passed configure and build. The four previously failing CTest cases (`surface_proxy`, `skill_catalog`, `skill_routing`, `skill_audit`) passed at the start of the rerun; the remaining 474 are still running. Evidence report `/Volumes/KINGSTON/AIVERSE-builds/pr124-merged-tree/test-reports/20261124-220524-97433`. Full Mac quick gate and Linux CI on this code NOT YET PASS. Next: finish gate, run Linux CI on current branch, then address any further failures and remaining feature acceptance before parent PR integration.
