# AIVERSE HTML5 Studio — Stage 55: visual smoke test and no-project Scene state

Date: 2026-11-20
Branch: feature/studio-html5
Parent commit: 98846cd3abb74b98d94c556e9c61cdbaff9669e0
Stage commit: see branch history.

## User-provided visual smoke result
The developer launched the native AiverseStudioHTML5.app on macOS after the Stage 53/54 WKWebView bundled-scheme fixes and supplied a screenshot showing:
- AIVERSE Studio HTML5 React shell with the project header, toolbar and nested dock tabs.
- "Native transport connected" status.
- Native 3D terrain/grid image inside the Scene panel, consistent with a running Metal Renderer3D.
- Hierarchy, Assets tab, Inspector, Lighting tab, Console and editor status bar visible.
- "No project opened" displayed in the header; the Hierarchy nevertheless showed an internal "New map" and Scene rendered an empty default terrain.

This establishes initial UI, bridge and native Scene rendering **visually work**. It does NOT establish that project loading, actual ECS scene identity, editor changes, docking gestures, PBR material previews or Shader Graph authoring work correctly.

## Implemented
- Added a pure UI mode guard `studioSceneMode(nativeConnected, projectPath)`: native+no project -> welcome; native+project -> renderer; browser-only -> demo.
- When no project is open, Scene now shows an actionable "Open Project" welcome state instead of mounting `ViewportPlaceholder` and reporting an internal MapDoc grid as a real project scene.
- Connected the welcome CTA to the existing native `studio.openProject` AppKit file picker; the opened scene uses the same Metal viewport bridge and receives layout updates via ResizeObserver.
- Hierarchy label changes to "No project" in this state.
- On the native rendering thread, StudioHost.tick returns before rendering if `Workspace.project.dir` is empty (belt-and-suspenders against a native phantom map).
- Added three Node 22 unit tests for scene presentation (welcome, real native project, browser demo), registered as `npm run test:scene` within `npm run test:web`.

## Files
apps/studio-web/src/panels/sceneState.ts
apps/studio-web/tests/sceneState.test.mjs
apps/studio-web/src/App.tsx
apps/studio-web/src/panels/StudioPanelRenderer.tsx
apps/studio-web/src/styles.css
apps/studio-web/package.json
apps/studio-native/mac/Main.mm
docs/handoff/studio-html5/55-visual-smoke-and-empty-project.md

## Validation actually performed
- Developer screenshot: React shell VISIBLE; native bridge CONNECTED; rendered 3D Scene VISIBLE; inspector and dock panels VISIBLE.
- Previous developer run: full native CMake build and 14/14 studio CTests passed before runtime loader changes.
- Stage 55 frontend/native source committed. On Node.js v22.16.0, reconstructed exact standalone `sceneState.ts` logic and `sceneState.test.mjs` passed **3/3 tests** (no failures). Standalone `tsc v5.8.3 --noEmit --strict --target ES2022 --module ESNext --moduleResolution Bundler --lib ES2022,DOM` passed.
- Full React/Vite application rebuild, actual macOS visual QA of the new welcome screen, and end-to-end project-opening flow are NOT confirmed in this turn.
- Do not conflate a rendered grid with a successfully opened project.

## Remaining and next
1. On Mac: pull branch, run `bash tools/studio/verify_mac.sh`, launch the bundle and click Open Project.
2. Check project scene object listing, selection -> Inspector, native camera controls, nested dock movements, editor save/undo, Lighting, Materials/PBR preview and Shader Graph.
3. Capture exact visual symptoms or logs of any failures, then record a Stage 56 handoff; do not remove ImGui or merge to main before feature parity.
