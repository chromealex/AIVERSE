# Lighting Stage 28 — Metal dynamic GI ICB ABI

2026-11-20, integration/lighting-main-stage1-20261120.
Code 8b2d945d4a04ab6d891bd837ee7acf2616a5605d.
ICB PBR argument buffer conditionally owns dynamicGI texture13 at argument26;
GiDynamicU remains inherited uniformU6/buffer7. Metal CPU/GPU ICB owners bind
and declare the atlas as an indirect fragment reader, without requiring an
unused sampler. Remove GI blanket GPU-authored fallback; other graph/deformation/
baked-GI eligibility checks remain. Ordinary PBR/graph sampler ABI unchanged.
All experimental ICB flags stay OFF by default. No performance claim or general
Metal ICB production acceptance implied by this lighting fixture.

Actual validation on KINGSTON:
- Debug aiverse_hdr_scene/gl built PASS (initial test used wrong ModelDraw field
  transform; corrected to world).
- run-tests --no-build --build-dir <KINGSTON>/lighting-completion-20261120/build
  --filter '^lighting_(icb.*|dynamic_gi_gpu|dynamic_gi_gl|compute_gi_gpu|graph_gi_gpu|graph_gi_gl|terrain_gi_gpu|terrain_gi_gl|luma_gpu)$':
  10/10 PASS, report20261120-154538-96530.
- Both new ICB tests assert actual gpuEncodedSubmissions>0 and commands>=2,
  plus six analytic pixel cases (attach, detach, lowquality, AO, light removal,
  restoration). CPU and GPU transport providers both PASS. Ordinary Metal/GL
  PBR/graph/terrain and Luma pixels PASS. Experimental flags confined to tests.
- diff --check PASS, CI N/A/no push. Release/other hardware NOT RUN this stage.

Next: full quick native suite and editor/client builds, Releasearm64 current
source and Sponza timings, platform gates. UI screenshot remains blocked by
locked Mac; user asked asynchronously to unlock. Do not mark all complete yet.
