# AIVERSE MotionWeave — Stage 169: FBX animated morph/Blendshape weights

Date: 2026-11-20
Branch: `feature/universal-ecs-animation-pipeline`
Parent handoff: [Stage 168](animation-stage168-fbx-simple-blendshapes-2026-11-20.md)
Draft PR: https://github.com/chromealex/AIVERSE/pull/22 — remains DRAFT / unmerged.

## User contract

Animation remains genre-neutral. Continue closing source features; the user wants ONE coordinated native Mac CMake/C++/CTest/Metal/Vulkan/GLES/sanitizer/perf pass after source functionality is complete, rather than a full run every stage. After EACH stage add an independent agent handoff with modified files, APIs, actual verification, limitations, current commit and next steps.

## Actual implementation

1. Source commit `7abce9cd9710da4a0192aa2efbafd344ab690186` — `engine/render/ModelFbx.cpp`.
   - Added `FbxReader::addMorphWeightChannels(AnimClip&,const ufbx_anim_stack&,const ufbx_baked_anim&)`. It visits the same `ufbx_blend_deformer`/channel order already imported in Stage 168 `MeshDef::morphTargetNames`, and rejects a mismatch. The Stage 168 geometry only supports ONE shape keyframe/channel and at most `kMaxMorphTargets` (8); these restrictions still apply.
   - Uses `ufbx_evaluate_blend_weight(stack.anim,channel,nativeTime)` for blended, animated FBX layer/curve evaluation, rather than trying to implement FBX cubic interpolation in engine runtime. Values are normalized against the supported single target weight (same convention as Stage 168 defaults), clamped to [0,1], and rejected if nonfinite.
   - Generates a common channel-value timeline: 30 Hz samples across the ufbx-baked playback duration, plus exact baked-property key times associated with the relevant blend channels (using the same trim-start-time convention as existing TRS baking). Deduplicates timestamps via `std::set<float>`, caps duration at 1800s and sample count at 65,536.
   - Emits one `render::AnimChannel::Weights` for each original visual node instancing the FBX mesh, containing all channels in the same order as the imported mesh morph texture; `comps` is the real number of morph channels and `values` are interleaved per keyframe. No channel is emitted if all sampled values equal the node's authored morph defaults.
   - Existing `sampleClip` and MotionWeave consume `AnimChannel::Weights` without any genre-specific code or a second graph writer. Existing TRS channel baking, clip duration/sourceStart/sourceFps and GPU Model/texture ownership remain unchanged. The loader still warns about other unsupported property animations while no longer labeling recognized blend weight properties as ignored.
2. Source commit `1e1b5bd99bc247bae877bda5a384e2bbe0b3b1ef` — `engine/render/ModelFbx.cpp`: exit before building a sampled timeline for models without any FBX Blendshape deformers; avoid unnecessary memory/time and avoid applying the 1800s morph-only duration bound to unrelated animation-only FBX models.

## API and data contract

No new public API. `render::loadModel("actor.fbx", reader)` returns `Model` with existing `MeshDef::morphTargetNames`, default `Node::weights` and imported clips containing `AnimChannel{path=Weights,comps=channelCount,times,values}`. The channel index matches Stage 168 `Primitive::morphDeltas` and is usable with Stage 167 semantic morph retarget source-to-visual authoring.

A single engine `AnimChannel::Weights` addresses a visual node, not an FBX blend deformer element. All FBX channels on a mesh are sampled on the same timeline. The same source mesh attached to multiple visual nodes creates independent node-targeted channels, respecting different authoring default weights where they exist.

## Explicit limitations / correctness risks

- This is **baked, sampled animation**, not an exact reproduction of arbitrary FBX cubic interpolation at every subframe. ufbx evaluates the curve at sample points; the engine interpolates linearly between them. For stepped/nonlinear curves, adding ufbx property key boundaries improves fidelity but does not prove exact behavior near step discontinuities.
- In-between/multi-keyframe shape interpolation remains UNSUPPORTED and fails import in Stage 168. More than eight simultaneous morph channels also fails. Animated weight curves are now represented only for supported single-target shapes.
- If the ufbx bake duration and native stack time origin diverge for unusual FBX time modes, `stack.time_begin + trimmedLocalTime` needs fixture-based confirmation. No real artist files were executed in this chat.
- Ufbx's evaluated channel full-weight convention vs the single keyframe's `target_weight` needs explicit parity tests with FBX exporters that use non-unit target weights. This code mirrors Stage 168 normalization but is not independently proven on exotic assets.
- The existing Stage 168 FBX morph delta geometry/normal coordinates, geometry transform compensation, GPU skinning and mesh resource lifetime are NOT changed by Stage 169.
- No actual procedural face rig, FaceID/ARKit, in-between interpolation, >8 facial channels, editor curve UI, continuous morph LOD chain or full SceneLoadCoordinator integration claimed.
- Full safe per-resource GPU token retirement is NOT implemented. No cross-branch merge/rebase was attempted.

## What was actually verified

**ACTUAL:** GitHub connected source read of Stage 168 `ModelFbx.cpp`, `Model.h`, `Animation.cpp`, vendored `ufbx.h` bake/animation evaluation declarations; checked `ufbx_baked_prop`/keyframes and `ufbx_evaluate_blend_weight` public API. SHA-guarded updates to native FBX loader committed; source-level readback/logic inspection. No native compiler or Mac GPU used.

**NOT RUN:** CMake/compiler/link, CTest, actual FBX animation import or rendered morph pixels, Metal/Vulkan/GLES, ASan/TSan/UBSan, CPU/GPU fault injection, 100k-entity benchmark, VRAM. The implementation is **SOURCE ONLY, UNVERIFIED** — never call it tested or complete before acceptance.

## Final native acceptance cases

- Create FBX fixture with one animated Blink morph (0→1→0), plus another static morph; compare imported default weights, `AnimChannel::Weights` key count/order, animation midpoint, runtime sampled `Pose::w`, and GPU skin/morph pixels against ufbx reference.
- Multiple channels and mesh instances, plus skinned/rigid FBX variants, to prove node binding and channel indexing agree with Stage 168 geometry.
- FBX stacked animation layers, nonzero take start, 24/30/60 fps, stepped interpolation, cubic tangents, time boundaries and non-unit target weight.
- Static channels should be omitted when equal to default; no-blendshape models must retain previous animations without morph-only duration limits.
- Test malformed curves/nonfinite values/oversized duration/sample counts: fail safely; missing/duplicate names, >8 channels and in-between shapes rejected.
- Test render parity on Metal, Vulkan and GLES; sanitizer stress, GPU model upload/retirement, memory and crowd perf.
- Inspect FBX property warning filter: supported `UFBX_Weight`/`DeformPercent` should not emit false ignored warnings, unrelated properties still warn.

## Readiness matrix

| Area | Code status | Remaining |
|---|---|---|
| Genre-independent ECS MotionWeave / AnimationGraph | Incrementally integrated | Final native regression |
| Independent child Graphs / hot reload / parameter continuity | Stage 161–164 source | Compiler and runtime parity |
| Skeleton + semantic morph retarget / LOD | Stage 165–167 source | Real imported rig/mesh/perf |
| FBX single-shape morph geometry and names | Stage 168 source | Real FBX/skin GPU pixels |
| **FBX animated Blendshape weight channels** | **Stage 169 source implemented** | Native runtime, curve parity tests |
| FBX in-between shapes and >8 morph channels | NOT IMPLEMENTED | Generalized deformers/GPU topology |
| Full facial authoring, complex IK and full morph LOD | PARTIAL | Production rigs and workflow |
| SceneLoadCoordinator integration | NOT INTEGRATED | Reviewed scene branch adapter |
| Complete GPU Model provider token retirement | NOT IMPLEMENTED | Texture/rollback/cache/fence owners |
| CMake/CTest/Metal/Vulkan/GLES/sanitizers/benchmarks | NOT RUN | One coordinated final Mac run |

## Next Stage 170

Extend generic morph handling beyond the current 8-channel GPU limit (without changing format without renderer parity), and/or implement FBX in-between target shapes with a mathematically defined runtime representation and explicit blend rules. Review GPU Model/texture lifetime and scene hooks only after peer branch reconciliation. Maintain a separate handoff with exact commits and unverified-test disclosures.
