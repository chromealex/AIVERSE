# Main integration — Digital Humans (VIVID) + MotionWeave Studio facial — 2026-11-21

- Target branch: `feature/aiv20261121_main_digital_humans` (worktree `/home/claude/wt_dh`), created from
  `feature/aiv20261121_main_animation` @ `1944e0fae` (= main `2d034dd6b` + MotionWeave animation merge, itself Mac-pending).
- Source: `origin/integration/motionweave-studio-facial-stage184` @ `9c5b44e845bd27d9983e380c9f75360f67984c07`
  (contains `feature/vivid-digital-humans`; latest source handoff
  `docs/agent-handoffs/motionweave-vivid-integration-2026-11-20.md`, VIVID log `docs/agent-handoffs/vivid.md`).
- Merge commit: `9aa56dde3f21fc0ce54d8447b6b6bb15b9a10f3b` (criss-cross: bases `93c8a0cf2`, animation stage 154 `6bf45b99d`).
- Fix commits: `a67eafdae` (compile fixes), `d0027e264` (Studio facial preview lighting isolation).
- Not pushed.

## Conflict resolutions (46 files)
Per-file rationale is in the merge commit message. Summary:
- Animation library headers (AssetLoader, BudgetPolicy, FacialControlRig (add/add), HumanoidRetarget, Scheduler): ours —
  main's stages 155–183 are a superset of the Studio branch's ports (none of the 300 animation commits after stage 154
  exist in stage184; stage184 re-ported them).
- Model/material data: union of main's SSS/thin-transmission/shadow-diffusion fields and VIVID's `humanProfile`/`eyeOptics`
  in `Model.h`, material keys (`GeometryScene.cpp`, `ModelMerge.cpp`) and `GpuMaterialState.h` hashes.
- `Renderer3DModels.cpp`: U2 packing unified — `uv1.y` = shading id 0..4 (Pbr/Unlit/Skin/Eye/Hair), `uv1.zw` = SSS (Pbr) /
  fixedShade (Unlit) / humanProfile (human); regular draws upload 10 rows (`ModelMaterialUniforms`, eyeOptics), the
  experimental per-command Metal ICB keeps its 9-row pack (256-byte stride, tenth row reads zero) and now skips
  vertex-override and human-shading items. VIVID `customVertexStream` / tracked-submission guards were applied to main's
  newer packet-cache, LOD, shadow-batch, material-batch and sparse-morph paths. LENS packet tap stays first in drawModel.
- `ModelShaders.h`: eye parallax UV + main's virtual alpha mask; Luma surface/material side channels now discard only
  Unlit (`uv1.y==1`) so skin/eye/hair stay in Luma (before: any `uv1.y>0.5` was discarded).
- RHI: both readback families (main: depth/colour/texture/buffer-range + fences; VIVID: one-pixel colour/depth) in Gfx.h,
  GL, Metal, Vulkan. GL: removed VIVID duplicate `frameActive_`/`inPass_` members and its `endFrame` early
  `frameActive_=false` (it would have disabled main's GL GPU fences). Vulkan: kept main's `frameSerial()`.
- Studio native: main's PRISMA/lighting/document-model scene + VIVID facial GPU preview, cyclorama and FXAA PostFx
  (used for facial and material preview; map keeps PRISMA `post_`). `StudioMetalScene::frame(..., projectPath, rules,
  const StudioFacialPreviewBinding* = nullptr)`. CMake: VIVID sources/links added to `aiverse_studio`, all
  StudioSession test targets and the Metal lighting test.
- Studio web: union of panels/methods (`runtime`, `nereid_flow`, `human_appearance`); main's dock-layout fixtures.
- Root CMake: main's lighting tests + VIVID Vulkan/GLES sculpt-ID, wardrobe, face-shape tests.
- AGENTS.md / metal-batching handoff: ours text kept, theirs-only sections appended (VIVID section at the end; VIVID's
  older build-output paragraph kept under §4 marked as superseded by the KINGSTON rule).

## Verified here (Linux container; no CMake/Ninja builds allowed)
- `g++ -std=c++20 -fsyntax-only -Wall -Wextra` (+ `-DAIVERSE_WITH_VULKAN=1`, includes per CMake) on all 69 C++ sources
  changed by the merge and every conflicted .cpp: no errors after the fix commit. Remaining warnings are pre-existing
  style (`-Wmisleading-indentation` in one-line headers on both sides; VIVID `-Wrange-loop-construct` in
  GroomSegmentCollision.h, `-Wtype-limits` in HumanImport.cpp). Not checkable here: PlatformAndroid (NDK),
  `tools/vivid-dna-import` (external DNACalib), `tools/vivid-creator/main.cpp` (CMake define), web/Windows-loader GL.
- Standalone g++ build+run: `vivid_face` PASS, `vivid_sparse_morph` PASS, `vivid_shape_binding` PASS.
- Studio web node tests (no node_modules needed): all 14 files, 92/92 PASS. `tsc --noEmit`: NOT RUN (no node_modules).
- `python3 -m unittest discover -s tools/studio`: 41 tests OK (1 skipped). py_compile of changed Python: OK.
- `tools/skills/catalog.py check`: OK. `tools/readme/check.py`: 0 problems. `tools/skills/audit.py --strict`: 1 error
  (`skills/editor/studio-ui/SKILL.md` NN-* path) — pre-existing on both parents.

## UNVERIFIED (must be covered on the KINGSTON Mac)
- Any CMake configure/build, link (StudioSession tests now link vivid_human+modeling), ctest.
- All Metal / Objective-C++: GfxMetal.mm readbacks, Studio `Main.mm`, `StudioMetalScene.cpp` facial+map switching, MSL shaders
  (eye parallax + Luma discard change + 10-row MU2), ICB per-command U2 with the 10-row MU2 struct.
- Vulkan/GLES runtime of the pixel readback paths; WebGL `PlatformWeb.cpp`.
- VIVID `ModelVertexStreamOverride` combined with main's packet cache/LENS packet tap (tap records overridden draws).
- Not merged: 9 commits of `origin/feature/vivid-human-safe-publication` (`bb55053a6..2e0d6740a`, Creator atomic last-good
  GPU publication, DNA save test, validate-script ref). They conflict with stage184's later Creator work in
  `tools/vivid-creator/main.cpp`, `tests/vivid/human_creator.cpp`, `docs/vivid.md` — needs the VIVID owner to reconcile.

## Mac verification
Build on KINGSTON (`/Volumes/KINGSTON/AIVERSE-builds/main-digital-humans/...`), targets at least `aiverse_studio`,
`aiverse_vivid_creator`, `aiverse_vivid_renderer_preview`, all test targets, then:

```
ctest -R '^(vivid_|studio_|animation_|motionweave_|auto_lod_runtime$|model_asset_provider_api$|model_batching_policy$|render_(async_model_cpu_queue|model_image_transaction|device_epoch|gpu_program_retirement)$|lens_|lighting_|prisma_lens_water$)' --output-on-failure
```
plus the full quick suite (`./run-tests.sh --quick`), the Creator `--smoke` captures, a Studio facial ↔ map switch
screenshot, and an RTS/FPS/editor launch smoke. New test targets from this feature: `vivid_*` (see CMake),
`studio_human_appearance_session`, `studio_facial_gpu`, `studio_facial_runtime`, `studio_facial_preview_binding`.
