# W1.38 — exact merged-tree Mac and Studio verification

Owner branch `codex/open-world-acceptance-20261124`. Exact tested SHA `26e6d33bbfccdf6357bfb7022ef283043f406b2d`; main parent remains `73a749b64e419605e07c6ae95b4244fb0ffe1741`. Previous code stage `7750dd3e2` and handoff `open-world-stage37-studio-default-dock-2026-11-24.md`.

## Evidence

- Mac AppleClang 17, KINGSTON: `./run-tests.sh --quick --build-dir /Volumes/KINGSTON/AIVERSE-builds/open-world-acceptance-20261124/quick` PASS on exact SHA. Configure PASS, complete native build PASS, CTest 0 failed out of 465; `lens_metal_frame` skipped by its own environment check. Report `/Volumes/KINGSTON/AIVERSE-builds/open-world-acceptance-20261124/quick/test-reports/20261124-203108-85836` (`build.log`, `ctest.log`, summary).
- All 14 World-labelled CTests passed, including `authority_world_persistence`, `world_persistence_mcp_stdio`, `world_operator_jobs` and 11 W1 region fixtures. The main `aiverse_tests`, native project build, FPS and network fixtures passed.
- Built MCP registry audit on previous merge SHA `715b1afbc`: PASS 330 tools, required families and tools/list parity. No code in the MCP registry changed after that audit. Built end-to-end `world_persistence_mcp_stdio` passed again on exact SHA.
- HTML5 Studio source copied to KINGSTON from the exact branch after code stage `7750dd3e2`: `npm run typecheck`, `npm run build` and full `npm run test:web` PASS. Node dependency versions were resolved from `package.json` without a lockfile; Vite 7.3.8 built 1746 modules. `python3 tools/studio/test_world_persistence_panel.py` PASS 3/3. Native WKWebView hands-on and screenshots NOT RUN.

## Scope boundary and next work

The integrated W1 code and Studio default dock now clear the native Mac quick gate and static/web Studio checks. This does not establish OW-01 feature completion: W1.30c real guarded ECS host integration, stable absent/reification, complete cold NPC/vehicle advancement and reference/pinning admission remain open; OW-02 navigation streaming and OW-03–16 are separate roadmap packages. Performance p95/soak, independent external native consumer, cross-platform runtime, WebView visual workflow, Inspector/graph applicability and publication evidence remain NOT RUN or unreviewed. Do not mark core, close #135, squash into main or publish success claims on this evidence alone.

Feature checklist for the bounded acceptance repair: implementation/architecture/native build/skill routing/MCP fixture/Studio source/tests/dependencies PASS with evidence above. Persistence PASS only in named fixtures; broad durability and migration acceptance NOT RUN. Performance, platform matrix, live Studio, complete examples and publishing NOT RUN. Next stage: W1.30c owner audit and opt-in guarded host fixture, then rerun exact merged-tree gate. The branch is published for review without modifying the existing Draft PR #135.
