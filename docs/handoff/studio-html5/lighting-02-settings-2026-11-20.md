# Lighting Stage 26 — persistent Studio settings

2026-11-20, integration/lighting-main-stage1-20261120.
Code 72cac2dbe3b8458ec80ff2b463679f92c5949da7.
Shared RenderLightingSettings v1 validates modes and bounded numeric values.
Native studio.renderLighting/get and Set preserve RTS extra/FPS full documents,
validate before mutation, participate in Save/Undo/Redo. Typed bridge controls
use Studio UI v1. Existing maps default off/legacy; additive optional root
renderLighting block, no gameplay authority or GPU state serialized.

Actual checks with TMPDIR/build/bundle/logs on KINGSTON:
- cmake configure and build aiverse_studio + aiverse_studio_render_lighting_tests PASS.
- ctest -R '^studio_render_lighting$' PASS: real RTS/FPS invalid-patch atomicity,
  undo/redo/save/reopen/disk serialization. Initial fixtures lacked RTS types,
  FPS rules.fps and player; fixed fixtures to satisfy real host validation.
- npm --prefix apps/studio-web run build -- --outDir external studio-web-dist PASS
  (includes tsc --noEmit); initial mock generic type corrected.
- npm --prefix apps/studio-web run test:web PASS (57 tests).
- Python unittest discover tools/studio test_*.py with external bundle: 19/19 PASS.
- git diff --check PASS; CI N/A, no push. Native screenshots NOT RUN.

This stage is authoring persistence only. Viewport consumption comes NEXT;
controls must not be represented as completed live GI/Luma yet. Continue with
shared runtime ownership and PBR preview geometry, real backend status,
native pixels/screenshots, then ICB and remaining platform/performance gates.
