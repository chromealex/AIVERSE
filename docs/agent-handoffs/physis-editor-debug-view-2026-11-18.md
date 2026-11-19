# PHYSIS Stage 17 — opt-in dockable editor nature debug view (2026-11-18)

**Repository** `chromealex/AIVERSE`. **Branch** `feature/physis-nature-simulation`, draft PR #25, based on `feature/universal-entity-inspector-fps`.  
**Status:** code, test source, CMake/CI registration and docs committed. **Native compile/CTest/CI PASS, macOS editor launch, screenshot, interactive click-through and GPU test have NOT RUN and are NOT VERIFIED.** A GitHub connector write is not a substitute for an editor build.

## Implementation commits

- `897ce30c1d4638a273bfb0201cb2c1042ba92046` — `engine/physis/PhysisDebugPalette.h`, CPU-only display palette and transparency for missing cells.
- `6d965481bbcd42b4f2c06a6364cb6dada2f93809` — `apps/editor/windows/PhysisWindow.cpp`, new dockable PHYSIS heatmap, live ECS read-only source, explicit isolated demo.
- `360675a72b95adfe3bba7ae33f2f4d92e4ff5970` — clamp requested editor cell origins and guard selected-cell Fixed coordinate conversion.
- `598c195a71cbbac864f4ccd37810f4b3f3d9c622` — 2D wind zone (W), tornado (T), hurricane (H), gravity field (G) radius outlines in the panel.
- `3d0b782fdb42f85dd16b204a0e78587d86a1db4b`, `dae6127dd8cc038afcd1ec905cacf2e4dc6909a5` — `tests/physis_editor_view_test.cpp`, stable palette endpoints/invalid masking, ECS raster snapshot immutability; fix C++ macro/Fixed accessor syntax.
- `9cea582a9dfc15f335398789e90b72b5befa2448` — `CMakeLists.txt`, default-build `aiverse_physis_editor_view_test`, CTest `physis_editor_view`.
- `511c775abe4556056d4ad0c81ca1f3ddacaf6356` — focused `.github/workflows/physis.yml` includes the test target in Linux/macOS builds (editor binary is NOT built by that job).
- `921a51ce52e10b31676163671abca4fcb704b7de` — `docs/physis.md`, demo/live behaviour, API and handoff.
- `1331d47f72c2a6d77fe09724505f6558e0ee5ea4` — root `AGENTS.md` ownership/verification rules.
- `0929001ce3160e89fd95b6a90d5bbc9ff5c08ec5` — `engine/physis/AGENTS.md` subsystem notes.
- `49a23e940a8d2d754e6a9559effa79202e5b941d` — `skills/world/aiverse-physis/SKILL.md` guidance for future AI agents.

## Architecture, UI and data ownership

The editor's pre-existing `CMakeLists.txt` uses `file(GLOB AIVERSE_EDITOR_WINDOWS CONFIGURE_DEPENDS apps/editor/windows/*.cpp)`. The new window class is registered through `AIVERSE_EDITOR_WINDOW(PhysisWindow)`, id `physis`, title `PHYSIS · Природа`, group `world`, dock `right-bottom`, `openByDefault=false`. No edits to shared `Editor.cpp`, `Editor.h`, `SceneWindow.cpp`, `InspectorWindow.cpp`, or the independent editor-unification branch were necessary. **Do not treat the standalone PHYSIS panel as a finished common entity Inspector/Scene gizmo.**

The window checks `Editor::sim()` for a `physis::State` in `sim->world()`:
- If available and demo is not selected, the UI samples authoritative ECS **read-only** (no calls to `Simulation::step`, no mutations).
- If unavailable, it displays an explanation and offers `Создать / сбросить демо`. This allocates an independent `std::unique_ptr<aiverse::World>`, seeds a 32×32 grid with climate-zone gradient, WindZone, Tornado StormEvent, GravityField and fire, and simulates 25 fixed ticks. `Следующий тик` and `+60 тиков` operate on this private demo world only.
- Controls: 14 heatmap layers, thermal/monochrome/ocean palette, stride 1–8, extent 8–64 PHYSIS cells, input X/Z center, explicit refresh, clickable selected cell with Fixed value, wind and gravity vector.
- The panel draws an ImGui `ImDrawList` 2D heatmap and optional circles for wind/storm/gravity volumes; the circles are not Box3D bodies or real 3D Scene overlays. No authoritative ECS entities are created when reading live state.
- `engine/physis/PhysisDebugPalette.h` depends only on PHYSIS diagnostics and `<cstdint>`; no renderer, ImGui, genre, NEREID, TerraWeave or Box3D coupling is introduced into PHYSIS core.
- Non-3D scalar raster layers use the Stage 15 fast path; Wind/Gravity uses Stage 16 transient COW snapshot-backed `FieldSpatialIndex`. The panel caches one raster per source pointer/tick/parameter revision and exposes manual refresh for same-tick structural edits/rollback. It does not store pointers to PHYSIS ECS components in the cache.

## Public API / usage

```cpp
#include "engine/physis/PhysisDebugPalette.h"
const auto color = aiverse::physis::diagnosticColor(
    raster, cellIndex, aiverse::physis::DebugPalette::Thermal);
// rgba channels 0..255; missing raster.valid cells => transparent.
```

The editor window is found through **Мир → PHYSIS · Природа**, closed by default. It is automatically compiled into `aiverse_editor` when the existing editor target is built with `AIVERSE_BUILD_EDITOR=ON`. It does not automatically install PHYSIS into a project's GameModule; that must be enabled explicitly in the game's C++ setup before live data can be displayed.

## Verification (ACTUAL)

- GitHub connector created/updated the files and stage handoff: completed.
- Local checkout `git ls-remote`: **failed** (`Could not resolve host: github.com` in current sandbox container); no full source checkout.
- `aiverse_editor` compilation: **NOT RUN**.
- `physis_editor_view` CTest / unified PHYSIS tests: **NOT RUN**.
- Focused PHYSIS Linux/macOS CI: workflow source updated, **no completed result verified**.
- macOS graphical editor, menu, dock layout, hover/click and screenshot: **NOT RUN**.
- NEREID water and TerraWeave terrain integration: deliberately unchanged and **NOT IMPLEMENTED** here.

Suggested exact commands (from a valid repository checkout):

```sh
./run-tests.sh --filter '^physis_'

cmake -S . -B build/physis-editor -DCMAKE_BUILD_TYPE=Debug \
  -DAIVERSE_BUILD_EDITOR=ON -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_ENABLE_PHYSICS=OFF
cmake --build build/physis-editor --target aiverse_editor aiverse_physis_editor_view_test -j4
ctest --test-dir build/physis-editor --output-on-failure \
  -R '^(physis_editor_view|physis_diagnostics|physis_spatial_index|editor_scene_adapter)$'
```

Then launch the editor, open **Мир → PHYSIS · Природа** and manually test:
1. A project without PHYSIS shows a clear absence message; explicit demo creation works and does not alter the project, Box3D world, save or undo stack.
2. Demo: temperature, wind, gravity, rainfall, fire palettes update after manual steps. Tornado/WindZone/Gravity circles display in correct X/Z locations and are clipped to the panel. Toggle palettes/stride/extent, click valid and invalid cells, test negative/large centre coordinates.
3. Native GameModule with PHYSIS installed: the panel reads the true authoritative World read-only, and game ticking updates the cache. Stop/start/reload and rollback-to-same-tick then click refresh; no stale pointers.
4. UI clipping, resizing, dock transitions, light/dark themes, localization and loading/cancelling game project.
5. Confirm old editor Scene/Hierarchy/Inspector paths remain unchanged, then document actual screenshots/logs and perf numbers rather than estimating.

## Limitations / next agent

- **No Scene viewport projection** of the storm/wind/gravity volumes; this is an internal 2D heatmap of a bounded X/Z patch. Drawing within Scene requires a tested viewport projection adapter in the **editor-owned** code, not PHYSIS core.
- **No editable PHYSIS ECS Inspector** (live mutation would require a safe editor command and undo/rollback integration); only selected cell read-only details and demo-only step buttons.
- The live data cache invalidates on source pointer or tick change, but cannot detect same-tick edits/rollback; manual refresh is the intended fallback.
- COW snapshot + tile build on refresh and thousands of ImGui rectangles per visible 64×64 patch need profiling in a real editor. Performance not measured.
- No automatic Terra/Water/VFX/Box3D integration. NEREID owns water, TerraWeave owns land/foliage. PHYSIS rain is normalized intensity, not water volume; read the separate ownership contract.
- The standalone demo is not a production GameModule/host integration and has no native serialization workflow.
- Potential additional compile warnings must be fixed after the **first real editor build**; do not merge PR #25 while compiler/CTest/GUI verification is absent.

**Next owner / bounded task:** PHYSIS/editor agent pulls latest PR #25 head, builds and opens `aiverse_editor`, fixes compiler/ImGui/version issues, runs CTest; afterwards builds a true opt-in 3D Scene viewport overlay using the same `DiagnosticRaster` and `World` read-only data, with editor-unification PR #7 coordination. Separately profile spatial index large-world query patterns and only then add jobs/streaming. End each implementation stage with a new handoff containing implementation SHA and actual test results.
