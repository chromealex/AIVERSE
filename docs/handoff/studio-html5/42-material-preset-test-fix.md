# AIVERSE Studio HTML5 — Stage 42: Materials tests against real exported preset registry

Date: 2026-11-20
Branch: feature/studio-html5
Parent commit: 54e2df18a9d05ed714d56eaaf98eb5ddc70ec6a5
Commit: see branch HEAD.

## Change
- Corrected the material creation-preset test added in Stage 41. It now imports `MATERIAL_PRESETS` from the actual source module and asserts its values rather than comparing two hard-coded identical arrays.
- This keeps the frontend's accepted preset union aligned with the native StudioMaterialValidation whitelist. Future changes on either side must update tests explicitly.
- No engine/HTML markup/runtime behavior changed.

## Files
apps/studio-web/tests/materialSchema.test.mjs
docs/handoff/studio-html5/42-material-preset-test-fix.md

## Tests actually performed
- Local Node v22.16.0 tests on a reconstructed module: 6/6 PASS, including the corrected import-driven creation preset assertion.
- Local tsc v5.8.3 strict/noEmit on standalone materialSchema.ts: PASS.
- C++17 StudioMaterialValidation header/preset test compiled and ran locally on Linux with warnings as errors: PASS.
- Full React/Vite npm build, macOS app/Metal/CTests: NOT RUN; GitHub CI may remain queued.

## Next
Inspect GitHub CI run; test native material creation and patch from a real macOS Studio app, then work toward real GPU PBR preview, asset assignments and remaining ImGui window parity. Do not merge until reliable build verification.
