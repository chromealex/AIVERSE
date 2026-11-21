# Stage 62 — Material shadow diffusion (2026-11-20)

Branch: feature/studio-html5. Code commit: 9e3f12ddbc09c31f0681f701ace9d396afce82fa.

Added the optional shadowDiffusion material key and Studio “Shadow diffusion (world units)” field, default 0, range 0–1, step .005. Existing shared PropertyField controls and au-input styling are reused; no new style language, bridge capability or browser-side project write. Native validation, preview and material library roundtrip support the property. Medium/High received sun shadows only. Existing material documents preserve their appearance; appended native Material member requires consumer rebuild, GPU uniform ABI unchanged.

Affected Studio paths: apps/studio-web/src/panels/materialSchema.ts and tests/materialSchema.test.mjs; apps/studio-native/StudioMaterialValidation.h, StudioPbrMaterialPreview.h, CMakeLists.txt and corresponding validation/preview tests. Shared renderer, loaders, material batching and docs are detailed in [Stage 4 shadow handoff](../../agent-handoffs/shadow-filter-noise-2026-11-20.md).

Executed: npm run typecheck, npm run build, npm run test:web PASS; final typecheck/build/test:materials PASS (7 tests). Python Studio contracts PASS 19/19. Root run-tests.sh focused five CTests PASS 5/5, including schema, native validation, preview/save roundtrip and batching. Full isolated aiverse_studio build PASS, app at /private/tmp/aiverse-shadow-noise-20261120/apps/studio-native/AiverseStudioHTML5.app. Metal/GL synthetic receiver captures PASS; full commands, reports and widths in the linked handoff. git diff --check PASS. No CI dispatched, URL/status N/A.

NOT RUN: interactive native Studio/WKWebView screenshot; CUA unavailable (native pipe path unavailable). Original user scene/performance and other GPU backends remain unverified. Existing running editor was not replaced. No claim of complete UI visual parity. Earlier independent terrain TAA sampler compilation blocker remains.

Next: launch isolated updated Studio, set fabric shadowDiffusion=.02 and stone=0, save/reopen, confirm field persistence and original-scene appearance; profile target GPU. No automatic material preset changes were made.
