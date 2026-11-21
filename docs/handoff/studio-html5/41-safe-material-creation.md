# AIVERSE Studio HTML5 — Stage 41: safe material creation

Date: 2026-11-20
Branch: feature/studio-html5
Parent commit: 0395b5aba646cb43e41373f15c8b9929a408f0ce
Stage commit: see branch HEAD.

## Implemented
- Added allowlisted `studio.materialCreate` native request with name and preset only.
- Input material name limited to ASCII alphanumerics/underscore/hyphen up to 64 characters; preset must be matte, metal or glass. Existing library keys cannot be overwritten by Create.
- Native C++ constructs a validated, simple initial color/metallic/roughness/alpha material. Saved using existing MaterialOps via Workspace::call("material_set"), not arbitrary browser JSON or file paths.
- HTML5 Materials panel has a Create workflow (name + preset + Create and save), disables creation when an existing local material draft is uncommitted, and shows the immediate-save/no-Undo result.
- Updated MaterialPreset typing, browser DEMO read-only handling and native document.changed event.
- Extended pure C++17 material validation test and native Session no-project negative checks.

## Files
apps/studio-native/StudioMaterialValidation.h
apps/studio-native/StudioSession.cpp
apps/studio-native/tests/StudioMaterialValidationTests.cpp
apps/studio-native/tests/StudioSessionTests.cpp
apps/studio-web/src/types.ts
apps/studio-web/src/mock.ts
apps/studio-web/src/panels/materialSchema.ts
apps/studio-web/tests/materialSchema.test.mjs
apps/studio-web/src/panels/MaterialsPanel.tsx
apps/studio-web/src/panels/MaterialsPanel.css
apps/studio-native/mac/Main.mm
docs/handoff/studio-html5/41-safe-material-creation.md

## Tests
- Pure C++17 Material validation logic with name/PBR/key checks passed locally before this stage; stage-added preset assertions are authored but NOT run against complete repo checkout.
- Material schema standalone Node 22 tests 5/5 and strict tsc passed in reconstructed fixture before this change. Rerun after adding preset typing.
- Full React/Vite and macOS CMake/CTest STILL NOT RUN. GitHub Actions run queued in previous checks.

## Constraints
- Creation writes materials.json immediately and bypasses MapDoc Undo. UI explicitly states this.
- Only three safe presets; no arbitrary shader graph, texture path, file deletion or material apply to model nodes.
- No realistic PBR GPU preview yet. Color swatches are only authored-color visualization.
- Existing ImGui editor remains production fallback until functionality and visual regression tests pass.

## Next
Run full macOS CI, address compile errors; add genuine native Material preview using the existing render/material infrastructure, then model material assignments and shader graphs behind validated APIs. Keep documenting handoffs each stage.
