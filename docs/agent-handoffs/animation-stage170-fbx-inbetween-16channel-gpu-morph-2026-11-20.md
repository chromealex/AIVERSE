# AIVERSE MotionWeave — Stage 170 handoff: FBX in-between shapes + 16-channel GPU morph

Date: 2026-11-20
Branch: `feature/universal-ecs-animation-pipeline`
Previous Stage 169 head: `4ebd3376ca2aa86b7804f6f1ce2544b6cf96e49a`
Draft PR: https://github.com/chromealex/AIVERSE/pull/22 — confirmed OPEN, DRAFT, UNMERGED.
One-writer rule: animation core, retarget and render are genre-neutral; parent and child Graph player ownership remains unchanged.

## User goal

Stage 170 follows the Stage 169 FBX animated morph weight importer. Increase the complete CPU/GPU morph path beyond eight channels and support FBX in-between key shapes, not just blindly raising an importer constant. Continue feature closure first, then run one coordinated native Mac compiler/CTest/Metal/Vulkan/GLES/sanitizer/performance suite. Write an independent agent handoff after every stage with code/commit IDs, API, exact verification status and next step.

## Source commits and changed files

1. `2b2bd0580bdbdddcc2ec82c503bb5e7416fe086e` — `engine/render/Model.h`: `kMaxMorphTargets = 16`, propagates to `Node::weights[16]`, `NodePose::w[16]` and generic morph array-dependent readers. A higher limit changes in-memory structs; do not assume prior binary ABI compatibility.
2. `956f6567a998f866971dd189cfc081755d77a4e9` — `engine/render/ModelShaders.h`: GLSL/std140 `U4.u_morphW[4]` plus `u_morphInfo`/`u_morphInfo2`, Metal `MU4.w[4]` matching layout, both GLSL and MSL vertex morph loops use 16 slots. OpenGL ES and Vulkan consume the common GLSL source; Metal uses MSL source. The actual GPU backends still need compilation and pixel parity tests.
3. `467ce30b00c5500b88ce06ad92b6693776098276` and `c94350000fa612c24e41ca43df16e603944e5025` — `engine/render/Renderer3DModels.cpp`: upload `float mo[kMaxMorphTargets+8]` (16 weights and 8 metadata floats), with metadata offsets based on the shared constant. Added `static_assert(kMaxMorphTargets==16)` to fail the host compiler if GPU shader/CPU layout capacity becomes inconsistent.
4. `807c7cf42829d42bbb8c60fba8fcf9f60c13c7e5` — `engine/render/Renderer3D.h`: correct ModelItem morphData offset comment.
5. `c27cd43f6aef659a11986d78cd00122459065539` — `engine/render/ModelGltf.cpp`: allow up to 16 real glTF targets and matching `extras.targetNames`; reject more than 16 instead of silently truncating morph data and changing named expressions.
6. `996f316a5ba3f0ccf32850bc996a1ef14c3815d0` — `engine/render/ModelFbx.cpp`: `appendFbxShapeBasis` projects the FBX channel's current weight into one or two weights for adjacent native in-between shape targets, starting from zero bind shape and clamping beyond the final target. Multi-key FBX channels flatten each real `ufbx_blend_keyframe.shape` to a GPU morph slot; reject missing shape, nonascending/nonfinite key weights, insufficient capacity, duplicate/empty authored names and `offset_weights` (not yet supported). For one-key channels preserve the original FBX channel name; for several keys use the compound `channel::shapeName` from TWO genuine authored FBX identifiers. Semantic compounds do not guarantee cross-exporter name parity and should not be confused with an authored glTF channel.
7. `18219df385db703192c041b6e7fc04237573854a` — `engine/render/ModelFbx.cpp`: update Stage 169 `addMorphWeightChannels` to emit `AnimChannel::Weights` for the flattened in-between slots, matching the new geometry slots, both for defaults and sampled FBX animation layers. Validate shape order against imported names and reject too many slots. `Node::weights` is populated by the same projection helper and thus agrees with each clip's output.
8. `af433ed3ebf0a0a3485045f026723ae38be0a8db` — `engine/render/ModelFbx.cpp`: update explicit importer capability and unsupported-feature comments.
9. `77f2088557f445c08b7c9d50d14827baa921d000` — `engine/render/Model.cpp`: preflight each morph primitive before any new GPU allocation; fail for >16 channels, negative channels, empty/malformed deltas, invalid vertex count or delta size overflow. Already-resident GPU morph textures with retired CPU deltas remain accepted for subsequent operations.

## Existing runtime API

No new public Model/Animator function is necessary. `render::loadModel("face.fbx",read)` yields `Model::meshes[].morphTargetNames`, `Primitive::morphTargets` / `morphDeltas`, `Node::weights[16]`, and native `AnimClip` channels with `AnimChannel::Weights`. The GPU renderer takes `ModelDraw::pose` and passes matching 16 weights + metadata to the shaders.

For a source FBX channel with targets at weights `w1 < w2`, a value `w` in `[w1,w2]` contributes `(w2-w)/(w2-w1)` to target 1 and `(w-w1)/(w2-w1)` to target 2. Before `w1`, it interpolates from the bind pose to target 1. After the final target, target-last is held at full strength. This is a piecewise-linear *shape* representation. Source weight curves are sampled via the Stage 169 ufbx evaluator, not reimplemented from raw FBX curve tangents.

## Important correctness and performance boundaries

- **Capacity is sixteen per mesh**, not unlimited and not yet a full 52-channel ARKit/MetaHuman facial rig. `>16` imported FBX/glTF is rejected, never truncated. A future >16 solution should review dynamic sparse activation, channel pages/compute morph, CPU and GPU format, LOD and material batches together rather than raise this fixed U4 again blindly.
- The FBX in-between mathematical representation uses linear blends of authored shape offsets, which must be validated against ufbx on real exporter FBX files (especially FBX weight units and nonunit keyframe targets). It is SOURCE IMPLEMENTATION ONLY; no claim of verified exact ufbx parity.
- `ufbx_blend_shape::offset_weights` (per-control-point scalar) is explicitly rejected if present. Missing in-between shape names and ambiguous keys are rejected; no fake semantic identifiers are invented.
- Stage 169's 30Hz resampling remains approximate for steep/step changes between samples; source key times are also included where baked by ufbx.
- More channels mean more CPU `NodePose` storage, GPU `morphTex` texels, 96-byte U4 instead of 64-byte U4, and up to 16 vertex-shader texture fetch pairs per morphing vertex. Crowd/GLES hardware budgets remain unmeasured.
- glTF built-in mesh `extras.rtsLods` still rejects morph-bearing source/target meshes. Stage 167's independent Model-to-Model named semantic retarget is separate; do not claim complete built-in morph LOD support.
- Model GPU upload failures still require the host to prove device completion before ModelAssetProvider shutdown. Per-resource token-complete GPU retirement is NOT implemented.
- Genre-neutral scene activation hooks with peer `SceneLoadCoordinator` branches are NOT integrated here.
- Full body/facial IK, many-channel morph facial authoring and editor visual workflow are still PARTIAL.

## Actual verification vs deferred tests

**ACTUAL PERFORMED:** connected GitHub source audit of `Model.h`, `ModelGltf.cpp`, `ModelFbx.cpp`, `ModelShaders.h` GLSL+MSL, `Renderer3DModels.cpp`, `Model.cpp`, animation sampling/LOD headers, vendored `third_party/ufbx/ufbx.h` and public ufbx blend evaluation API; SHA-guarded GitHub source commits; readback of shader U4 counts, CPU offsets, Model morph capacity and FBX flatten code. Confirmed PR #22 remains draft and unmerged.

**NOT RUN:** native CMake configure, C++ compile/link, CTest, running ModelView/Studio, real FBX/glTF files with nine–sixteen channels, Metal/Vulkan/GLES GPU pixels, static shader compilation, ASan/TSan/UBSan, GPU fault injection, crowd/VRAM benchmark, and actual ufbx in-between deformation reference parity. No CI-green or production-ready claim. The user requested a single coordinated full native Mac acceptance once source feature work closes.

## Required final test matrix

1. 1, 8, 9, 16 and rejected 17-channel glTF/FBX imported rigs; CPU `NodePose.w`, GPU U4 member alignment and Metal/GLSL pixel parity; shader variants with skinning, shadows, materials and different vertex ID offsets.
2. FBX one-key and multi-key in-between: shape coefficient sum/values at below-first, exact-first, midpoint, exact-next, after-last; compare deformed vertex positions and normals to ufbx reference across nonunit weights and unusual scene unit transforms.
3. FBX skinned mesh material and palette split + de-duplication, independent mesh instances, channels with absent/duplicate shape name, duplicate/descending weights, more than sixteen targets and optional `offset_weights` rejected safely.
4. GLTF `extras.targetNames` with channel 15, animated `Weights` through ModelView, copied source-to-visual semantic LOD plan, monotonic Model publication epochs and failed transactional reimport.
5. Renderer3D/Model upload with malformed length/negative/beyond-capacity GPU morph data: no draw, no out-of-bounds shader read, no premature GPU release.
6. Sparse/complex crowd profiles at 1k/10k/100k animated entities on Mac; CPU median/p95, morph texture upload size, GPU bandwidth, memory/VRAM; repeat on GLES/Vulkan device builds when available.

## Readiness matrix

| Subsystem | Source status | Outstanding |
|---|---|---|
| Generic ECS AnimationGraph + independent child graph clocks | Stage 161–164 integrated source | native CMake/runtime |
| Graph hot reload/parameters and restore telemetry | Stage 163–164 source | real asset continuity tests |
| Rig/retarget and semantic morph/LOD | Stage 165–167 source | complex real rigs + perf |
| FBX geometry and animated shape weights | Stage 168–169 source | native FBX validation |
| **16-channel CPU/GPU morph contract** | **Stage 170 source implemented** | all shader/backend & upload tests |
| **FBX in-between targets to GPU slots** | **Stage 170 source implemented** | ufbx reference/artist file parity |
| More than 16 simultaneous facial channels | NOT IMPLEMENTED | dynamic sparse paging/compute or new format |
| Built-in glTF morph LOD chain | NOT IMPLEMENTED | mesh/skin/GPU parity |
| Full facial/IK authoring | PARTIAL | advanced rig/editor integration |
| SceneLoadCoordinator | NOT INTEGRATED | review peer branches |
| Token-safe Model provider GPU retirement | NOT IMPLEMENTED | texture/cache/rollback/fence |
| Full native CMake/CTest/Metal/Vulkan/GLES/sanitizers/perf | NOT RUN | single final native Mac suite |

## Next Stage 171

Design and implement >16 virtual facial channels / large-rig morph streaming and shared shader/per-frame budget (at least 52 for ARKit-like rigs) without silent truncation, or finish glTF morph LOD and skinning parity first. Then complete scene lifecycle hooks and per-resource GPU lifetime. Maintain the one-writer rule, do not merge peer branches blindly, and create another independent handoff with exact commits/tests/next steps before handing control to the next agent.
