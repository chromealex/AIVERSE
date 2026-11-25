# Stage 37 — Studio and MCP acceptance runner

Implementation: 21ea3c8c52942b4c1f3c3264d7f231fecbd63d24, branch integration/building-graph-stage36b-20261124. Stage 36D native tests passed in user-provided Mac logs (480/481 full run plus separate passing opt-in Metal test). The quick build did not include the HTML5 Studio targets. New tools/studio/verify_building_integration_mac.sh reuses the relocated all-tests-v2 cache, verifies physical source/build paths and mounted KINGSTON, and stores logs, web output, npm cache and temporary files beneath the existing external build tree. No deletion of CMake cache or full clean build.

Run:
```sh
cd /Volumes/KINGSTON/AIVERSE-builds/AIVERSE-building-stage36c
git pull --ff-only
bash tools/studio/verify_building_integration_mac.sh "$PWD/all-tests-v2"
```

Stages: rebuild MCP target incrementally, real tools/list + mcp_registry feature registration audit, standalone Building diagnostics call with no owner and rejection of zero/overflowing scene IDs, npm test:web, TypeScript/Vite, Studio Python tests, reconfigure native HTML5 in existing cache, build app and topology/session targets, run selected Studio CTest contracts. npm dependencies installed on KINGSTON checkout; branch has no npm lockfile, so installation is not fully reproducible. Node 22+ required. Automated checks newly authored, NOT RUN by agent. Logs printed at exit; return code propagated. No visual or live attached runtime MCP acceptance claimed. Standalone no-owner result is intentional and not proof of a populated graph.

Manual next: open the generated native Studio app with an isolated project, verify room topology and routes, texture provenance, graph overlays and inspector at normal and narrow dock widths. Exercise actual building runtime owner and check MCP diagnostics against native scene. Record screenshots/actual output, exact SHA and feature evidence before merged-tree main gate. No main merge.
