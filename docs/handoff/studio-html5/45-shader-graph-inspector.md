# AIVERSE HTML5 Studio — Stage 45: read-only Shader Graph inspector

Date: 2026-11-20
Branch: feature/studio-html5
Parent: fdc4d702185b7787c7ece450ed63f130c5d0855f
Stage commit: see current branch HEAD.

## Implemented
- Added strictly allowlisted read-only native methods studio.shaderGraphs and studio.shaderGraphGet, using existing ShaderGraphOps list/get. No arbitrary graph writes, paths, shader edits, generated shader strings or raw backend MSL/GLSL cross the WebView bridge.
- Graph names validated with a 1..64 ASCII alphanumeric/underscore/hyphen policy. Source filename list filtered; maximum 128 listed graphs. Existing ShaderGraphOps enforces project-local path, <=256 KiB and compilation.
- New HTML5 Shader Graph panel under nested docking shows project graph names, domain, node/sample counts, node operations, connections and outputs.
- Pure TypeScript graph layout helper sanitizes/bounds positions and ignores broken links. Node 22 graph layout test suite added to frontend test:web.
- Native standalone shader-graph-name validator compiled in Linux CI and registered as CMake studio_shader_graph_name test on macOS.
- Browser DEMO has no fictitious graphs; all results are read-only.

## Files
apps/studio-native/StudioShaderGraphValidation.h
apps/studio-native/tests/StudioShaderGraphValidationTests.cpp
apps/studio-native/StudioSession.cpp
apps/studio-native/tests/StudioSessionTests.cpp
apps/studio-native/CMakeLists.txt
apps/studio-web/src/types.ts
apps/studio-web/src/mock.ts
apps/studio-web/src/nestedDock.ts
apps/studio-web/tests/nested-dock.mjs
apps/studio-web/src/panels/StudioPanelRenderer.tsx
apps/studio-web/src/panels/shaderGraphLayout.ts
apps/studio-web/tests/shaderGraphLayout.test.mjs
apps/studio-web/src/panels/ShaderGraphPanel.tsx
apps/studio-web/src/panels/ShaderGraphPanel.css
apps/studio-web/package.json
.github/workflows/studio-html5-web.yml
tools/studio/verify_mac.sh
docs/handoff/studio-html5/45-shader-graph-inspector.md

## Tests
Added pure JS layout and C++ name validator tests but did NOT run the full frontend or native Mac build. GitHub Actions is queued and not PASS. Preview node connections require real WebKit visual QA. All changes are source code only at this point.

## Remaining
- This is a read-only **graph viewer**, not a full editor: no node dragging, wire editing, graph snapshots, schema op catalog, save/apply or live shader preview.
- Existing engine ShaderGraphWindow remains production implementation. Do not remove until proven parity.
- Next: add a versioned graph draft and native validate/save/apply commands with explicit transaction boundaries, then native GPU preview after macOS QA.
- Resolve queued CI / execute native macOS CMake and screenshot tests before merging any Studio work.
