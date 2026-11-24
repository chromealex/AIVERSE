# W1.31 — first native acceptance repair

Owner branch: `codex/open-world-acceptance-20261124`, based on PR #135 HEAD `3a9baf0e01e9755372a145834951a7be5f53c5f0`. Code stage: `c91821abe`.

## Scope and changes

The first owner-Mac quick gate failed at 31% of the build. `tests/world/region_entity_ledger_test.cpp` declared two `changes` variables in one scope, causing ten compiler errors in the latest-state paging fixture. The first is now `recentChanges`; assertions are unchanged. `aiverse_region_ecs_bridge_test` linked only `aiverse_engine` while `RegionEcsBridge::crossings` calls `SpatialWorld::cell` from `aiverse_world`. Its CMake target now links the existing world owner. No runtime API, save schema or authority behavior changed.

Paths: `tests/world/region_entity_ledger_test.cpp`, root `CMakeLists.txt`.

## Evidence

- FAIL, initial Mac AppleClang 17 quick gate on source `3a9baf0e`, `./run-tests.sh --quick --build-dir /Volumes/KINGSTON/AIVERSE-builds/open-world-acceptance-20261124/quick`; build stopped at 31%. Log: `/Volumes/KINGSTON/AIVERSE-builds/open-world-acceptance-20261124/quick/test-reports/20261124-182036-71765/build.log`.
- PASS, Mac AppleClang 17 focused build: `cmake --build /Volumes/KINGSTON/AIVERSE-builds/open-world-acceptance-20261124/quick --target aiverse_region_entity_ledger_test aiverse_region_ecs_bridge_test --parallel 4`.
- PASS, focused CTest: `ctest --test-dir /Volumes/KINGSTON/AIVERSE-builds/open-world-acceptance-20261124/quick --output-on-failure -R '^(region_entity_ledger_test|region_ecs_bridge_test)$'`; 2/2 passed.
- Full quick gate restarted after the fix; result pending when this handoff was written. Other native, Studio, MCP, performance, platform and runtime acceptance remains NOT RUN or unverified.

Feature checklist: implementation PASS for this bounded build repair; architecture, dependencies and test registration PASS by source and focused native build; tests PASS only for the two fixtures above. Native consumer, skill, MCP, Studio, Inspector, persistence behavior, graphs, performance, platforms beyond this Mac build, headless gameplay, examples, publishing and overall W1 acceptance remain NOT RUN or NOT APPLICABLE to this test-build repair. Do not infer full feature readiness.

Next: finish the restarted quick gate, repair any later compile/test failures in separate stages, then exercise built MCP and Studio. W1.30c host wiring and W1.31 stable absence/reification remain separate feature work. Keep #135 Draft and do not merge before exact merged-tree Mac gate.
