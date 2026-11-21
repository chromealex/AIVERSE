# Lighting Stage 30 — FPS bounds and material status

2026-11-20, integration/lighting-main-stage1-20261120.
Code: 7f0bbd633228d4dc27248f4da82d670044a05c42.

StudioLightingPreview now derives FPS ground and model bounds from authored
proxy geometry, including negative coordinates, instead of the unrelated
MapDoc 64x48 RTS default. Empty FPS preview has an origin-centered floor.
StudioMetalScene uses that model even with GI disabled, frames its actual
bounds, excludes the RTS grid, and reports material preview GI as off.
RTS behavior and renderLighting v1 save/public API remain unchanged.
No full game-asset parity is claimed for this terrain/proxy preview.

Actually executed, all artifacts/builds on KINGSTON under
AIVERSE/Builds/.temp/lighting-completion-20261120:
- Debug aiverse_studio + aiverse_studio_metal_lighting_tests build PASS.
- Debug studio_metal_lighting/render_lighting/authored_lights/document_visuals:
  4/4 PASS, stage30-tests.log. Actual Metal test verifies negative-coordinate
  FPS GPU convergence, Luma activation, center-screen picking into authored
  scene, material-preview status and return-to-scene. Existing RTS pixels and
  CPU/GPU modes still pass.
- Release arm64 same targets build PASS; studio_metal_lighting 1/1 PASS,
  stage30-release-build.log and stage30-release-tests.log.
- Stage29 full quick completed: 313/316 PASS, three catalog tests initially
  failed. After committed metadata fix CTest skill_catalog/routing/audit 3/3
  PASS (stage29-catalog-retest.log). Main engine test PASS in 840.79s; full
  suite 998.66s. This is full baseline plus focused reruns, not one clean run.
- git diff --check PASS. CI N/A; no push.

Remaining external gates: locked Mac prevents interactive HTML/native UI
screenshots; user confirms no Android/Vulkan stand. Those are NOT RUN.
Next: isolated Release arm64 Sponza OFF/ON/OFF benchmark and final evidence
consolidation. Existing realtime-animation/convergence and preview limitations
remain; ICB experimental defaults stay OFF.
