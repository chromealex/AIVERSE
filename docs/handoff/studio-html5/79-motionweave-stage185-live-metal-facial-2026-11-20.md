# AIVERSE MotionWeave × HTML5 Studio — Stage 185: real 64-channel native Metal facial preview

**Date:** 2026-11-20
**Branch:** `integration/motionweave-studio-facial-stage184` (continuation after its Stage 184 code+handoff)
**Starting HEAD:** `de638b4c67492b327bddf074e341f1e49e8c0aff`
**Source HEAD before Stage 185 documentation:** `36e7b835d51e2fdb87f6852cfdb87104bbcd3614`
**Open draft PR:** https://github.com/chromealex/AIVERSE/pull/89 (base `feature/studio-html5`); PR #22 remains separate and Draft.
**Parallel source reviewed:** `feature/universal-ecs-animation-pipeline` at `1a11a21af8389b246983244b742cc8a65d9a2713`, Studio base continuously updated while the integration branch was being edited. No mass merge, rebase, force push, main modification or claim of mergeability.

## What actually changed in source

| Commit | Files | Feature |
|---|---|---|
| `96d7d752` | `engine/render/Model.h`, `ModelShaders.h`, `Renderer3DModels.cpp`, `ModelGltf.cpp`, `Renderer3D.h`, `Animation.cpp` | Targeted **64-channel** adaptation of existing Studio renderer instead of overwriting its very different 170KB `Renderer3DModels.cpp` with an older animation-branch copy. Native `kMaxMorphTargets=64`, `NodePose`/Node weights and full glTF `extras.targetNames`; GLSL+MSL U4 arrays now contain **64 sparse weights + 64 original channel IDs + 8 metadata floats = 136 floats (544 bytes)**; shaders loop only the active count (<=64); CPU packet generation never truncates expression channels. glTF 65+ targets, node/mesh weight counts and animated weight channels fail explicitly instead of silently taking first eight. |
| `db11bb30` | `Model.cpp`, `ModelGltf.cpp` | Preflight finite 64-channel morph payload size and malformed target layout before GPU allocation. Enforce source texture exact-float 2^24 RGBA32F texel ceiling and `AIVERSE_MORPH_TEXTURE_MIB=1..256` hard budget. Do not discard CPU morph deltas or publish an invalid texture after failed allocation; explicit glTF animated weight count guard. |
| `f2c72b73` | new `libraries/include/aiverselib/animation/FacialControlRig.h`, `FacialPreviewSession.h` | Port actual Stage 180/183 compiled facial Morph/IK runtime to the Studio integration branch. Replace upstream `ModelPoseRetarget.h` dependency with a small direct **read of real importer-authored Model mesh-local names**, while retaining the Stage 180 compiler, model reimport signature guards, per-actor half-life state, native `AnimationRig` eye/jaw IK and single-pose apply semantics. Public C++ namespace `aiverse::lib::animation` preserved. |
| `8eac8e9e` | new `apps/studio-native/mac/StudioFacialGpuPreview.{h,cpp}` | Native device-owner Model load, exact content-root file reads (no symlink segments, max 128 MiB per file), `FacialPreviewSession::configure` against REAL imported model, upload into existing gfx::Device, one `render::Animator` evaluating a parent pose, facial session morph/IK postprocessing, `worldMatrices` and `Renderer3D::drawModel`. |
| `c744a95b` | `StudioMetalScene.{h,cpp}`, `StudioSession.h`, `StudioFacialPreviewBinding.h`, `mac/Main.mm`, `Studio CMakeLists.txt` | Wire facial preview into the EXISTING native CAMetalLayer Scene viewport. StudioSession exposes binding to trusted owner; a native status callback marks `runtimeReady` only after the facial draw was submitted and `renderer_.render` returned. `runtimeVerified=false` remains until real pixel/backend QA. Native branch target links public `AIVERSE::libraries`. |
| `bee3a688` | native GPU preview cpp | Failed/partial candidate GPU uploads require `waitForResourceIdle()` before release; quarantine on failed completion proof. Preserve prior valid model generation on failed candidate import. |
| `a350a788` | `FacialRigPanel.tsx`, `types.ts`, `StudioSession.cpp` | Typed HTML5 input streaming with 75ms debounce, 700ms native runtime status poll, live Scene readiness presentation. Existing material sphere preview cleared on facial binding; UI uses no fake browser face. `runtimeReady` becomes boolean, `runtimeVerified` remains false. |
| `cc70ef5e` | GPU preview cpp, native Stage184 test | Correct typed IK `float[3]` -> `render::float3` translation at the native trust boundary; Stage184 channel count expectation now 64. |
| `e362f83a` | new `tests/StudioFacialRuntimeTests.cpp`, Studio CMake | NEW deferred C++ fixture: 52 named channels, real `FacialPreviewSession`, existing Animator + eye Aim IK, missing controls, invalid JSON retained last-good, reimport invalidation. CTest `studio_facial_runtime`. |
| `405339cc` | GPU preview/MetalScene/Main.mm | Cache failed import for exact selection revision instead of retrying at 60Hz; reset camera on failed render/transition; viewport hidden clears native `runtimeReady`. |
| `497b9075` | `StudioFacialRuntimeTests.cpp` | Add CPU-only *actual* glTF JSON+binary 52-channel importer fixture with ordered target names and high-index expression (51) sample; assert 65-channel import fails rather than truncating. Static JSON syntax parsed/reconstructed successfully in GitHub-source audit, but C++ test NOT RUN. |
| `36e7b835` | native GPU preview + GUID binding test | Detach stale animator if GPU retirement proof fails; extend trusted runtimeReady/QA-flag contract assertions. |

## Public API and execution path

Native HTML5 Studio writes Stage 181/184 `facial-rigs/<preset>.json` v1. The frontend uses the shared asset GUID picker, search, native revalidation, drag/drop and optional reference lock. Existing methods:

```ts
await invoke("studio.facialPreviewBind",{guid,rig:draft});
await invoke("studio.facialPreviewControls",{revision,values:{jawOpen:0.8},vectors:{lookAt:[2,1,3]}});
await invoke("studio.facialPreviewInfo",{});
```

These IPC methods never allow a browser-chosen local file path, GPU pointer or arbitrary shader code. The native owner reads the trusted GUID's `content/` asset and compiles the actual model/rig. For a visible Scene viewport, on the owner thread:

```text
StudioSession typed binding/inputs
    → StudioMetalScene::frame() on existing CAMetalLayer
      → StudioFacialGpuPreview::sync() (real native Model and GPU owner)
      → Animator::update/evaluate()   [ONE parent pose owner]
      → FacialPreviewSession::apply() [named morphs + existing IK]
      → worldMatrices()
      → Renderer3D::drawModel()
      → Renderer3D::render() / endFrame()
      → StudioSession::setFacialPreviewGpuStatus(renderSubmitted,error)
      → studio.facialPreviewInfo: runtimeReady, runtimeVerified=false, reason
```

Invalid source model, unknown channel, ambiguous node, missing IK bone, stale revision or failed GPU upload never produces a fabricated successful facial frame. The previous native GPU Model stays owned across failed revision attempts; current invalid selection is not rendered under a mismatched GUID. Explicit new Bind increments the revision and allows retry. Studio uses the same Model/Renderer3D graphics backend and 64-channel sparse shader format, not separate HTML geometry.

## Safety and known unfinished work

1. **Native compilation and shader compilation are NOT RUN.** The C++ code, WKWebView IPC and shaders have been committed but no Mac build/Metal pixels have been observed. A source-level feature is not a verified working preview. **Do not undraft or merge PR #89 or #22** based on this handoff.
2. This is a bounded direct Model owner with device-wide `waitForResourceIdle()` for replacement/failed upload and quarantine if GPU completion cannot be proven. Full Stage 174–177 `ModelAssetProvider` / `ModelReadLease` reference-counted residency and nonblocking GPU token retirement have **NOT** been transplanted into Studio. Keep model/pose pointers alive through synchronous Renderer3D rendering only. Add resource-epoch tracking before enabling high-volume preview hot reimport.
3. Real 52-channel **glTF/GLB** import and 64 sparse GLSL/MSL source have been adapted. The legacy **Studio FBX importer still does not import the Stage 168–170 named blendshape/in-between path**. Selecting a facial FBX lacking mesh morph semantics must fail at native compile with diagnostics, not be presented as a 52-channel success.
4. Stage 172/182 morph-aware LOD maps and index-only automatic morph LODs were not transplanted: these still need separate conflict-aware mesh/LOD migration and Metal/Vulkan/GLES crossfade tests. `AIVERSE_MORPH_TEXTURE_MIB` rejects budgets, but this preview does not page/compress facial VRAM.
5. Model loading and compile are currently synchronous in the macOS UI owner-frame boundary and may cause an import-time pause on large assets. Add background CPU decoding with GPU publication only on owner thread after a prepared Model/Scene resource lifecycle is integrated. A content file rewritten under an unchanged GUID needs explicit re-Bind; no automatic live model import epoch detection.
6. Scene viewport renders the selected actor when the **native Scene viewport is visible**; otherwise `runtimeReady=false`. Existing material preview can take precedence when explicitly reselected. No new ImGui editor, browser GPU face mock, extra Animator, or authoritative gameplay ECS write.
7. The same source U4 is shared for Metal MSL and GL/GLES/Vulkan GLSL, but **only source adaptation, no backend pixel or compiler parity proof**; existing backend-specific maximum texture/UBO limits and crowd thermal/VRAM performance remain unknown.

## Verification actually performed

**ACTUAL:** GitHub connected read/audit of active Studio and MotionWeave branch heads, Stage 170–185 relevant core files, Studio native Metal frame and AppKit WKWebView bridge, exact ModelGltf/ModelShaders/Renderer3DModels code sections, public FacialControlRig and JSON APIs, code commits with expected-HEAD update guard, source readback, and reconstruction+JSON.parse of the new 52-expression glTF fixture (52 target objects/52 unique names). PR #89 confirmed OPEN/DRAFT (at time of handoff its base had continued to advance). No local native AIVERSE source worktree/GPU is available in this environment.

**NOT RUN:** actual C++ compilation/link, CMake configure/build, `studio_facial_runtime` or `studio_facial_preview_binding` CTest, glTF binary decode at runtime, 52-channel Metal facial rendering, shader compiler, WKWebView interaction/screenshot, Vulkan/GLES builds/pixels, ASan/TSan/UBSan, GPU fence fault injection or VRAM/crowd measurements. **No PASS claim.** The new tests are authored, not executed.

## One coordinated native Mac acceptance (when feature closure reached)

```sh
git fetch origin
git switch integration/motionweave-studio-facial-stage184
cd apps/studio-web
npm install
npm run typecheck
npm run build
npm run test:facial
npm run test:assets
cd ../..
cmake -S . -B build/studio-stage185 -DAIVERSE_BUILD_STUDIO_HTML5=ON \
  -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_TOOLS=ON
cmake --build build/studio-stage185 --target aiverse_studio \
  aiverse_studio_facial_runtime_tests aiverse_studio_facial_preview_binding_tests
ctest --test-dir build/studio-stage185 --output-on-failure \
  -R '^(studio_facial_runtime|studio_facial_preview_binding|studio_facial_rig_document|studio_asset_catalog)$'
```

Then open an actual artist-imported glTF/GLB with *matching* semantic `extras.targetNames`, render 1/8/16/52/64 channels (and reject 65), verify eye/jaw IK, invalid asset/status, project switch, GPU failure, runtime frame pixels, Sponza/material/shadow regressions and actual Metal/Vulkan/GLES backend builds where supported; ASan/TSan/UBSan and 1k/10k/100k crowd p50/p95, memory/VRAM and thermal remain open. Do not mark tests PASS until real logs.

## Readiness matrix

| Feature | Stage 185 source state | Next |
|---|---|---|
| Universal MotionWeave Control Rig + session | PORTED in integration branch | Native compile and peer source reconciliation |
| Studio 64-slot sparse model/pixel shader U4 | SOURCE IMPLEMENTED | Metal/GLES/Vulkan compiler+pixel proof |
| glTF 52–64 named channels without first-8 truncation | SOURCE IMPLEMENTED | Real GLB/52-channel CTest and artist file |
| Studio actual native 3D facial Scene rendering | SOURCE IMPLEMENTED | Mac launch and screenshot QA |
| GUI live morph sliders, typed IK XYZ, GUID/lock | SOURCE IMPLEMENTED | Typecheck/native IPC interaction |
| GPU Model lifetime and project stale rejection | BOUNDED SOURCE IMPLEMENTED | Full ModelAssetProvider + ModelReadLease epoch/fence retirement |
| FBX blendshape channels / skinned-morph LOD | NOT INTEGRATED | Port Stage 168–172 safely |
| Main PR merge / real CI acceptance | NOT DONE | Conflict-aware latest Studio branch/PR #89 |

## Exact next stage 186

1. Read fresh latest `feature/studio-html5` (it is actively advancing), `feature/universal-ecs-animation-pipeline`, current PR #89 and these handoffs; resolve current divergence on a **new integration worktree/commit**, preserving parallel Studio HTML5 shell, Assets metadata, RHI/Metal and MotionWeave native code. Do not reset/drop peer changes or merge to main prematurely.
2. Wire the Stage 174–177 real `ModelAssetProvider`, `ModelReadLease`, CPU worker/immutable publication and GPU completion token retirement into the new Studio native facial-preview path. Port FBX named/in-between blendshape import and morph-aware authored/skinned LOD as separate bounded commits with individual agent handoffs.
3. Run the one-pass native Mac build/CTest/Metal/Vulkan/GLES/sanitizer/crowd/perf campaign after feature closure. Fix actual compiler/link/runtime problems before declaring `runtimeVerified=true` or merging Draft PR #89 / PR #22.

**Preserve the ONE pose owner and never introduce gameplay/genre dependency into the reusable MotionWeave layer.**
