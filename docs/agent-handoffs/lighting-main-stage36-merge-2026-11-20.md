# Lighting Stage 36 — user-authorized main merge

2026-11-20, branch main. Merge implementation: 9611d05d5aaf299f0892b1fe2f60b935a6c5bede.
Parents: main 552bd0751a0bafb571d1b791d809eec2cdd16712 and lighting
3ae97cd98 (integration/lighting-main-stage1-20261120). User: “Вливай в мейн”.
Dedicated worktree .worktrees/lighting-main-merge; original root checkout and
its unrelated dirty Studio/water work were preserved.

## Resolution

Integrated all committed lighting/Studio/entity/skills/MCP stages. Retained
main's texture formats/VT, STRATA, LENS, atmosphere and compute/readback work.
Additive CMake targets, skills and handoff entries were combined. SpectraFX
preview fixes were equivalent; kept main formatting. No existing save formats
or defaults removed. Light entityId metadata and renderLighting v1 remain.

- Vulkan: reuse canonical TextureResources backend instead of parallel GI-only
  implementation. GI buffers12..15, image0, std140 UBO16; checked dispatch and
  explicit writable image binding. Corrected pre-existing VkDevice.h frame-slot
  CaptureFn to Device::CaptureFn (compiler failure during this merge build).
- Model VT/GI: VT retains U6/atlas/table, GI moves to U7 and a free texture after
  VT and optional alpha mask. Nonvirtual GI preserves tex13/U6. Over-budget
  combinations return explicit pipeline error. Graph/VT + GI uses ordinary
  draws; experimental plain-PBR ICB GI remains tested, flags default OFF.
- Preserve main shared PBR packing and include shadowDiffusion for all paths.
  Material/surface captures retain their shader-key bits alongside main VT bits.
- Terrain: preserve LENS immutable texture snapshots and staged pipeline owner;
  extend prepared variants/commit validation to 13 slots including GI. Emit
  literal GLSL sampler names to preserve Vulkan resource-binding translation.
- Clear borrowed GI atlas on discarded scene submissions.
- Added real Metal VT+GI receiver acceptance with gray virtual base color;
  checks both resources together against CPU indirect lighting and sRGB decode.
See docs/dynamic-gi.md for the combined public resource contract.

## Actually executed

All native builds, artifacts, logs, fixtures and temp:
/Volumes/KINGSTON/AIVERSE/Builds/.temp/lighting-main-merge-20261120
(build/ and logs/). Debug, macOS; no new Release run in this merge stage.

- cmake -S . -B <root>/build -DCMAKE_BUILD_TYPE=Debug
  -DAIVERSE_BUILD_EDITOR=ON -DAIVERSE_BUILD_TOOLS=ON -DAIVERSE_BUILD_TESTS=ON
  -DAIVERSE_BUILD_HDR_TESTS=ON -DAIVERSE_BUILD_RENDERBENCH=ON
  -DAIVERSE_BUILD_STUDIO_HTML5=ON -DAIVERSE_BUILD_CLIENT=OFF -DAIVERSE_VULKAN=ON
  -DAIVERSE_STUDIO_WEB_DIST=<lighting-completion-root>/studio-web-dist: PASS.
- cmake --build <root>/build --parallel 12 --target aiversemcp aiverse_studio
  aiverse_studio_render_lighting_tests aiverse_studio_metal_lighting_tests
  aiverse_hdr_scene aiverse_renderbench aiverse_lighting_vulkan_compute_shader
  aiverse_lighting_lighting_model_gi_shader_contract_test
  aiverse_lighting_lighting_terrain_gi_shader_contract_test: all PASS across
  build.log/build-retry.log/build-final.log. First failures: CaptureFn above,
  then newly moved material binding referenced mt before declaration; fixed.
- ctest -R '^studio_render_lighting$' --output-on-failure: 1/1 PASS, 3.25s.
- GPU filter studio_metal_lighting plus lighting_(dynamic_gi_gpu|compute_gi_gpu|
  icb_gi_gpu|icb_compute_gi_gpu|terrain_gi_gpu|graph_gi_gpu|luma_gpu|
  virtual_material_gi_gpu): initial 8/9 PASS. New VT fixture initially expected
  linear 0.5 instead of sRGB-decoded base color. Corrected analytical reference,
  rebuilt hdr_scene and reran '^lighting_virtual_material_gi_gpu$': 1/1 PASS
  4.81s. All nine now pass, but not one fresh combined green run.
- ctest -R '^lighting_(vulkan_compute_shader|lighting_model_gi_shader_contract|
  lighting_terrain_gi_shader_contract)$': 3/3 PASS, 2.17s.
- ctest -R '^lighting_(native_host|luma_host|animated_host)$': 3/3 PASS, 13.75s.
- python3 tests/editor/scene_entity_mcp.py --binary <root>/build/aiversemcp
  --output <root>/mcp-fixtures: actual stdio RTS+FPS create/components/history/
  validation/save/reopen PASS.
- npm run test:web: 57/57 PASS; npm run typecheck PASS;
  npm run build -- --outDir <root>/studio-web-dist PASS.
- python3 -m unittest discover -s tools/studio -p 'test_*.py': 19/19 PASS,
  AIVERSE_STUDIO_WEB_DIST set to previously built bundle. TMPDIR on KINGSTON.
- tools/skills/catalog.py generate/check: 234 skills PASS; audit.py 0 errors/
  warnings; tests/skills/catalog_test.py PASS; git diff --check PASS.

No current CI run or push yet. Full repository suite, Release rerun, Vulkan
hardware/Android NOT RUN (no available device/loader), not claimed green.
Earlier Stage34 visual UI evidence remains; no new UI layout changes here.
Next action: incorporate concurrently advanced origin/main (currently 7d7bd7c99),
verify its overlap, then push main without force and confirm remote ancestry.
