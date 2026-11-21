# Stage 100 — «Ветер» panel: hierarchical wind masks preview (2026-11-21)

Branch `agent-wind-masks` (not feature/studio-html5; MAIN agent merges).
Code: `f0afc486c` (Studio/editor), engine `1839583bf`, `414a568f8`.

- Native: `studio.wind` / `studio.windSet` in StudioSession
  (`apps/studio-native/StudioWindPreview.h`). Session-only preview: speed,
  heading (0=+X, 90=+Z), gusts, amplitude, debug view
  off|parts|depth|weight|skeleton. Atomic validation; no MapDoc mutation,
  no file writes. Result includes the project mask library
  (`wind/masks.json`), profiles (`wind/profiles.json`) and scene rig stats.
- StudioMetalScene: `Renderer3D::setSceneWind` + `setWindRigDebugView`
  each frame, skeleton lines for rigged instances, reload on
  `<model>.wind.json` change.
- Web: generated `WindPanel` (Studio UI v1 components/tokens), typed bridge,
  mock, default right dock tab; nested-dock fixtures updated.

Executed (Linux VM): `tsc --noEmit` PASS; `npm run test:web` all suites PASS
(26+8+6+10+8+…, 0 fail) after fixture update; Python UI contracts
`test_design_system`, `test_new_panel`, `test_content_layout`,
`test_web_bundle` (1 skipped), `test_migration_audit`, `test_build_config`
PASS; `StudioSession.cpp`, `StudioSessionTests.cpp`, `StudioMetalScene.cpp`
g++ syntax check (no new errors vs HEAD). Headless check of
StudioWindPreview.h PASS.

NOT RUN: vite build (mac node_modules: rollup native binary for darwin
only), native `aiverse_studio`, `studio_session` CTest, macOS panel/scene
visual QA, Metal shader compile of the wind graphs.

Next: build on the Mac, open a project with a rigged model
(`<model>.wind.json`), check sway, the four debug views and skeleton lines;
scene-only screenshots.
