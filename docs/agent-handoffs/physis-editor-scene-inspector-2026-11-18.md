# PHYSIS Stage 18 — Scene viewport, Inspector authoring and generic native config (2026-11-18)

**Repository:** `chromealex/AIVERSE`. **Feature branch:** `feature/physis-nature-simulation`, draft PR #25.  
**Prerequisite merged main:** `810feba70f01cdc35003ac80ea8486f39dc16049` (first parent PHYSIS `4eaee58e20631c2466eba3694dfd9b2700ab18d8`, second parent main `9ee6ff6fbf390bd45363bc92c838f492dfe92eee`). Detailed merge handoff: `docs/agent-handoffs/physis-main-merge-2026-11-18.md`.

**Stage 18 implementation/test commits, in order:**
- `ece9eb56652325e7c965c634df023f0dc73f9f4c` — `engine/physis/PhysisSceneGeometry.h`: pure `Fixed` ground tile quads for diagnostic rasters.
- `6360d4aeb4c81e75637dcbd301421493d490da40` — `apps/editor/PhysisSceneOverlay.h`: opt-in Scene-camera projection, bounded diagnostic heatmap, wind/storm/gravity zone outlines; **read-only ECS**.
- `1ea8cb9b4cfb968f8296dd48025bcd2082e6ee0f` — `apps/editor/windows/SceneWindow.cpp`: invoke overlay before shared HUD without modifying selection/genre behavior.
- `b9c26382b215db19d34d9448719bdfadc0a96a9c` — `apps/editor/windows/PhysisWindow.cpp`: overlay toggles, radius/stride/opacity, Inspector focus.
- `b2be39b0f4a78ded68cdeb9663454c3d55ed2fa9` — `apps/editor/PhysisAuthoring.h`: strict map-level JSON validation and one-Undo-step MapDoc writeback for both ordinary maps and verbatim FPS documents.
- `7b060246705d31862597746ac0bc6054933c299f` — `apps/editor/windows/InspectorWindow.cpp`: shared MapProps PHYSIS panel, grid/climate/feature controls and extended JSON editing; atomic Apply/Undo.
- `65196e37688aacbb39d00007ee965378aba3b2d2` — `tests/physis_authoring_test.cpp`: RTS/FPS roundtrip, atomic reject, no-op edit, MapDoc Undo/Redo.
- `bc3fb29714e2f87f8de3eb8c05bbc5ea18e8080b` — `tests/physis_scene_geometry_test.cpp`: grid origin/stride, bounds, world read-only.
- `231f44e635763cadc41898450d4e8cb13b6fe9b0` — `CMakeLists.txt`: geometry and authoring tests, editor_core conditional.
- `2067a7df7b8e002a450ecb22bbc1fd7d7c44f28b` — `engine/physis/PhysisProjectConfig.h`: generic `physis::optionsFromMap` / `installFromMap` native map loader, opt-in.
- `c7e828e7cc5fc66f4e5f0c9ad4ae882649c26313` — `tests/physis_project_config_test.cpp`: strict authored config, 2 native GameModules, deterministic peer state/save/restore/checksum source fixtures.
- `2fc94f108ebd1a33b1414d98ca24e5041f77be40` — CMake generic project-config test target.
- `e2e3a0fb804507a978d193a2eaacfa7c395141b0` — focused `.github/workflows/physis.yml` builds geometry/config tests on Linux/macOS.
- `453c283e7a53f12b321bfd1e018529fa2129c25d` — `.github/workflows/studio-validation.yml` builds and runs PHYSIS authoring/geometry/config integration tests with editor.
- `87e7e7dfc4e39e5599e70bc61131728dd3592c75` — `docs/physis.md` public usage, ownership and bounds.
- `9ee3efab2b1cc041f16a5b8471b76bdc6fc494d2`, `64e7088b2640291b0ad8eaf9f73f899da813cd21` — root and PHYSIS `AGENTS.md`, handoff/ownership instructions.
- `bcda54e55c806da453932dc7aef2de5ba36fc869` — `skills/world/aiverse-physis/SKILL.md`, AI reuse and native setup.
- `5a91ead851e4ed9c3572ea356ff6fa44391652b1`, `e797d5001492fb0aa8338e71163b374dc169cb7f`, `d930c2586ae04409a158e2c9b0ba147e34445785` — register PHYSIS in skill catalog generator/aliases and generated JSON (214 entries).
- `be57f7bdc22aa852ca960a01ede982d9733a24a4` — append integration note to `docs/editor-unification-handoff.md`, without claiming that PR #7 acceptance is complete.

## Architecture / public API

- `PhysisSceneGeometry.h` is CPU-only, immutable and genre-neutral. It converts `DiagnosticRaster` into `SceneDiagnosticTile` world-space quads in `Fixed` metres with elevation and a tiny lift.
- `apps/editor/PhysisSceneOverlay.h` is the only editor camera/render adapter. It reads **live `Editor::sim()->world()`** and projects cells through the shared `RTSCamera::worldToScreen` into `ImDrawList` over the real Scene viewport. Heatmap and rings are clipped and bounded (radius 6..24 climate cells, stride 1..4, at most 32 zone rings) and **do not mutate ECS**, call Box3D or instantiate geometry in Scene.
- `PhysisWindow.cpp` toggles the Scene overlay, layer/gradient and parameters. The offline demo remains a separate private World, never projected as an authoritative scene. The Scene overlay is opt-in and off by default; it requires PHYSIS installed by the running GameModule.
- `apps/editor/PhysisAuthoring.h` accepts map-level `"physis"` configuration in `MapData.extra` (generic map) or `MapData.fps` (FPS verbatim map). `validate` strictly bounds grids, seed, temperature, humidity, opt-in flags and flood ownership. `apply` checks everything before `MapDoc::beginEdit`, creates exactly one history snapshot for a meaningful edit, preserves unknown keys, supports remove/undo/redo and does **not** write to live PHYSIS.
- `InspectorWindow.cpp` under MapProps offers **Создать PHYSIS**, **Настроить PHYSIS**, **Удалить PHYSIS** with structured fields and an extended JSON tab. Clicking Apply parses the entire draft, validates and commits once. The common editor Selection/Inspector and existing Undo stack are reused; no FPS/RTS-specific mechanics introduced.
- `engine/physis/PhysisProjectConfig.h` is a reusable standalone/native option loader. `optionsFromMap(Json map)` returns `std::nullopt` for absent/disabled PHYSIS; validates enabled grid and climate and produces `InstallOptions`. `installFromMap(Simulation&, Json map)` explicitly installs PHYSIS during `GameModule::setup` (never implicitly from the editor). It honors `FloodMode::ExternalAuthority` when NEREID owns water and optional persistence switches without changing Stage 1–5 schema defaults.

```cpp
#include "engine/physis/PhysisProjectConfig.h"
// In ANY native GameModule::setup(Simulation& sim, Json rules, Json map):
aiverse::physis::installFromMap(sim, map);
// Return false for absent/disabled map["physis"]; true for installed.
```

`"physis"` config has `"grid": {"originX", "originZ", "width", "height", "cellSizeMeters", "seed"}`,
`"climate": {"baseTemperature", "baseHumidity"}`,
`"enabled"`, `"enableStorms"`, `"enableDisasters"`, `"enableWindZones"`, `"enableGravityFields"`, `"enableForces"` and `"floodMode": "LocalShallow"|"ExternalAuthority"`.

**Explicit limit:** authored arrays for specific wind/storm/gravity volumes remain JSON metadata and are **NOT automatically instantiated** by the core loader. Stable entity IDs, spawn scheduling and persistence/versioning require an additional host-owned adapter. No forced FPS/RTS module integration.

## Actual validation — no invented PASS

- GitHub connector source writes: **completed**, file paths and hashes reviewed.
- Merged main commit parents and tree SHA: **verified** using GitHub API; source skill catalog merged as parseable JSON.
- Local native checkout: **BLOCKED**, `git ls-remote https://github.com/chromealex/AIVERSE.git` returned `Could not resolve host: github.com`.
- **NOT RUN:** `aiverse_editor` native C++ build, `./run-tests.sh --filter '^physis_'`, `physis_authoring`, `physis_scene_geometry`, `physis_project_config`, Studio validation, PHYSIS CI pass, macOS graphics view, user interaction or screenshots. Tests are authored and registered but results not available.
- Static source inspection confirmed merged InputFlow link in CMake, PHYSIS test targets, editor Scene invocation, Inspector section and source files. **This does not prove compilability.**
- No NEREID water or TerraWeave branch was edited; renderer VFX is independent.

## Reproduce / next agent

```sh
# From a real AIVERSE checkout with the feature branch, at its latest head:
./run-tests.sh --filter '^physis_'

cmake -S . -B build/physis-editor -DCMAKE_BUILD_TYPE=Debug \
  -DAIVERSE_BUILD_EDITOR=ON -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_ENABLE_PHYSICS=ON
cmake --build build/physis-editor --target \
  aiverse_editor aiverse_physis_authoring_test \
  aiverse_physis_project_config_test aiverse_physis_scene_geometry_test -j4
ctest --test-dir build/physis-editor --output-on-failure \
  -R '^physis_(authoring|project_config|scene_geometry|editor_view|spatial_index|integration|box3d)$'
```

1. Build and fix **actual** compiler / linker / numerical / ImGui API failures, including InputFlow merged main behavior; record exact tested HEAD and command/log.
2. Open the macOS editor and verify PHYSIS menu, live-world guard, scene camera projection, window opacity/stride/radius, map config Inspector, invalid JSON prevention, two MapDoc formats and Ctrl+Z/redo; capture screenshots and performance traces. The current scene overlay is a projected ImGui overlay, not shaded volumetric weather.
3. Review project map persistence and optional native `installFromMap` integration in an unrelated generic project (no genre branches). Add a separate stable-ID field-volume importer only with explicit API versioning and server/rollback ownership.
4. Consider diagnostics-world-revision cache invalidation: same-tick edits can leave Scene overlay cached until manually refreshed, and world replacement might reuse memory address. Detect with host revisions when available.
5. Keep existing NEREID PR #23 authoritative for Water and TerraWeave PR #24 for terrain/foliage. Box3D remains optional physics owner; PHYSIS controls only nature/force fields. Stage 19 should be a **separate** handoff after any further implementation.
