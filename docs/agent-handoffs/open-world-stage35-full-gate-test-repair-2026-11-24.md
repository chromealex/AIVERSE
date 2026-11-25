# W1.35 — full Mac quick-gate test repair

Owner branch `codex/open-world-acceptance-20261124`; code SHA `57a71d4c7`. Previous handoff: `open-world-stage34-native-host-link-2026-11-24.md`.

On code `0a45a8ac5`, `./run-tests.sh --quick --build-dir /Volumes/KINGSTON/AIVERSE-builds/open-world-acceptance-20261124/quick` completed the full native build, then CTest failed 4/465: `aiverse_gameplay_tests`, `skill_catalog`, `skill_routing`, `skill_audit`. Log `/Volumes/KINGSTON/AIVERSE-builds/open-world-acceptance-20261124/quick/test-reports/20261124-191422-58500/ctest.log`. `aiverse_tests` itself passed in 976.68 seconds. All World-labelled tests, including `authority_world_persistence`, `world_persistence_mcp_stdio` and `world_operator_jobs`, passed. `lens_metal_frame` was skipped.

Changes:
- `tests/libraries/gameplay_tests.cpp`: construction fixture now reports the requested placement owner when testing a territory-required rotated footprint. The prior default owner was 0 while placement owner was 1, so the expected success contradicted the policy.
- `tools/skills/catalog.py` and regenerated `skills/catalog.json`: register existing `aiverse-persistent-world` and `aiverse-region-lifecycle` at the generator source, preserving gameplay/simulation routing rather than hand-editing only generated JSON.
- `skills/editor/studio-ui/SKILL.md`, `skills/workflow/aiverse-mcp-coverage/SKILL.md`: distinguish panel ID and JSON-RPC methods from real MCP tool/path references so strict skill audit does not misclassify them.

Evidence after change, Mac AppleClang 17: `cmake --build /Volumes/KINGSTON/AIVERSE-builds/open-world-acceptance-20261124/quick --target aiverse_gameplay_tests --parallel 4` PASS; `ctest --test-dir /Volumes/KINGSTON/AIVERSE-builds/open-world-acceptance-20261124/quick --output-on-failure -R '^(aiverse_gameplay_tests|skill_catalog|skill_routing|skill_audit)$'` PASS 4/4; `python3 tools/skills/catalog.py check` PASS 271 skills; `python3 tools/skills/audit.py --strict` PASS 0 errors/warnings; `python3 tests/skills/catalog_test.py` PASS. Complete gate on this exact code SHA NOT RUN yet.

Feature checklist for this bounded test/skill repair: implementation, architecture, skill routing, dependencies and targeted tests PASS. Native W1 and MCP fixture passed on previous SHA, but exact merged-tree acceptance, Studio visual workflow, broader persistence scenarios, performance, platforms, headless consumer, examples and publishing remain unverified. Next: rerun complete quick gate on this code, then inspect Studio/MCP independently. Keep #135 Draft until the merged-tree gate and missing feature work are complete.
