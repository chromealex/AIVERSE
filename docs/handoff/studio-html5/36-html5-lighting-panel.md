# AIVERSE Studio HTML5 — Stage 36: Lighting panel

Date: 2026-11-20
Branch: feature/studio-html5
Parent commit: f3ac92d0e242775d8b3f0fe5f6efff15ec4f3131
Stage commit: see branch HEAD.

## Implemented
- Added Lighting as a real tab in the shared nested dock, next to Inspector; metadata and default dock layout updated.
- Added React LightingPanel, backed only by the Stage 35 allowlisted native Studio bridge: read light list, set environment time/clouds/storm/dayLength/nightLevel, add map-center light, update name/type/position/height/radius/intensity/nightOnly/electric/bake and delete with explicit two-step confirmation.
- All editable fields are generated from typed lightingSchema.ts and rendered with the shared validated PropertyField from Stage 34.
- Added loading/empty/error/busy and browser-demo read-only states. Read-only DEMO never pretends to save to an engine.
- Main macOS bridge emits document.changed after successful native authoring mutation, keeping App dirty state and LightingPanel in sync while not forcing an asset-directory rescan.
- Added standalone Node 22 tests for lighting schema field allowlists, enums, bounds/default values and non-mutating metadata.

## Files
apps/studio-web/src/panels/lightingSchema.ts
apps/studio-web/src/panels/LightingPanel.tsx
apps/studio-web/src/panels/LightingPanel.css
apps/studio-web/tests/lightingSchema.test.mjs
apps/studio-web/src/types.ts
apps/studio-web/src/mock.ts
apps/studio-web/src/nestedDock.ts
apps/studio-web/tests/nested-dock.mjs
apps/studio-web/src/panels/StudioPanelRenderer.tsx
apps/studio-web/src/App.tsx
apps/studio-native/mac/Main.mm
apps/studio-web/package.json
docs/handoff/studio-html5/36-html5-lighting-panel.md

## Tests actually performed
- Node 22.16.0 standalone lightingSchema tests **PASS 6/6** after correcting the allowed-field order expected by the test.
- tsc 5.8.3 --strict --noEmit on lightingSchema.ts + propertyLogic.ts: PASS.
- Standalone C++ lighting validation test from Stage 35 PASS on Linux.
- Full React/Vite npm build, macOS native linking and real lighting renderer QA NOT RUN here. GitHub Actions may still be queued; do not claim deployed/verified lighting visuals.

## Limitations
- Lighting authored in open MapDoc, not realtime GPU appearance; native Scene preview still uses simplified geometry and does not reproduce all runtime light effects.
- UI does not expose every optional LightOps parameter (colors/spot angle/pattern), only a meaningful safe subset.
- Editing FPS lights depends on native game loader schema compatibility; macOS end-to-end test remains required.
- Selection uses light array index; array reorder from external operations may change selection.
- Existing ImGui remains production fallback.

## Next
Inspect GitHub Actions run, verify npm typecheck/build, run macOS CMake/CTest and visually test Light add/edit/delete/undo. Then port additional panels using the safe AI scaffolder and native Workspace operations. Handoff each stage.
