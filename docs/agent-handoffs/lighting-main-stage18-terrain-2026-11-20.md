# Lighting Stage 18 — terrain pixel acceptance

2026-11-20, branch `integration/lighting-main-stage1-20261120`.
Code `35b665a917effad7286dc94b8ad13f641b17226c`.

Added an asset-free receiver using actual RtsTerrainShaders, 36-byte production
vertices, real U0/U6 binding and custom opaque draw. Metal checks full terrain
GI slot21 at High and Low; desktop GL checks the compact low terrain slot8.
This validates compact shader behavior on desktop GL, NOT GLES hardware support.
Six RGB cases: enabled, next-view no attachment, low quality, receiver disabled,
lights removed, lights restored. CPU reference expected radiance is nonzero for
positive controls. Texture lifetime is drained only after proven GPU idle.
No renderer/shader/save/default changes in this stage; fixture/CMake only.

Actually executed on KINGSTON (same build/TMPDIR as Stage15):
- Built aiverse_hdr_scene and aiverse_hdr_gl, PASS.
- run-tests.sh --no-build --build-dir <KINGSTON root>/build --filter '^lighting_terrain_gi_'
  2/2 tests PASS, 6/6 RGB on each backend. Report `20261120-143538-92696`.
- git diff --check PASS. No CI/push/main merge. Mobile/Vulkan GPU NOT RUN.

Next: native host configuration, immutable scene/light publication and per-view
binding; demonstrate the actual model-loading path with CPU capacity fallback.
