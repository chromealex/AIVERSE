# AIVERSE MotionWeave — Stage 172: morph-aware glTF visual LOD and source-pose parity

Date: 2026-11-20
Branch: `feature/universal-ecs-animation-pipeline`
Baseline: Stage 171 head `b5d44fb06beb82992093f84083c04bed7f465a96`
PR: https://github.com/chromealex/AIVERSE/pull/22 — leave DRAFT and UNMERGED.
Genre independence: View/Model/Graph/presentation ECS stay shared; do not put RTS/FPS/gameplay owner logic in the animation core.
Testing contract: finish source functionality first, then ONE coordinated Mac CMake, C++, CTest, shader, Metal/Vulkan/GLES, sanitizer, crowd and GPU retirement run. Handoff after EACH stage.

## Purpose

Stage 171 carried up to 64 expressions into the GPU but authored glTF mesh LODs still rejected all morph-bearing source/target meshes. Simply removing that rejection would corrupt faces when LOD meshes reorder or omit target channels. Stage 172 compiles a REAL semantic remap per source-to-target LOD edge, applies it in both LOD phases of crossfade, gives morph geometry conservative import-time bounds and adds an opt-in per-model morph VRAM budget.

## Source commits and changed files

1. `f4c99deceb0a91e84301a2e17beb3bd06c2cab7f` / `7c21e8c21ca8998c10ecab9a0afa3fbfa6b9c623` — `engine/render/Model.h`: add default-constructed `ModelLod::morphSourceForTarget`, an immutable target mesh-local morph slot -> original source slot mapping owned by the LOD asset. Keep existing `ModelLod{mesh,maxPixels}` calls working with default empty vector for legacy non-morph LOD.
2. `6a79e79f8a8ec1742b7ddf0ffc9c5f2375bf2138` — `engine/render/ModelGltf.cpp`: replace the blanket morph LOD rejection. For each authored LOD edge, compare source/target presence of morph data, require unique exporter-authored `extras.targetNames` on BOTH morph-bearing levels, validate each participating primitive's channel count, reject a visual target expression not found in LOD0, and produce the name-based remap **in target channel order**. Source can have more channels than a coarse LOD; no name/index guessing and no separate Graph writer. Existing skin binding and non-nested LOD checks remain.
3. `1a45ad13ecf33e58f93286e36fe15f9c77db3c84` — `engine/render/Renderer3DModels.cpp`: enable visual LOD of unskinned facial morph meshes in addition to existing eligible rigid and skinned meshes. For base/target crossfade and steady LOD, fetch each target channel weight from its mapped source `NodePose.w[]` and pack the TARGET channel texture index into Stage 171 sparse GPU draw palette. Retain one authored source pose/clock; no LOD graph tick, no duplicate child Graph player.
4. `5452a63daf4ad72f853fe779b4d42c114d959381` — `engine/render/ModelFbx.cpp`: necessary Stage 168 regression fix. After FBX face-corner dedup/palette splitting, populate `Primitive::firstVertex` and `vertexCount` from actual final appended Model vertex range. Stage 168's morph deltas and Stage 170/171's GPU morph upload preflight previously could reject correctly imported FBX models with zero `vertexCount`. This is source code fix, not proven native FBX import.
5. `8061045bd9709094a350cd329c1f46b35a7cf1c4` — `engine/render/Model.cpp`: during `Model::finalize`, build a conservative per-primitive morph geometry envelope by including all positive and negative POSITION deltas per source vertex/channel for normalized [0,1] weights. Reject bad delta dimensions/nonfinite values. Propagate to mesh and model bind bounds for source LOD selection/culling. The envelope is intentionally conservative for many independent expressions, not a tight per-frame bound.
6. `f23c86b3d0c33a6b15d41d6264c9f949c997b10c` and `875b62779d2c64aae7eb0455ce92bbcbaba1cc3b` — `engine/render/Renderer3DModels.cpp`: only select a morph LOD when current source weights are finite and in [0,1] (the range covered by the import-time envelope) AND every authored LOD has a complete, valid target->source remap. If a native host hand-authors a malformed or missing morph mapping, stay at LOD0 instead of silently rendering an unanimated/bind-expression target. Existing skinned-morph LOD fallback also checks the mapping.
6a. `7c5adc97c373f6d6ef806692fcf468e6c25c8f2f` — `engine/render/Renderer3DModels.cpp`: additional native-host malformed Model guard; reject a primitive morph source count exceeding `NodePose::w[64]` BEFORE reading dynamic weights at LOD selection. This avoids an out-of-bounds read when a hand-authored Model bypasses importer validation.
7. `4b040e56104c00d9f74f33ea9e2814cf081c6304` / `8d3270f1886f04b775474eff9768ca2895c8fcc9` — `engine/render/Model.cpp`: add the optional environment setting `AIVERSE_MORPH_TEXTURE_MIB` (integer 1–256). Fail an oversized imported Model before allocating a morph texture; never drop source channels to fit budget. Preserve Stage 171's global 2^24 exact-float-indexable RGBA32F texel bound; backend-specific texture dimension/resource limits may be smaller. The second commit corrects parsing of the terminal NUL character and actually enforces the user-supplied limit.
8. `40b2a5d5ec51502229f069ec95ec7655aabec529` — `engine/render/ModelGltf.cpp`: explicit C++ `<iterator>` for safe `std::distance` use.
9. `2596e8f34138f4841598917234e68e210570417d` — `engine/render/ModelGltf.cpp`: support the genre-neutral `mesh.extras.aiverseLods` metadata key for both source skin propagation and authored LOD chain parsing. `extras.rtsLods` remains a backward-compatible alias only when the neutral key is absent. No RTS gameplay logic is added.
10. `4ef55a4b39fe7a3adf10aa514e54f904c9cbb82a` — new `docs/morph-aware-animation-lod.md`: user-facing glTF schema example (illustrative, not complete JSON geometry) and importer/runtime/budget rules.

## Runtime contract / example

```cpp
// Original source mesh: Smile, Blink, JawOpen.
// Authored visual target: Blink, Smile.
// Imported ModelLod::morphSourceForTarget becomes {1,0}.
// Parent source Graph/Animator calculates ONE pose:
//  sourcePose.w[0] = Smile, sourcePose.w[1] = Blink.
// When coarse LOD is selected, target weight 0 reads source 1,
// target weight 1 reads source 0. GPU shader fetches target morph
// texture slots 0 and 1, never reversed expression channels.
```

At crossfade, `ModelLodBlend::mesh` and `incoming` both use their respective maps. The parent's scene Entity/AnimationGraph remains unchanged; the authored LOD metadata is on the source Model asset, not stored in serialized gameplay ECS. Retire imported LOD and source CPU/GPU Model under the same provider/fence ownership contract.

Authoring example and opt-in GPU budget: `docs/morph-aware-animation-lod.md`.

## Important constraints and honest limitations

- Only **authored** glTF visual mesh LODs are morph aware; `AutoLodRuntime.cpp` deliberately continues to skip morph/deformable meshes. There is no automatic morph-target-preserving mesh decimator, retopology or LOD generation.
- Each morph-bearing source/target mesh MUST have real, unambiguous, ordered `extras.targetNames`. Every target expression must exist on LOD0; missing target expressions fail import, rather than making an unsafe index assumption. Source and target both have morph data. Target may intentionally omit low-value source expressions.
- Skinned meshes retain their existing skin palette safety checks. Existing culling/LOD bounds are conservative only for normalized morph weights in [0,1]. If an editor/Graph emits negative or >1 weights, LOD falls back to source LOD0 but generic bounds/culling outside the normalized range still need an explicit authoring policy.
- The import-time expanded envelope can overestimate face deformation when 52+ targets are present; high density/crowd culling and LOD timing must be profiled. The GPU sparse palette still has up to 64 weights/draw with no arbitrary pruning.
- `AIVERSE_MORPH_TEXTURE_MIB` is a configurable HARD asset rejection threshold, NOT dynamic VRAM paging/residency or real backend capability query. A device may reject textures of unsupported dimensions even within the configured budget.
- Model/Renderer3D source compiled against one C++ branch; no concurrent peer scene branch merge or FPS/RTS-specific APIs. SceneLoadCoordinator activation and full token-safe nonblocking `ModelAssetProvider::shutdown` remain NOT IMPLEMENTED.
- FBX morph `firstVertex/vertexCount` correction has **not** been verified on real artist FBX, palette-split meshes or actual GPU frames.
- Stage 172 does not implement morph-aware mesh material batching/MDI paths (they remain protected by existing deformable/morph exclusions), and does not claim GPU/Metal/Vulkan/GLES pixel parity.

## Verification performed

**ACTUAL:** GitHub remote source audit of `Model.h`, `ModelGltf.cpp`, `ModelFbx.cpp`, `Renderer3DModels.cpp`, `Model.cpp`, `ModelLodTransition.h`, `AutoLodRuntime.cpp`; SHA-guarded GitHub code commits; readback checks for `morphSourceForTarget`, the generic authoring key, renderer remap, normalized-weight fallback, FBX vertex span, and VRAM guard; public user guide committed. No local native Mac compiler/device used.

**NOT RUN:** CMake configure, native C++ compiler/link (-Werror), CTest, real glTF/GLB and FBX fixtures, Metal/Vulkan/GLES GPU shader compilation and pixels, ASan/TSan/UBSan, GPU cache/retirement/fault tests, VRAM/RSS and large-crowd benchmarks. This is **SOURCE IMPLEMENTED AND UNVERIFIED**, not a tested release or completed animation program.

## Final acceptance cases

1. Morph source `[Smile,Blink,JawOpen]`; target `[Blink,Smile]` and `[Smile]`: verify exact weights and morph geometry in steady LOD and crossfade, with parent animation clock unchanged, for 1/8/16/52/64 channel rigs.
2. Missing/duplicate names, target-only expression, source/target morph presence mismatch, inconsistent per-primitive counts and stale manually authored remaps: reject authoring or stay at LOD0, no misordered pose.
3. Source/target skeletal skin binding, joint palette splits, shadow/colour passes and normal maps; Metal/Vulkan/GLES screenshot and shader parity; no double Graph writer or invalid ModelReadLease.
4. Morph envelope [0,1] worst-case deformations and negative/>1 fallback; verify geometry visibility/culling and screen-size LOD thresholds.
5. FBX vertex-span regression on real skinned and split geometry; LOD0 morph GPU upload, morph texture indexing, device idle/rollback safety.
6. `AIVERSE_MORPH_TEXTURE_MIB=1,16,32,64,128,256`; malformed values and over-budget rejection. Device max-size behavior, VRAM peaks and no silent channel truncation.
7. Large-crowd 1k/10k/100k model submissions with many/partial facial expressions; CPU p50/p95, RHI uniform uploads, GPU morph fetches, memory/VRAM and shader variants.

## Readiness matrix

| Subsystem | Source status | Outstanding |
|---|---|---|
| Universal AnimationGraph / ECS / independent child sources | Stage 161–164 source | Native compilation & playback tests |
| Playback/parameter continuity on Graph reimport | Stage 163–164 source | Real assets, fault tests |
| Generic rigs/semantic morph/64-channel sparse GPU | Stage 165–171 source | Complex rigs, backend parity and perf |
| **Morph-aware authored glTF LOD and crossfade** | **Stage 172 source implemented** | Native visuals and LOD/culling regression |
| **Conservative morph bounds and GPU budget** | **Stage 172 source implemented** | Real resource/memory benchmarks |
| **FBX morph vertex-span regression fix** | **Stage 172 source implemented** | Native palette-split FBX/GPU regression |
| Morph-preserving automatic LOD generation | NOT IMPLEMENTED | Deformable mesh simplification/retopology |
| Full facial/IK authoring and 64+ channels | PARTIAL | Advanced avatar/editor workflows |
| SceneLoadCoordinator integration | NOT IMPLEMENTED | Peer branch review |
| Token-safe complete GPU provider retirement | NOT IMPLEMENTED | Shared cache/rollback/fences |
| Full CMake/CTest/Metal/Vulkan/GLES/sanitizers/perf | NOT RUN | Single coordinated final Mac acceptance |

## Next Stage 173

Address remaining animation-source feature correctness (runtime LOD/skeleton/morph revisions and morph authoring diagnostics), then review and adapt the genuine peer SceneLoadCoordinator source/scene life-cycle (no blind branch merge), and safely complete GPU Model/texture provider token retirement only after auditing the graphics fence/rollback owners. When feature closure is reached, run the user's ONE full native Mac acceptance suite; do not merge PR #22 or claim green before logs. Create standalone handoff for every implementation stage with source commit, changed files, actual tests and next step.
