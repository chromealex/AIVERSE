# AIVERSE Studio HTML5 — Stage 40: Materials library PBR editor

Date: 2026-11-20
Branch: feature/studio-html5
Parent commit: 7386b7bc55a0212a43828282532d9d040e00c8ea
Stage commit: see HEAD

## Implemented
- Replaced the Materials placeholder with a real HTML5 library-browser and subset PBR editor.
- Typed, safe materialSchema.ts defines all 15 C++-allowlisted keys; existing Studio PropertyField handles numeric bounds, enums, booleans and error feedback.
- Local draft edits are separate from material library state. Apply & save explicitly sends one safe materialPatch; no automatic per-field disk writes.
- Switching materials with a dirty draft is blocked, with a Discard & switch flow.
- The UI clearly warns that material_set saves materials.json immediately and is not part of MapDoc Undo.
- Shows real authored color swatches from the material's baseColor array as 2D approximations, **not real PBR or GPU rendered preview**. Supports filtering, loading/empty/error and DEMO read-only states.
- Native host emits document.changed after successful materialPatch; lighting/editor panels can refresh without creating new engine operations.
- Added Node.js 22 material schema tests and frontend CI script integration.

## Files
apps/studio-web/src/panels/materialSchema.ts
apps/studio-web/tests/materialSchema.test.mjs
apps/studio-web/src/panels/MaterialsPanel.tsx
apps/studio-web/src/panels/MaterialsPanel.css
apps/studio-web/src/types.ts
apps/studio-web/src/mock.ts
apps/studio-web/src/panels/StudioPanelRenderer.tsx
apps/studio-native/mac/Main.mm
apps/studio-web/package.json
docs/handoff/studio-html5/40-html5-materials-panel.md

## Tests actually performed
- Pure materialSchema.ts Node.js v22.16.0 tests 5/5 PASS in a reconstructed local fixture.
- TypeScript v5.8.3 strict/noEmit standalone materialSchema.ts PASS.
- Native StudioMaterialValidation C++17 compiled/tested on Linux PASS (Stage 39).
- Full React/Vite dependency resolution and macOS WKWebView/Metal compilation: NOT RUN; GitHub CI previously queued.

## Limits and next
- No arbitrary shader graph/texture path edits, material creation/delete, material_apply to nodes or full PBR GPU preview. Material swatches represent only flat authored color.
- MaterialPatch is a disk write; no Undo. Backups/versioned material document transactions remain needed.
- Current MaterialOps may reload models on project events but Studio.materials reads only materials.json for low overhead.
- Native GPU Scene still uses proxy meshes. Complete SceneWorld material/model parity later.
- Run macOS CI and frontend integration, fix all compiler/type failures, then add basic material preview and safe asset assignment as separate stages.
