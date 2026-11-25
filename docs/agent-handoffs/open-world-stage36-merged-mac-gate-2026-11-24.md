# W1.36 — merged-tree Mac quick acceptance

Owner branch `codex/open-world-acceptance-20261124`. Exact tested merge SHA `715b1afbc` combines PR #135 source and current `main` `73a749b64e419605e07c6ae95b4244fb0ffe1741`. Merge conflicts in `tests/libraries/gameplay_tests.cpp` and `skills/workflow/aiverse-mcp-coverage/SKILL.md` were resolved without dropping either behavior. Main independently introduced `aiverse_native_registry`; the temporary `aiverse_native_modules -> aiverse_game` link from W1.34 was removed in the merge result to use that more precise owner.

## Actual verification

- Mac AppleClang 17, KINGSTON: `./run-tests.sh --quick --build-dir /Volumes/KINGSTON/AIVERSE-builds/open-world-acceptance-20261124/quick` PASS at SHA `715b1afbc`. Configure, full native build and CTest stages passed. CTest reported 0 failures out of 465; `lens_metal_frame` skipped by its own GPU availability condition. Report directory `/Volumes/KINGSTON/AIVERSE-builds/open-world-acceptance-20261124/quick/test-reports/20261124-195702-16684` (`build.log`, `ctest.log`, summary).
- The World label covered 14 tests. `authority_world_persistence`, `world_persistence_mcp_stdio`, `world_operator_jobs`, all 11 initial W1 region fixtures, `fps` and the main `aiverse_tests` passed in the full run.
- Built MCP audit: `python3 tools/mcp/audit_registry.py /Volumes/KINGSTON/AIVERSE-builds/open-world-acceptance-20261124/quick/aiversemcp --engine .` PASS: 330 registered tools, required families and tools/list / mcp_registry parity. Registry parity is not end-to-end authoring proof.
- `python3 tools/studio/test_world_persistence_panel.py` PASS 3/3 source-level UI tests. HTML5 typecheck/build and actual WebView visual acceptance NOT RUN.

## Feature delivery assessment

Implementation and native build for the integrated W1 source: PASS at tested SHA. Architecture and dependency graph: source and successful link PASS for this tree. Native API, selected persistence/rollback/headless fixtures, MCP stdio fixture, registered skill routing and the Mac quick test criterion: PASS in the stated bounded tests. Studio panel source tests: PASS; actual Studio runtime and visual workflow NOT RUN. Inspector and graph applicability remain unreviewed. Performance/p95, long soak, fresh external project consumer, save migration and cross-platform matrix NOT RUN. Real guarded host installation, complete cold NPC/vehicle advancement, stable absent/reification, linked reference safety and OW-02 navigation streaming remain unimplemented per W1.30b. Do not mark OW-01 complete or change roadmap status to core.

Next: continue W1.30c with an audited existing ECS owner and complete consumer-phase guards, then W1.31 absence/reification and OW-02 navigation residency. Add targeted live fixtures and performance measurements. Run Studio HTML5 build and WebView acceptance. After any new code, rerun the exact merged-tree Mac gate. Keep PR #135 Draft until those feature and acceptance criteria are addressed; do not squash into main yet.
