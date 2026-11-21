# AIVERSE MotionWeave — Stage 171: 64-channel facial morphs with sparse GPU palette

Date: 2026-11-20
Branch: `feature/universal-ecs-animation-pipeline`
Baseline: Stage 170 head `fa18021ad7fe1a6bf6b3f57d2d525bec140abf0e`
PR #22: https://github.com/chromealex/AIVERSE/pull/22 — remains DRAFT / NOT MERGED
User requirement: a reusable, genre-neutral animation system for all game genres; complete features before one coordinated native Mac CMake/CTest/Metal/Vulkan/GLES/sanitizer/bench suite; **one standalone handoff after each implementation stage**.

## Delivered source

1. `4e3a15dece962a8600ab199268589cab22b02899` — `engine/render/Model.h`: `kMaxMorphTargets=64`. `Model::Node::weights`, `NodePose::w`, source/visual retarget arrays and general named channels inherit the capacity. This covers 52 ARKit-style named expressions with twelve spare slots, **not** a complete ARKit/MetaHuman facial authoring system.
2. `01e0f0db4f9ef64d0f223ef17da85dc6efa2ebf5` — `engine/render/ModelShaders.h`: shared GLSL/Vulkan/GLES `U4` and Metal `MU4` now have 16 vec4 weights, 16 vec4 *original channel indices*, then two vec4 metadata blocks. Both vertex deformers iterate over the **actual active count**, with a maximum of 64, and use the provided original channel index to read the correct morph deltas from tex8. Morphing still happens before skinning in both normal/shadow passes. U4 is 136 floats (544 bytes). This is NOT shader-compiler or pixel-validated.
3. `a96429614b52256a70d8f0ab3d5da714a9b09e89` — `engine/render/Renderer3DModels.cpp`: for every valid pose and mesh primitive, preflight all original morph weights for finite values; compact **all nonzero** weights into the per-frame morph buffer with their exact original source-channel IDs and active count. No top-N pruning or truncation even when all 64 are active. Upload the 64+64+8 float layout exactly matching GLSL/MSL; `static_assert(kMaxMorphTargets==64)`. Invalid (NaN/Inf) dynamic weights fail closed by omitting the morph packet, rather than letting nonfinite values poison GPU positions. Per-draw CPU scan is O(number of authored channels), GPU morph reads scale with active channels rather than maximum channel count.
4. `494b780aad767b2f84b55f48309471bccf0f78fc` — `engine/render/ModelGltf.cpp`: update importer contract text for 64 channels. `28df6f89c4e14992735a711981520e8742b67462`: glTF node/mesh weight vectors and animation weight-channel widths that exceed 64 explicitly FAIL instead of being silently truncated. Existing glTF target/name limit inherits `kMaxMorphTargets`; the whole 64-channel glTF mesh can be imported but memory/perf and GPU parity need real tests.
5. `e2725916cd47ec63ee360edf6afe9be9dbc87e35` — `engine/render/ModelFbx.cpp`: 64-channel FBX/in-between source-limit documentation. Actual FBX channel flattening, per-shape basis and 30 Hz animation baking originate from Stages 168–170 and now inherit the 64-channel core limit.
6. `7660a051a8080d8c109a4dbbc83f243c7f732848` — `engine/render/Renderer3D.h`: document new sparse per-frame morph item offset.
7. `dee6c06fc2c8ec9456680d2744e007ae5274cde8` — `engine/render/Model.cpp`: protect the existing float-based morph texel uniform `u_morphInfo.x`. A float stores integer offsets exactly only through 2^24; cap the total RGBA32F morph texture at 2^24 texels (2048 × 8192, at most 256 MiB), failing instead of silently indexing incorrect deltas. GPU devices may enforce *lower* texture limits; actual RHI texture capability introspection does not exist in this API and GPU creation can still decline.
8. `4850f5e481ba0c76aa21c7627f4fa6a4c2b946ea` — `engine/render/Animation.cpp`: tie `kMaxComps` directly to `kMaxMorphTargets`, preventing future silent width divergence in `AnimChannel::Weights` sampling.

## Architecture and API

No new public playback API. Existing `loadModel(path,read)` imports up to 64 channels; `AnimationGraph` and `NodePose` handle them; the renderer assembles:

```
U4 layout (std140 and MSL, 136 floats):
  [0..63]    = compact nonzero weights (padded with zeros)
  [64..127]  = original source morph channel IDs (padded with zeros)
  [128..131] = morphTexel base, primitive firstVertex, vertexCount, total authored channels
  [132..135] = morph texture width, nonzero active count, unused, unused
```

The shader only loops `k < activeCount` (bounded 64), loads `sourceChannel = IDs[k]` and samples two RGBA32F texels from `base + (sourceChannel * vertexCount + localVertex) * 2`. No lossy active-channel selection, no arbitrary semantic reassignment. This is deliberately a **single draw** sparse indexed palette, not mesh channel paging; GPU work can still be expensive when all 64 are active.

## Confirmed limitations and risks

- **64 is a hard capacity**, not unlimited virtual channels. A 65+ target mesh is rejected. ARKit 52 expression channels fit, but ARKit-to-rig controls, face-tracking ingestion, semantic validation, visemes, facial GUI and editor curves are NOT completed by this Stage.
- U4 grows from Stage 170's 96 bytes to **544 bytes/draw**. NodePose, Model::Node and compiled retarget plan sizes also grow. GPU shader compilers, UBO alignment, all variants, GLES capability and CPU/GPU throughput must be measured before release. A sparse palette avoids unnecessary vertex morph fetches but still scans the original CPU channels per actor.
- Existing glTF embedded mesh LOD chains continue to reject source/target morph meshes. Stage 166–167 *independent Model-to-Model* semantic morph retarget is not equivalent to built-in morph mesh LOD support.
- FBX in-between sample semantics, per-control-point morph weights, geometry conversion and >64 channels remain limitations. FBX weight interpolation is baked/sampled (Stage 169), not exact for every subframe/tangent.
- The shared `gfx::DeviceInfo` currently does not expose per-device maximum texture dimension/UBO size; GPU may decline oversized 2048×H texture even before this 256 MiB texture cap. A memory/budgeted morph LOD strategy is future work.
- `ModelAssetProvider` full token-safe nonblocking GPU retirement, texture-cache ownership, scene loader activation hooks, complete facial IK/Control Rig authoring, Metal/Vulkan/GLES backend parity and large-crowd performance remain open.
- Changes are intentionally **source-only**; never claim native compilation or GPU success without logs.

## Source audit and tests actually run

**ACTUAL:** connected GitHub readback of Model, Animation.cpp/AnimationGraph, ModelGltf, ModelFbx, Renderer3DModels, ModelShaders GLSL/MSL, Model.cpp upload and the Stage 170 handoff. SHA-guarded source commits. Reviewed layout agreement of `kMaxMorphTargets=64`, U4 `16×vec4 + 16×vec4 + 2×vec4` and float-indexed morph texture. No actual native build occurred.

**NOT RUN:** CMake, C++ compiler/link with `-Werror`, CTest, actual glTF/FBX import, Metal shader compilation, Vulkan/GLES runtime, skinned morph pixel correctness, ASan/TSan/UBSan, GPU fence tests, MDI/morph batch parity, VRAM/crowd benchmarks. No CI-green or feature-frozen claim.

## Final acceptance test plan

1. Import and animate 0/1/8/16/17/52/64 channels; reject 65. Validate NodePose weights, morph target names, FBX in-betweens and glTF animation channels.
2. Confirm sparse palettes preserve original source channel indices for source IDs 0, 1, 15, 16, 51, 52 and 63; 0/1/52/64 active channels and NaN/Inf failure. Compare rendered pixels to dense CPU reference.
3. Compile GLSL ES3, Vulkan GLSL/SPIR-V and Metal MSL shaders with skinning, shadows, transparency, normal mapping and motion/baked variants. Verify exact 544-byte U4 layout and no UBO/slot conflicts.
4. Exercise large morph delta textures up to and over the 2^24 texel float-index boundary; upload rejection and GPU completion-safe rollback.
5. Bench representative 52-channel facial rigs across near-camera and crowd LODs (1k/10k/100k actors), CPU scan/UBO uploads, GPU vertex fetches, frame median/p95, bandwidth/RSS/VRAM and thermal mobile limits.
6. Verify no double AnimationGraph writer and compare graph hot-reimport parameters/phase, on-device attachments, and scene/prefab restore after backend changes.

## Readiness

| Subsystem | Source status | Missing |
|---|---|---|
| Generic ECS MotionWeave + AnimationGraph | Implemented incrementally | native acceptance suite |
| Independent child Graph/SceneAttachmentHost | Stage 161–164 source | native runtime, real graphics |
| Graph hot-reimport playback/parameters | Stage 163–164 source | continuity regression |
| Skeleton/semantic retarget + importer morph names | Stage 165–167 source | production rigs/crowd |
| FBX geometry/weights/in-between | Stage 168–170 source | artist FBX parity |
| **64-channel morph CPU and GPU pathway** | **Stage 171 source implemented** | strict compiler + GPU backend and crowd tests |
| **Sparse per-draw morph palette** | **Stage 171 source implemented** | real-world GPU perf |
| More than 64 facial channels | NOT IMPLEMENTED | dynamic paging/compute pipeline if required |
| Built-in glTF morph mesh LOD chains | NOT IMPLEMENTED | full mesh/skin/target identity parity |
| Full facial IK/Control Rig authoring | PARTIAL | editor and artist workflow |
| Authoritative scene-loader adapter | NOT INTEGRATED | peer branch contract review |
| Token-complete GPU ModelAssetProvider retirement | NOT IMPLEMENTED | shared texture/rollback/fence proofs |
| CMake/CTest/backend/sanitizer/perf | NOT RUN | final coordinated Mac pass |

## Next Stage 172

Prioritize true morph-aware glTF mesh LOD and complex facial authoring with memory budgets; review SceneLoadCoordinator integration only after peer branch reconciliation and close GPU retirement with actual RHI fences. Preserve one-writer rule. After each source stage, append a handoff and a visible readiness table, and keep PR #22 DRAFT until native acceptance.
