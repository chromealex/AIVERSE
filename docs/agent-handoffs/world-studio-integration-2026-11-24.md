# World processes + HTML5 Studio integration — 2026-11-24

- Integration branch: `codex/world-studio-merge-20261124` (to be pushed to `feature/world-processes-stage31-bidirectional-handles`).
- Merge commit: `1d15eee54`, parents `0bd191dcb` (world processes) and `c8d70487e` (Studio HTML5).
- Requested order: merge `origin/feature/studio-html5` into the world processes branch. `main` was not changed.

## Reconciliation

- Preserved the world-process diagnostics, planetary snapshots, drag tools, and scalar-distance simulation LOD policy alongside Studio's game review, runtime editing, asset history, and Caverns brush API.
- Combined `StudioHost` document-changing method registrations and game lifecycle resets. Restart uses Studio's fresh launch plan and refreshes world-process snapshot identity.
- Combined `StudioSession` linked-component whole-value validation with `MapDoc::CompoundEdit` so paste/reset is one undoable, rollback-safe transaction.
- Combined all web `StudioMethod`, parameter and result types. Registered new panels in the default dock tree and updated its behavioral tests.
- Merged separate `terraweave`, `world`, and `studio` roadmap changes in `docs/devlog/systems.json`.
- Corrected the existing `PlanetSeaVolumeComponent` construction that prevented syntax compilation of the integrated Studio session.

## Verification actually run

- `python3.12 tools/site/build.py check`: 48 entries, 0 errors.
- `npm run typecheck && npm run build` in `apps/studio-web`: passed; Vite reported a chunk-size warning.
- `npm run test:web`: passed after updating the dock registry and its behavioral tests.
- `python3 -m unittest tools.studio.test_world_explorer tools.studio.test_world_persistence_panel tools.studio.test_entity_debugger`: 7 tests passed.
- `c++ -std=c++20 -w -fsyntax-only -I . -I libraries/include -I third_party/lua apps/studio-native/StudioHost.cpp apps/studio-native/StudioSession.cpp apps/studio-native/StudioScene.cpp apps/studio-native/StudioGameSurface.cpp`: passed. This does not verify linking or runtime behavior.
- `tools/studio/verify_headless.sh`: failed in Python Studio contract tests (8 failures, 5 errors), including old build configuration, model reference/zoom and scene-tool source assertions. Its later Node/C++ checks were not reached.

## Remaining gates

The full native Studio build, linked tests, macOS Metal/WebView interaction, gameplay regression, and world-process native acceptance were **NOT RUN**. Do not infer main-merge readiness from the passing syntax and web checks. Run the consolidated native gate on KINGSTON, inspect the failing static contracts against both source branches, and exercise Studio game restart, world diagnostics, Caverns brush, runtime edits, history, and planetary forest views before any `main` integration.
