# Stage 36B integration — Feature Gate, then Studio (2026-11-24)

Target: `integration/building-graph-stage36b-20261124`. Source order: `feature/aiv20261123_feature_gate` merged at `ed65f1494`, then `feature/studio-html5` merged at `538dc6b53c7e87737fb47568f83c31c2197607dc`. No merge to `main`.

The first merge kept Building Graph diagnostics beside Feature Gate readiness operations in `apps/editor/core/Workspace.cpp`, `apps/studio-native/StudioTools.h`, `tools/mcp/audit_registry.py`, and the Studio panel/method registries. It combined the `test:graphs` list in `apps/studio-web/package.json` and reconciled `docs/devlog/systems.json` by system and roadmap identity.

The second merge kept Building Graph diagnostics, Feature Gate, Caverns, Open World, Runtime Debugger and Play Review in the same Studio host/session. `engine/sim/Simulation.cpp` retains the shared `stepWithInput` admission callback and Studio's bounded profiling. `StudioHost.cpp` clears Building Graph overlay/reader and Play Review state on game transitions while preserving Studio's saved launch revalidation on restart. The dock registry now contains each merged panel once, and its default layout and dock tests include the added panels. The Building Generator material binding checks optional style material slots before editing.

Verification on the merged code SHA: `python3.12 tools/site/build.py check` — 49 entries, 0 errors; `python3 -m py_compile tools/mcp/audit_registry.py` — pass; `clang++ -std=c++20 -fsyntax-only` for `engine/sim/Simulation.cpp`, `apps/editor/core/Workspace.cpp`, `apps/studio-native/StudioSession.cpp`, `StudioHost.cpp`, and `StudioGameSurface.cpp` — pass with repository, library, engine/systems and Lua include paths as applicable (StudioHost emitted a pre-existing stb_image_write `sprintf` deprecation warning). `apps/studio-web`: `npm run typecheck`, `npm run build`, `npm run test:graphs` (65/65), `npm run test:web` — pass. `python3.12 -m unittest discover -s tools/studio -p 'test_*.py' -v` — 55/55.

Not run: full native CMake build/CTest, built MCP registry audit, real macOS WKWebView/Metal session, screenshots, and end-to-end Building Graph/Feature Gate/Studio behavior. No runtime or visual parity claim follows from syntax and web tests.

Next: run the merged native Mac gate on KINGSTON, inspect real Studio panels and Building Graph diagnostics in a project, then verify MCP operations against the built server. Keep this branch separate from `main` until those gates are reviewed.
