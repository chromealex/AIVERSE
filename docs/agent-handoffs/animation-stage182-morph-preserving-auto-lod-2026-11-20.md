# AIVERSE MotionWeave — Stage 182: automatic index-only morph-preserving LOD

**Date:** 2026-11-20  
**Branch:** `feature/universal-ecs-animation-pipeline`  
**Starting HEAD:** `51806d41b1ab1cfd261d56f01da7a811b98fcc27` (Stage 181 cross-branch checkpoint)  
**Source implementation:** `d6a631acaa641e3ca5abcd0553cfc81483fa8a2c`  
**CPU memory follow-up:** `5457b3ea6edd997697ce866acbd7ca13c52078e4`  
**PR:** https://github.com/chromealex/AIVERSE/pull/22 — **DRAFT; DO NOT MERGE** before native Mac acceptance.  
**Scope:** genre-neutral visual rendering and cosmetic animation only. No gameplay ECS, simulation tick, replay/checksum, RTS/FPS branching, alternate Graph evaluator or authoring UI.

## Delivered source changes

| File | Change |
|---|---|
| `engine/render/AutoLodRuntime.cpp` | Accept complete, semantic-name-bearing, unskinned morph meshes without authored LODs. Include **all** source target displacements (up to 64) as signed POSITION and NORMAL min/max envelopes alongside existing UV/tangent/color/lightmap attributes when invoking `meshopt_simplifyWithAttributes` (27 attributes total, under meshoptimizer's supported cap). Generate 1–3 lower-triangle **index streams** using the exact original vertex/morph indices. Never reduce or reorder the authored source channel set. |
| `engine/render/Model.h` | Add two opt-in `Primitive` fields, `morphAliasMesh` / `morphAliasPrimitive`, default `-1`; generated coarse mesh refers to the real LOD0 source primitive, not a second morph delta CPU/GPU allocation. No existing serialized Graph/Model JSON field is changed. |
| `engine/render/Model.cpp` | Preserve original source morph displacement envelopes in coarse bounds; validate aliases before GPU resource allocation; after the source RGBA32F upload assigns per-primitive texel offsets, copy **only the texel offset** to its generated alias. GPU texture uploaded once per source primitive, not once per LOD. |
| `tests/render/auto_lod_runtime.cpp` | Extend deferred CPU test with a real 52-channel in-memory mesh: exact LOD0 source delta immutability, identical semantic names/identity remap, alias to same GPU morph layout/vertex range, conservative bounds, source-only index references, no repeated generation, rejection of missing names and malformed delta payload. |
| `engine/render/AutoLodRuntime.cpp` follow-up | Avoid transient copying of huge CPU morph delta arrays into `MeshDef original` and every coarse `MeshDef`: use a stable source reference after reserving capacity and construct each coarse primitive by copying **metadata only**. |

### Contract and algorithm

- Existing `Model::batching.autoLod` and `AIVERSE_AUTOLOD=off` remain controls. Automatic LOD is import-time CPU work via `prepareModelUpload`, never an on-frame simplifier or a second pose source.
- Existing authored glTF LOD has priority. Eligibility requires unskinned, ordinary opaque material, no variants, valid triangle/vertex spans and complete unique real imported `morphTargetNames`. Missing/anonymous channels and malformed morph data do not receive guessed mapping. Procedural geometry and skinned models remain excluded.
- Per source vertex, accumulate negative and positive POSITION/NORMAL deltas across every authored morph target into twelve error attributes (six for position, six for normals). Meshoptimizer minimizes approximation against this conservative deformation envelope. The LOD reuses **original vertices**: there is no renumbering, channel truncation, authoring pose rewind or skin palette migration. Its `ModelLod::morphSourceForTarget` is the exact 0..N-1 identity mapping, compatible with Stage 172's steady LOD and crossfade.
- The generated coarse `Primitive` preserves `firstVertex`, `vertexCount`, `morphTargets` and source mesh-local names; it has no `morphDeltas` of its own. `Model::finalize` includes LOD0's conservative morph bounds. `uploadModel` rejects dangling/chained aliases, mismatched source spans/names/counts and sources lacking both CPU deltas and an already-resident morph texture.
- Each coarse draw references the existing Model-scoped RGBA32F source texels and one original parent pose. Separate GPU morph textures, material shader forks, or extra graph player instances are **not** introduced. Texture dimensions and memory budget remain governed by the existing `AIVERSE_MORPH_TEXTURE_MIB` source import policy.

## Actual verification versus pending

**ACTUALLY PERFORMED:** read the exact GitHub `Model.h`, `Model.cpp`, `AutoLodRuntime.cpp`, `Renderer3DModels.cpp`, Stage 172/180/181 handoffs and existing CPU AutoLOD test. Created two source commits on the feature branch via blob/tree/commit and expected-HEAD fast-forward checks; fetched implementation commit/file readback. No force push and no main/Studio branch modification.

**NOT RUN:** native CMake configure, compiler/link, `auto_lod_runtime` fixture, 52/64-channel real GLB/FBX source import and GPU rendering, Metal/Vulkan/GLES shader/pixel/crossfade parity, ASan/TSan/UBSan, high-channel memory peak, VRAM or crowd benchmarks, GPU retirement/fault injection. **Source implementation is UNVERIFIED by native execution.** No PASS claim.

Deferred on the user's native macOS clone:

```sh
cmake -S . -B build/ecs-test -DAIVERSE_RUNTIME_AUTOLOD=ON -DAIVERSE_BUILD_TESTS=ON
cmake --build build/ecs-test --target aiverse_auto_lod_tests
ctest --test-dir build/ecs-test --output-on-failure -R '^auto_lod_runtime$'
```

Then execute the broader Model/animation/Renderer3D/SceneHost suite and actual Metal/Vulkan/GLES build/frames once all feature changes are closed. Fix compiler errors and re-run before release.

## Readiness matrix

| Capability | Status | Remaining proof/work |
|---|---|---|
| Universal Graph/Montage/Retarget/IK/presentation ECS | Prior source implementation | Native tests, real assets |
| FacialControlRig 52 expressions + 64 source channels | Stage 180 SOURCE IMPLEMENTED | Real rigs, ModelView preview, GPU frames |
| HTML5 Studio facial JSON authoring | Stage 181 on separate `feature/studio-html5` | Integrate with actual runtime/3D preview |
| Authored semantic morph LOD and crossfade | Stage 172 SOURCE IMPLEMENTED | Real GPU parity and culling |
| **Automatic unskinned morph-aware triangle LOD, all source channels** | **Stage 182 SOURCE IMPLEMENTED** | Native compiler, 52/64 asset/pixel tests |
| **Original source morph texture shared by all generated LODs** | **Stage 182 SOURCE IMPLEMENTED** | GPU upload alias/VRAM validation |
| **No transient 52/64-channel CPU copies during LOD creation** | **Stage 182 SOURCE IMPLEMENTED** | Build and peak RSS profiling |
| Skinned morph automatic decimation | NOT IMPLEMENTED | Deformation-aware palette/skinning metrics |
| Actual LOD vertex/texture memory reduction | NOT IMPLEMENTED | Separate vertex remap and morph delta resampling; index-only saves triangles, **not vertex or VRAM count** |
| Native CMake/CTest, RHI Metal/Vulkan/GLES, sanitizers, crowd | **NOT RUN** | Coordinated Mac acceptance after feature closure |

## Exact next Stage 183

First run a narrow syntax/compile review of the source change on a real C++ toolchain and correct any compiler or runtime issues; even this Stage 182 fixture is **not executed** here. Then produce the missing **actual facial ModelView/Studio preview bridge** once `feature/studio-html5` can consume Stage 180's compiled FacialControlRig without a blind merge. The bridge must import named real mesh/bone channels, surface missing-target errors in the Studio facial tab, render through one native Graph/Animator and honor ModelAssetProvider GPU lifetime. Finish a separate skinned-morph geometry simplification / vertex-remap path only with measured deformation, runtime remap and GPU memory budget safety. After functionality is closed, run **one** comprehensive native Mac CMake/CTest/Metal/Vulkan/GLES/sanitizer/crowd/VRAM campaign. Never undraft or merge PR #22 solely on source inspection.

If either feature branch advances concurrently, re-read current HEAD and all peer handoffs before editing; preserve both branches.
