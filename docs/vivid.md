# AIVERSE VIVID — universal digital-human framework

Status: DESIGN / staged implementation; created 2026-11-19.
Owner / branch: VIVID agent, `feature/vivid-digital-humans`.
Handoff log: [docs/agent-handoffs/vivid.md](agent-handoffs/vivid.md).
Code ownership: `engine/character/vivid/`, `tests/vivid/`, `docs/vivid.md`, `skills/visuals/aiverse-vivid/`.
Cross-cutting render, animation and build changes must be small and coordinated with their owners; avoid editing their in-progress features.

## Ada groom/head binding correction — 2026-11-20

The two unskinned Ada card nodes (hair, eyebrows) now attach to the mapped facial
head through bind-preserving transforms. The working KINGSTON source and separate
Ada-Groom-Bound package are corrected; brows retain46 morph targets, lashes remain
in the skinned face. Companion preparation now performs this binding too.
[Focused QA](qa/vivid-head-binding-2026-11-20.md) records exaggerated turn/tilt and
blink frames. This establishes following for this character's cards, not universal
strand grooming, hair physics, arbitrary rig compatibility or UE visual parity.

## SKM attachment authoring — 2026-11-20

The existing assembly panel now supports independent imported SKMs attached to
any named bone or authored socket, local transforms, keep-world/snap rules and
multiple arbitrary slots. See [SKM attachments](vivid-skm-attachments.md).
Native hierarchy/skin roundtrip tests pass; visual hair attachment is not accepted.
The user identified hair not following the head in the previous video; prior
limited-frame attachment conclusions are withdrawn pending corrected native capture.

## Preview motion continuation — 2026-11-20

Stationary preview idle includes upper-body breathing, resting arm movement,
conjugate morph gaze and smooth blink envelopes. Four poses crossfade over 0.65s
without resetting facial phase. Native offline video capture now exercises the
shared Animator transition path. Eyelash white-RGB/alpha-mask conversion is fixed.
[Motion QA](qa/vivid-motion-review-2026-11-20.md) records the actual two videos:
T-pose shirt intersections, coarse hair silhouette and gray eye artifacts remain.
This is not MetaHuman Creator quality parity or a complete clothing solver.

## Imported appearance controls — 2026-11-20

`HumanAppearance.h` defines a separate version-1 recipe with 72 bounded controls,
explicit material roles and a shared native application path. Companion packages
carry `appearance.json`; Creator applies it automatically. Matte fabric and skin
use explicit roughness instead of the low values in packed source maps. Hair
retains coverage and now carries source root/seed attributes for colour effects.
Y-up body proportions affect presentation skin-joint matrices only, including
clothing rigs. The native Studio **VIVID Appearance** panel exposes the profile through a
typed bridge with apply, draft undo/redo, and native save/load. See the
[control reference](vivid-appearance-controls.md). The old Creator DNA sliders
are separate; profile overrides are loaded via the manifest or --appearance.

Reference controls: [Epic hair overrides](https://dev.epicgames.com/documentation/metahuman/hair-overrides)
and [skin tools](https://dev.epicgames.com/documentation/metahuman/skin-material-tools).
The implemented subset has no bundled MetaHuman texture library, authored regional
accent masks, advanced hair geometry grooming or full UE shading parity.

## Companion texture checkpoint — 2026-11-20

Ada's external companion package now uses decoded material textures instead of
uniform cloth colours and raw RGB attribute masks. Cloth colour/detail/AO, iris
masks, hair coverage, skin cavity and teeth maps are handled by the offline baker;
original colour/normal images are preserved. VIVID hair graphs preserve sampled
alpha through colour and shadow evaluation. The companion baker now preserves
soft coverage with blended lashes and a masked hair core plus blended fringe;
the earlier all-MASK conversion lost fractional edge coverage. Core primitives
share interchange accessors, but native import currently duplicates their vertices.
A portrait capture mode permits
close visual inspection. Full UE refraction/hair/skin shader parity is still not
established. See [texture conversion](../tools/vivid-dna-import/README.md).

## Full-character visual checkpoint — 2026-11-20

A separate published Ada companion GLB now renders full body, clothes, shoes,
hair and textured head in Creator. The external preparation script preserves
skins/textures, decompresses Draco and frames the character. Creator demo head
motion preserves imported bind translation. Three real Metal captures (front,
¾, CPU) passed. This is **companion-export support**, not body reconstructed from
the legacy head-only Ada.dna. Cloth uses an explicitly recorded uniform-colour
fallback; complete RigLogic/body animation and UE material parity remain unverified.
See the [full-character workflow](../tools/vivid-dna-import/README.md).

## MetaHuman DNA diagnostic adapter — 2026-11-20

`tools/vivid-dna-import` now provides an offline adapter through an externally
supplied official DNA Calibration reader. Real Ada.dna LOD0 imports into Creator
as 9 meshes / 34625 layout vertices; one selected source morph is routable through
VIVID CPU/GPU. See [adapter guide](../tools/vivid-dna-import/README.md). This is
**partial compatibility**: no RigLogic behavior, skeleton/skin weights, texture
maps or groom import yet. The SDK and sample remain external on KINGSTON.
`--smoke --still` provides a frozen pose and snapped face channels for comparison.

## Integration visual checkpoint — 2026-11-20

The MotionWeave integration branch corrects CPU morph-library lifetime, first-frame
GPU selection, procedural facial parenting, iris depth and iris shader radius.
Creator smoke now requires actual head submissions. Earlier Creator smoke PASS
records were insufficient visual acceptance: floor and hair could pass with a
missing head. The latest separate handoff records rebuilt Metal captures and
scoped regression results. Procedural artwork and standalone Creator captures do
not establish production-human quality or Studio WKWebView parity.

## Purpose and non-goals

VIVID provides believable, customisable digital humans for any AIVERSE game genre: cinematic characters, gameplay avatars, NPCs, crowds, first-person hands, dialogue portraits and user-created avatars. The core MUST NOT depend on RTS, FPS, specific game rules or proprietary Unreal runtime code.

Three independent levels of support:
1. **Interchange character:** import licensed generic glTF/FBX characters, skeletons, animations, textures and morphs; provide standard AIVERSE materials and scalable quality.
2. **MetaHuman-compatible runtime:** handle permitted character exports and optional MetaHuman DNA/rig data through a separately audited adapter (e.g. OpenRigLogic). No dependency on Unreal Engine at game runtime.
3. **VIVID Creator:** editor-side procedural character editing, presets, morphable body/face, skin, eyes, hair, accessories, outfit fitting and non-destructive saved recipes. This is a much later product milestone, NOT an implemented feature.

Never assume that a MetaHuman package can simply be copied or distributed with the engine; inspect Epic asset/export restrictions and third-party dependency licenses per release. The example assets must be own-authored or openly licensed, with provenance.

## Architectural separation

```
external character assets (glTF/FBX/DNA/groom/audio/tracking)
             | offline/authoring converters, validation, provenance
             v
 VIVID CharacterAsset + Skeleton + MorphLibrary + Material profiles
             | shared immutable asset data, per-character ECS references
             v
 VIVID input sources (gameplay, facial tracking, audio, cutscene, AI)
             | semantic facial channels, visemes, expressions, gaze
             v
 VIVID facial solver + rig mapper + corrective deformation
             | pure presentation values, body AnimationGraph/IK layers
             v
 dynamic deformation (GPU compute/vertex; CPU fallback for tooling)
             | bounded LOD/culling/streaming policies
             v
 AIVERSE renderer/RHI (skin/eyes/teeth/hair shaders, hair geometry)
```

- **Presentation vs simulation:** VIVID facial state, animation and hair motion are visual presentation state, not authoritative fixed-point gameplay or net checksum. Gameplay/UI/ECS actors may provide commands or compact semantic replicas; network/replay wire contracts must be explicit and versioned if added later.
- **One skeleton/graph:** reuse `engine/render/AnimationGraph*`, `AnimationRig*`, `Animation.cpp` for body animation. Compose the face as a post-animation modifier / deformation pass. Do not introduce another generic body animation controller.
- **One renderer:** integrate with `engine/render/Model*`, common RHI, materials, Prisma/lighting; do not add an Unreal renderer or bypass RHI for Metal-only features.
- **Data-oriented instances:** immutable shared `CharacterAsset` and mapping tables, compact per-instance `FacialState`, LOD policy and material overrides. ECS components contain handles/state, not per-frame duplicated mesh buffers.
- **Portable core:** C++20, platform-independent CPU reference; use optional backend adapters for GPU compute. macOS Metal and GL/Vulkan paths are verified independently.

## Capability requirements

### Geometry, rig and import
- glTF 2.0/FBX skeletal meshes, body and facial meshes; named shapes, corrective shapes, clip names, sockets, eyes, oral cavity, teeth and tongue; attachment hierarchy and blend-shape tangents if present.
- Support >8 facial morph targets per mesh and >4 skin influences where required **without** raising unrelated uniform/vertex ABI limits silently. Define sparse/corrective GPU morph buffers, 8-influence optional vertex streams and palette strategies; preserve legacy formats and performance.
- MetaHuman DNA LOD mapping, GUI controls and rig logic via optional runtime adapter only after license/API verification. Support multiple rig generations, failure reporting and source-version metadata. FBX blend shapes currently ignored by AIVERSE — implement their conversion before claiming end-to-end FBX facial import.
- Retarget from different skeleton layouts, scale/axes validation and lossless reference pose; offline mesh/skin LOD preserving morph correspondence, face/head attachment remaps.
- Assets carry schemaVersion, import warnings, material slots, texture colour space, dependencies, provenance and character identity-independent asset IDs.

### Facial animation
- Standard 52 ARKit-style semantic channels, extensible custom channels and named blend shapes.
- Blend live face tracking, keyed cinematic animation, authored emotions, visemes/audio-driven lip-sync, procedural micro-motion, blinks and gaze; explicit priority/masks/weights, deterministic input sampling per presentation frame and graceful missing-channel fallback.
- Symmetric/asymmetric controls and correctives, jaw and eye joint drivers, tongue/teeth coupling, head/body layering, stable blending through transitions. AnimationGraph bridge should not overwrite unrelated animated values.
- Precompute/control the rig at character-LOD-specific frequency; support sleeping and update budgets for offscreen/distant actors.

### Rendering and material appearance
- Skin: multiple scattering approximations, subsurface profiles, transmittance, pore micro-normals, sweat/wetness, wrinkle maps, anisotropic detail and makeup variation; all quality-scalable.
- Eyes: cornea/iris/sclera layers, depth/refraction approximation, tearline and wetline; physically plausible eye gaze and catchlights.
- Teeth, gum, tongue, nails and eyelashes with appropriate shading/collision restrictions.
- Hair: hair-card path first, then groom strands with root binding to skinned scalp, tangents, anisotropic shading, shadows, density LOD and collision-aware secondary motion; eyebrows, eyelashes, facial hair and peach fuzz. Fallback to hair cards for mobile.
- Correct in motion, shadows, GI, Prisma postfx, varying exposure and camera distance; no brute-force path-tracing requirement.

### Creator, tools and workflows
- Import inspector with per-asset report, editable facial mapping, expressions/phonemes preview, GPU/CPU deform diff and retargeting diagnostics.
- Preview editor: neutral/brow/smile/frown/jaw/gaze/blink, animation timeline, LOD and skin/hair quality selectors; side-by-side Metal/GL/Vulkan screenshots and GPU/CPU timing.
- Eventually a VIVID Creator: presets, blendable parameter ranges, face/body morphs, rig-compatible clothing, hairstyle/colour, age-appropriate variations, material presets, undo/redo, reproducible JSON recipes and user-generated content validation.
- Optional external DCC workflow (Blender/Maya) and offline import packages; engine runtime must not require them.

## Phased delivery and acceptance gates

| Stage | Deliverable | Acceptance |
|---|---|---|
| 0 | Architecture and handoff contract | This document, owner-scope and durable status; no runtime claims |
| 1 | Facial semantic core | 52 channels, finite validation, tracking/expressions/viseme source mixing, time smoothing; isolated native unit tests |
| 2 | AIVERSE pose bridge | Explicit mapping to existing render::Pose morph slots (0..7), missing names/errors; no source ABI change |
| 3 | Extended GPU deform | Arbitrary named blend shapes, sparse/compute deformation, multiple skin weight streams; GL+Metal comparison incl shadows |
| 4 | Asset import | FBX blend shapes/glTF morph preservation, normal/tangent delta paths, metadata and export-validation CLI |
| 5 | DNA/OpenRigLogic | Optional guarded adapter and licensing audit; version coverage, controlled baseline asset |
| 6 | Appearance | Skin/eyes/mouth materials and quality tiers with reference renders |
| 7 | Hair | Cards, groom strands, scalp binding, physics, update/LOD budget and GPU captures |
| 8 | Performance/Creator | Character LOD, editor authoring, remote playback, mobile budgets and cross-platform regression |

Stages may split into smaller bounded implementation units; after **every** unit commit a separate handoff commit, recording exact SHA, tests actually executed, limitations and next step.

## First-stage public contract

Prefer public headers under `engine/character/vivid/`, namespace `aiverse::vivid`; C++ usable from any native project module. Keep the pure facial core free of render/OS/engine imports. Expose a constant semantic channel order, safe named lookups, an instance-owned input mixer and an explicit `advance(dt)` method. Never silently truncate unknown channels to eight; only the legacy render adapter is temporarily eight-target-limited.

## Required test matrix (do not claim passed until run)
- Native semantic channel unique-name/lookup, all 52, NaN/Inf/negative input rejection, clamping and saturation.
- Source priority/layer interaction, independent controller instances, first-frame setup, dt=0 and pause/resume.
- Pose adapter remapping, duplicate targets, invalid model/node/morph index and animation-layer preservation.
- Rig binary/asset corruption tests, malformed or absent metadata; valid sample renders and facial pose screen captures.
- CPU/GPU skinning and morph numerical parity, sun/reflection/shadow consistency, render-thread safety and repeatability.
- 1/10/100 character CPU/GPU costs with LOD, crowd performance, memory peak and mobile/desktop fallback.

## Status truth (2026-11-19)
At design creation no VIVID runtime, editor, DNA, skin or groom features were implemented/validated. Existing AIVERSE supports up to 8 GPU morphs per primitive and up to 80 joints per primitive palette, and FBX import ignores blend shapes; these are **blockers for full MetaHuman fidelity**, not bugs to remove blindly. This file is the design contract, not completion evidence.

## Implemented stage 1 — portable facial channels (2026-11-19)

Available now (source implemented; full native build **NOT RUN**):
- `engine/character/vivid/Face.h/.cpp`: typed 52-channel ARKit-compatible semantic API, strict names and lookup, instance-owned track/emotion/speech source layering, optional per-layer crossfade, source clearing, safety clamp, exponential smoothing, instant snap and reset.
- `setEmotion(Neutral/Happy/Sad/Angry/Surprise/Disgust/Fear)` and `setViseme(Silence/Aa/Ee/Oh/Fv/Mbp)` are simple *authoring presets*, **not** speech recognition, lip-sync extraction or MetaHuman DNA solving.
- `AIVERSE::vivid` CMake alias, `vivid_face` CTest unit target; no renderer, importer or gameplay coupling.

Usage from native code:

```cpp
#include "engine/character/vivid/Face.h"
aiverse::vivid::FaceController face(0.08f);
face.set(aiverse::vivid::Layer::Tracking, "eyeBlinkLeft", 1.0f);
face.setEmotion(aiverse::vivid::Emotion::Happy, 0.6f);
face.setViseme(aiverse::vivid::Viseme::Aa);
face.advance(dt);
const float jaw = face.frame().value(aiverse::vivid::Channel::JawOpen);
```

The `face.frame()` result is a semantic vector only. **Nothing applies it to meshes or draws a face yet.** No save format, ECS registry or network schema was added. Stage 2 will bridge this data to existing `render::Pose` morph slots while leaving their current 8-target limit explicit.

## Implemented stage 2 — legacy pose bridge (2026-11-19)

Source available, **native build/test not yet executed**.

- `engine/character/vivid/MorphBinding.h/.cpp` adds `aiverse::vivid::MorphRoute` and `MorphBinding::compile(model, routes)`. Compile-time validation checks the face channel, owning model node, target slot and actual mesh primitive morph count, finite scale and duplicate destinations.
- `MorphBinding::apply(frame, pose, influence, MorphApply::Override | Additive)` composes a facial frame **after** `render::Animator::evaluate` or `AnimationGraphInstance::evaluate`, maintaining unrelated bone transforms and morph slots. Invalid input/pose length returns false **without partial modifications**.
- Target `AIVERSE::vivid_render` links the pure semantic core and existing `aiverse_models`; `vivid_morph_binding` CTest source covers rejected routes, interpolation, additive clamping, preservation and invalid inputs.
- Only the **already supported 0..7 pose morph slots** are writable; binding 52 semantic controls to different slots is possible in data, but it does not give a mesh 52 simultaneously usable morph targets. This adapter is *not* the Stage 3 extended GPU morph deformer.

Native host example:

```cpp
#include "engine/character/vivid/MorphBinding.h"
std::vector<aiverse::vivid::MorphRoute> routes {
    {aiverse::vivid::Channel::JawOpen, faceNodeIndex, jawMorphSlot, 1.0f}
};
auto binding = aiverse::vivid::MorphBinding::compile(model, routes);
aiverse::render::Pose pose;
animGraphInstance.evaluate(pose);
face.advance(dt);
binding.apply(face.frame(), pose); // before model renderer consumes pose
```

Next: Stage 3 must add a separately versioned, scalable blend-shape storage/deformation path; do not raise `kMaxMorphTargets` globally or bypass RHI without profiling.

## Implemented Stage 3A — sparse morph reference + GPU buffer contract (2026-11-19)

**Source authored; new native tests NOT RUN yet.** The two earlier `vivid_face` and `vivid_morph_binding` tests were reported PASS by the user (2/2), prior to this stage.

- `engine/character/vivid/SparseMorph.h/.cpp`: immutable `SparseMorphLibrary`, `MorphTargetInput` and `MorphDelta`, named targets with per-vertex position, normal and tangent deltas. Canonical sorting, duplicate/bad numeric validation, zero-delta compaction, finite input validation, persistent source-independent packed layout and sparse CPU reference evaluator.
- No hard eight-slot limit: practical per-asset budget **4096 targets and 16,000,000 sparse entries**. Reference test exercises 160 independent facial targets. These limits are intentional resource-safety budgets, not UE/MetaHuman quotas.
- `GpuMorphDelta`: explicit **64-byte** / 16-byte-aligned representation containing vertex index plus float4 position, normal, tangent deltas. `GpuMorphRange`: 16 bytes per named target, containing index offset/count. Layout validated at compile time and designed to be portable to Metal/GLSL storage buffers. **Not yet GPU executed**.
- `SparseMorphLibrary::evaluate(base, weights, output, normalizeDirections)` is a CPU oracle to measure GPU parity and catch import bugs. The caller supplies fixed-sized vertex and weight spans; no per-call heap allocation; malformed arguments return false without output mutation; same-span in-place evaluation is supported.
- New pure-core test `vivid_sparse_morph`; sources are under `tests/vivid/sparse_morph.cpp`. **CTest not run** on this revision.

This stage does **not** replace the legacy `render::Model` eight-target path, import MetaHuman DNA, bind arbitrary names to semantic channels or produce rendered character pixels. Next stages must generate per-vertex *gather* adjacency to avoid race-prone GPU scattering, wire the new deform buffer into the RHI skinning path, and keep legacy loads identical unless explicit VIVID assets are present.

## Implemented Stage 3B — facial semantics to named morph targets (2026-11-19)

**Source authored, native build/CTest NOT RUN in this stage.**
- `FaceShapeBinding.h/.cpp`: immutable `ShapeRoute{Channel,targetName,gain}` compiled against a `SparseMorphLibrary`. Many distinct morph targets may be controlled by one semantic channel; unknown names and duplicate destination names rejected, with explicit finite/clamped scales. This bypasses the *semantic* 8-target bottleneck without modifying the existing renderer.
- `FaceShapeBinding::evaluate(face, weights, influence)`: per-character weight buffer must be sized to all packed targets; all mapped weights updated in one pass, unused targets zero, invalid input leaves output unmodified. No heap allocations during evaluation.
- `tests/vivid/shape_binding.cpp` test exercises 97 routes (96 jaw corrections + one smile), 97 named shapes in one library, full `FaceController -> FaceShapeBinding -> SparseMorphLibrary::evaluate` chain, parameter validation and instance isolation.
- `aiverse_vivid_shape_tests` and CTest `vivid_shape_binding` registered. **These tests are authored, not yet run**.

Example:

```cpp
std::vector<aiverse::vivid::ShapeRoute> routes{
    {aiverse::vivid::Channel::JawOpen, "jawOpen_primary", 1.0f},
    {aiverse::vivid::Channel::JawOpen, "jawOpen_corrective", 0.4f}
};
auto shapeBinding = aiverse::vivid::FaceShapeBinding::compile(library, routes);
std::vector<float> shapeWeights(library.targets());
shapeBinding.evaluate(face.frame(), shapeWeights);
library.evaluate(baseMesh, shapeWeights, deformedMesh); // CPU reference only
```

Performance: current code evaluates targets on CPU, not on the GPU; do not claim production facial rendering. Next: Stage 3C produce a deterministic per-vertex gather table for a compute/vertex GPU pass without unordered float atomics.

## Implemented Stage 3C — deterministic GPU gather topology (2026-11-19)

**Source authored; full native compile, CTest and Metal/GL/Vulkan execution of this stage NOT RUN.**

A target-major sparse morph delta list cannot safely be scattered to GPU vertices without floating point atomics. VIVID now also compiles a **compressed sparse row (CSR)** per-vertex table:

- `gpuVertexOffsets()`: `uint32[vertexCount+1]`, a prefix sum of how many nonzero shape deltas affect every vertex.
- `gpuVertexLinks()`: `GpuMorphLink{targetIndex,deltaIndex}`, exactly **8 bytes** each, ordered by ascending target index within every vertex.
- GPU kernel contract (not yet implemented): one thread per output vertex; loop `links[offsets[vertex]..offsets[vertex+1])`, multiply corresponding morph delta by that target weight and accumulate position/normal/tangent locally, with no cross-thread scatter or float atomic writes. Feed resulting vertex buffer into ordinary skinning/rendering. This can support much more than eight channels without changing legacy `render::Pose` arrays.
- `SparseMorphLibrary::evaluateGather()`: portable, no-alloc CPU implementation of the GPU gather algorithm, for parity against existing `evaluate()` and future Metal/GL/Vulkan tests.
- `GpuMorphDelta` 64B, `GpuMorphRange` 16B, `GpuMorphLink` 8B and offsets `uint32` form the proposed GPU buffer contract; exact backend alignment/barrier/ownership rules must be confirmed before shipping GPU dispatch.
- Conservative asset budgets now explicitly include **16,000,000 vertices**, **4096 targets**, and **16,000,000 sparse deltas**. Values above the vertex budget fail before large allocations.
- `tests/vivid/sparse_morph.cpp` now checks exact CSR offsets/references and a 160-target gather/scatter CPU parity case. Updated test not run in this stage.

End-to-end renderer changes remain pending. The existing 8-target `render::Model` morph pipeline is unchanged. Shader compute dispatch, buffer ownership, dirty weights, skinning order, shadow and reflection path consistency, async compute barriers, mobile fallback and live MetaHuman import are **not** implemented.

## Native verification checkpoint — 2026-11-19

**User-confirmed macOS validation at commit `31d04c986ada3ff4602d303d28fa4cdc08686de8`.** Configured successfully on macOS, built targets `aiverse_vivid_face_tests`, `aiverse_vivid_morph_tests`, `aiverse_vivid_sparse_tests`, and `aiverse_vivid_shape_tests`. User-shared CTest output reports **4/4 passed, 0 failed** in **0.71 s**: `vivid_face` (0.03 s), `vivid_sparse_morph` (0.03 s), `vivid_shape_binding` (0.61 s), `vivid_morph_binding` (0.03 s). No warning/error diagnostics appear in the shared post-fix build output. These results validate the current CPU facial core, sparse morph/gather reference and routing tests, **not** a GPU morph deformer, Metal/Vulkan/OpenGL integration, DNA import, or visual output. Tests were executed by the user on Mac, not independently by this agent. Full suite and other configurations remain unverified.

## Stage 4A — opt-in GPU morph compute dispatcher (2026-11-19)

**Source committed, native build/GPU execution NOT RUN.** This is an *opt-in* RHI adapter, not an integrated renderer/character pipeline.

- `engine/character/vivid/GpuMorph.h/.cpp` adds `GpuMorphDeformer::initialize(gfx::Device&, SparseMorphLibrary, baseVertices)`, `dispatch(weights)`, `outputBuffer()`, `download()` (diagnostic-only between frames), `release()`. Six RHI storage buffers: 48B GPU-packed base vertices, 64B deltas, uint CSR offsets, 8B links, float weights and a 48B packed output stream. A 32B aligned uniform block supplies counts and normalization mode.
- `VividComputeShaders.h` supplies paired Metal/GLSL compute shaders. Each GPU invocation gathers every sparse morph contribution for one vertex, using the CSR table with no floating-point atomics. Position, normal and tangent are accumulated; normal/tangent normalization is optional. It runs over the existing RHI compute interface; Metal has a concrete compute implementation. GL/Vulkan support depends on actual backend capability and has **not** been proven by simply supplying GLSL source.
- Independent link target `AIVERSE::vivid_gpu`, linked to `aiverse_vivid` + `aiverse_engine`. `vivid_gpu_contract` CTest uses a fake gfx device to verify buffer allocations, uniforms, dispatch/bindings and failure cleanup. **The fake device does not execute GPU code or validate MSL compilation.**
- `GpuMorphDeformer` memory allocation is capped to 512 MiB and rejects nonfinite vertices/weights, invalid counts and unsupported compute backends. GPU resources own their device lifetime; do not destroy device before VIVID deformer. `initialize` and `release` are device-thread-only, `dispatch` must run inside a GPU frame but outside render passes.
- **Missing before production**: actual Apple Metal GPU dispatch/readback numerical parity, GPU-to-Model skinning integration, output vertex format use in existing render passes, buffer synchronization across frame slots, tested GLES/GL/Vulkan fallback, complex-body/facial corrective graph and LOD.

### MotionWeave integration policy (2026-11-19)

MotionWeave is on `feature/universal-ecs-animation-pipeline` (inspected at **`2ae54740331d7ed6ba8d8e01e6c39e753391852f`**), with `AIVERSE::MotionWeave`, `"engine/animation/MotionWeave.h"` and presentation ECS World. Its branch is **913 commits ahead of main**, substantially divergent from the current VIVID branch (913 ahead, 46 behind on GitHub compare). Its ongoing animation feature scope includes `render::Pose`, resource/lifetime/retargeting and shared RHI files. **No MotionWeave commits have been merged into VIVID during Stage 4A**: a wholesale merge would introduce unrelated evolving renderer changes before the GPU morph adapter needs those interfaces.

Future adapter must consume MotionWeave's *final presentation pose* (after animation-graph IK, retargeting and authoring adjustments), blend VIVID face semantics as a clearly ordered post-graph stage and feed a separate *extended morph GPU stream*. Keep MotionWeave as authoritative producer of skeleton pose and prevent competing pose writers, preserve presentation-only ECS isolation and transfer only safe GPU buffers to the render thread. Once MotionWeave is stable/merged into main, integrate via public `AIVERSE::MotionWeave` rather than copy its animation algorithms. Recheck HEAD before cherry-picks; tests in MotionWeave documentation are deferred and are not evidence of production readiness.

## MotionWeave integration — merged on 2026-11-19

The earlier Stage 4A sentence saying MotionWeave is "not merged" is superseded by this section. MotionWeave branch snapshot `548f75f0ce824db6f468f6577da95387d388ea0b` was merged into VIVID using **real two-parent merge commit `f27c076410a5cb0aa7d12cc689d83c7eff302928`**, with VIVID parent `dfd4f3fab67cd573da4322ddad2f108a27f7cb38`. The merge tree starts from MotionWeave's full snapshot, adds the complete original VIVID source and tests, and deliberately combines the two overlapping files `AGENTS.md` and `CMakeLists.txt`; no features were intentionally dropped.

The resulting branch exposes `AIVERSE::MotionWeave`, `aiverse::motionweave::MotionWeave::update`, `AnimationPoseComponent`, the shared animation graph/retarget pipeline and the original VIVID `AIVERSE::vivid`, `AIVERSE::vivid_render`, `AIVERSE::vivid_gpu` targets. Source-branch MotionWeave and `main` remain untouched. This is a source-level integration, **not** a green combined native build or verified renderer path: macOS CMake, MotionWeave's tests and GPU readback tests remain outstanding on this merged revision.

VIVID shall **reuse** MotionWeave's presentation World, scheduler, finished `AnimationPoseComponent` and managed rig; it must not add a second skeleton graph, retargeting system, IK solver, animation worker scheduler or LOD manager. The order is: `MotionWeave::update` finishes (including worker joins) → VIVID samples inputs and computes facial weights → VIVID creates a derived render pose/morph stream **without modifying MotionWeave's cached pose** → the existing AIVERSE renderer consumes the result. Read and use `engine/animation/MotionWeave.h` and `engine/animation/EcsAnimationSystems.h`.

## Stage 4B — explicit VIVID / MotionWeave pose bridge (2026-11-19)

**Implemented in source; combined native build/CTest NOT RUN yet.** New `AIVERSE::vivid_motionweave` target depends on `AIVERSE::MotionWeave` and `AIVERSE::vivid_render`. Root CMake registers VIVID **after** the MotionWeave target is declared, avoiding unresolved CMake alias references.

Public function `composeFaceAfterMotionWeave(source, facial, binding, output, scratch, influence, mode)` in `engine/character/vivid/MotionWeaveBridge.h` consumes `aiverse::motionweave::AnimationPoseComponent`. Call only **after** `MotionWeave::update` and its worker-join barrier. It retains a single owner for AnimationGraph and the bone skeleton (MotionWeave). VIVID only applies the legacy-morph facial layer onto a **derived** render pose; neither the cached MotionWeave pose nor the matrix array is mutated. `output` and `scratch` are caller-owned reusable `render::Pose` buffers; the function uses scratch + swap to avoid a second copy and protect previously rendered output if an input is invalid. A repeated frame never accumulates additive face weights into a cached MotionWeave frame.

```cpp
#include "engine/animation/MotionWeave.h"
#include "engine/character/vivid/MotionWeaveBridge.h"

// MotionWeave is the ONE animation scheduler.
aiverse::motionweave::MotionWeave::update(presentation, dt);
const auto& source = presentation.get<aiverse::motionweave::AnimationPoseComponent>(actor);
face.advance(dt);
aiverse::vivid::composeFaceAfterMotionWeave(
    source, face.frame(), morphBinding, renderPose, scratchPose);
```

The adapter covers only legacy `render::Pose` morph indices 0..7 because that is the format that MotionWeave/Renderer3D currently exposes. Extended 52+ shape weights are handled separately by VIVID's `FaceShapeBinding` / GPU CSR morph stream, pending real GPU numerical parity and renderer consumption. This is **not** a fork of MotionWeave, nor a replacement of the rig/retargeting pipeline.

New headless CTest source: `tests/vivid/motionweave_bridge.cpp`, registered as `vivid_motionweave_bridge` / target `aiverse_vivid_motionweave_tests`. The fixture uses real MotionWeave presentation ECS World and a bind-pose AnimationGraph and checks source isolation, repeat-frame additive stability, morph preservation and invalid-input rejection. Native compilation and execution **NOT RUN** in this stage.

## Stage 4D — real macOS Metal compute parity test (2026-11-19)

**Test source added; native Metal execution NOT RUN on this new commit.** This stage validates the GPU kernel directly, not yet the engine's complete model renderer.

- New macOS-only `tests/vivid/metal_gpu.mm`, target `aiverse_vivid_metal_gpu_tests`, CTest `vivid_metal_gpu`. It compiles the *actual* `kVividMorphMSL` kernel from `VividComputeShaders.h` at runtime using Apple's Metal framework and dispatches 64-thread workgroups, with an intentionally non-multiple-of-64 **137-vertex** mesh and **160 independent morph targets**.
- Test builds the same `SparseMorphLibrary`/CSR and `GpuMorphVertex`/uniform buffer layout that the VIVID GPU adapter uses. It uploads all six buffers (base, sparse deltas, vertex offsets, links, weights, output), dispatches and waits for completion, then compares all resulting vertex positions/normals/tangents against `SparseMorphLibrary::evaluateGather` within a maximum absolute difference of `0.0005`.
- Three real Metal passes: authored weights with normalized directions, authored weights without normalization, and all-zero weights. Non-finite outputs or a shader/pipeline/command failure cause a failing CTest. A machine without a Metal-capable device exits with skip code 77 and is reported **Skipped**, not Passed.
- CMake enables Objective-C++ only for macOS when tests are enabled and links Foundation + Metal. No changes to `engine/gfx` or MotionWeave source files, public C++ API, saved games or network formats.
- **What this test does not prove:** the `gfx::Device` Metal command path (`GpuMorphDeformer::dispatch()`), GPU resource lifetime across frames, drawing with VIVID-deformed mesh buffers, VIVID↔MotionWeave skinning order, other render passes or Vulkan/GL. A separate actual device RHI integration test and renderer hookup are required after the kernel passes.

The user's earlier **7/7 native tests Passed** on the pre-Stage-4D merged source. No real Metal GPU comparison has yet been executed, and do not transfer those passes to this new test.

## Stage 4F — real AIVERSE Metal RHI end-to-end compute validation (2026-11-19)

**Code committed; new native test NOT RUN on the Mac yet.** Stage 4D's direct Metal compute test `vivid_metal_gpu` was reported **Passed (1/1, 0.68 s total)** by the user at source SHA `424e8382144511c1b453ce9d3234cc813734356c`. Its CMake/Objective-C++ issue is resolved. This proves the kernel/CSR ABI numerically on real Apple hardware but not the AIVERSE GPU resource path.

New `tests/vivid/metal_rhi.mm` calls the engine's actual `aiverse::gfx::createMetalDevice` using a headless CAMetalLayer and drives `GpuMorphDeformer::initialize/dispatch/download/release`, using `gfx::Device::beginFrame/endFrame` between multiple submissions. It constructs **160 sparse morph targets / 79 vertices**, reads the actual output RHI storage buffer and compares all position/normal/tangent components to `SparseMorphLibrary::evaluateGather` with 0.0005 absolute tolerance. Four consecutive GPU frames vary morph weights and normalization and verify that deformation never accidentally accumulates across frames.

The macOS-only `aiverse_vivid_metal_rhi_tests` binary links `engine/gfx/metal/GfxMetal.mm` in the standalone test rather than linking all of `aiverse_runtime`; a minimal `platform::readContentFile` stub replaces the runtime's optional shader-archive content hook for this test only. CMake target uses Objective-C++20, ARC, Foundation, QuartzCore and Metal frameworks. CTest entry is `vivid_metal_rhi` with return code 77 treated as Skipped only if no physical Metal device exists; shader/RHI failures must fail.

This does **not** yet connect the GPU-computed 48B `GpuMorphVertex` stream to `ModelVertex` drawing/skin vertex buffers or the final MotionWeave skinned pose. Render integration will require careful frame ownership, barriers, LOD and shadow parity.

User Mac runner: `cmake --build build/vivid --target aiverse_vivid_metal_rhi_tests -j 4` then `ctest --test-dir build/vivid --output-on-failure -R '^vivid_metal_rhi$'`. Record exact compile errors, warnings and GPU parity numbers in the next stage handoff; until then **new RHI test is NOT RUN**.

## Stage 5A — render-compatible 60-byte GPU morph stream (2026-11-19)

**New implementation and Metal RHI test source committed; full native build / new GPU test NOT RUN on this revision.**

The renderer consumes a **60-byte** `render::ModelVertex` with float3 position/normal, float2 UV, float4 tangent with handedness in `.w`, 4B colour, 4B joints and 4B skin weights. VIVID's sparse morph kernel outputs a separate **48-byte** `GpuMorphVertex` (3 float4). Feeding 48-byte memory directly into Renderer3D's fixed 60-byte vertex pipeline would corrupt geometry and skin/UV data.

VIVID now has a second, **optional GPU compute stage**, `GpuMorphMeshStream` in `engine/character/vivid/GpuMorphMeshStream.h/.cpp` backed by matching Metal and GLSL compute kernels in `GpuMorphMeshShaders.h`. It consumes the original 60-byte vertex buffer + the morphed 48-byte stream and produces a packed 60-byte vertex stream. A GPU thread handles exactly one vertex and copies the original 15 uint32 words, replacing only the bit patterns for position (0–2), normal (3–5), and tangent.xyz (8–10); UV (6–7), tangent handedness (11), colour (12), joints (13) and weights (14) are preserved **bit-for-bit**. This means existing Renderer3D skinning semantics remain intact: morph deformation precedes skinning and no skeleton algorithms are duplicated.

Public API: `AIVERSE::vivid_mesh`, `GpuMorphMeshStream::initialize(device, span<ModelVertex>)`, `dispatch(deformedGpuBuffer, vertexCount)`, `vertexBuffer()`, `download(span<ModelVertex>)`, `release()`. A 512 MiB combined source/destination budget prevents unbounded GPU allocation. The full model stream (same indexed/global vertex space) is required, not a primitive-local partial stream.

The real Metal RHI test `vivid_metal_rhi` was **extended (source only)** to dispatch VIVID's sparse gather then stream conversion in one GPU frame across four weight profiles; compare all deformed position/normal/tangent components against `SparseMorphLibrary::evaluateGather` and require UV, tangent.w, colour, joint IDs and joint weights to match the original 60-byte vertices byte-for-byte. The previous `vivid_metal_rhi` passed on user Mac at `22b60c471`, **before this extension**; therefore extended vertex-stream test is NOT YET VERIFIED.

**Still NOT wired into normal draw calls.** `ModelDraw` does not yet accept an opt-in per-instance GPU vertex-buffer override. `GpuMorphMeshStream::vertexBuffer()` is now a storage buffer with model-compatible bytes, but the renderer must explicitly handle lifetime across colour, depth, shadows, instancing, geometry pooling/ICB and LOD before binding it for production. The 8-target compatibility path remains unchanged. MotionWeave still owns the finished pose, skin matrices and animation scheduling; VIVID only prepares input vertices for existing skin shaders.

## Stage 5B — Apple Metal MSL keyword / mesh compute diagnostics (2026-11-19)

**Source fix committed, Metal GPU test NOT RUN after fix.** The user successfully built the added 60B GPU stream target and the real Metal RHI test, but the `vivid_metal_rhi` CTest **FAILED** before submitting the new mesh conversion kernel, reporting only `VIVID RHI 60-byte mesh stream initialization failed`. The separate `vivid_metal_gpu` direct Metal kernel test remained **Passed**. The combined selection was **1/2 Passed**, not 2/2.

The new `kVividMeshStreamMSL` kernel incorrectly used `vertex` as a C++/Metal parameter name (`uint vertex [[thread_position_in_grid]]`), colliding with Metal's `vertex` stage qualifier keyword. It now uses `vertexIndex` consistently in the MSL source. The GLSL compute shader and all buffer layouts are unchanged.

The real `tests/vivid/metal_rhi.mm` error path now emits `GpuMorphMeshStream::lastError()` and `gfx::Device::lastError()` when `initialize` fails, making any remaining shader compile or allocation error actionable instead of hiding the cause behind a generic assertion. Further MSL/API compiler behavior must be verified by the next macOS run; **this is a root-cause hypothesis supported by source inspection, not yet proof of a passing shader compile**.

Run `aiverse_vivid_metal_rhi_tests` and `aiverse_vivid_metal_gpu_tests` on Mac again before changing `Renderer3D`. No changes were made to `MotionWeave`, `ModelVertex`, the general RHI or game save/network data.

## Stage 5C — opt-in Renderer3D dynamic vertex-stream override (2026-11-19)

**Source authored and committed; native build / renderer colour-shadow visual tests NOT RUN yet.** A production VIVID actor must not be enabled until compile and visual validation are green.

The AIVERSE renderer now accepts **per-`ModelDraw` opt-in fields**, `vertexOverride` (`gfx::BufferHandle`) and `vertexOverrideCount` (`uint32_t`). They default to an invalid handle/zero, preserving the legacy path. Pass `GpuMorphMeshStream::vertexBuffer()` and `vertexCount()` only after VIVID compute submissions in the **same GPU frame before render passes**. The buffer must belong to the exact gfx device and remain alive through all shadow/colour/reflection/depth draws, GPU command completion and any captures.

The header-only `engine/render/ModelVertexStreamOverride.h` policy validates that the stream is a full, model-relative **60-byte `ModelVertex`** array, long enough for all primitive vertex ranges, matches a retained CPU `model.vertices.size()` when that data exists, and is never used with pooled geometry, nonzero model base-vertex/index offsets or specialized geometry clusters. Invalid opt-ins are rejected; they never silently render the original unmorphed mesh.

Initial acceptance is intentionally limited to **Metal**, dedicated **dynamic** `Model` buffers (no `kDrawStatic`, no `BakedSample`). Queued `ModelItem`s retain the custom handle. `drawModelBatch` binds the opt-in vertex stream in place of `model->vb` for ordinary draws. The same draw path handles shadow, opaque/transparent, reflection and other phases, preserving existing `ModelVertex` attribute layout, materials and MotionWeave-provided skin matrices.

**Safety gates**: overridden models disable the legacy eight-slot GPU morph shader (avoids double deformation), cached rigid lookup, regular/GPU LOD selection, primitive material/shadow merging, rigid bounds culling and primitive occlusion, regular instance batching, MDI, GPU-authored/encoded ICB and geometry-cluster optimizations. Switching an existing persistent-ID object to a stream explicitly retires its stale static scene record. These are conservative early-stage restrictions; richer culling/LOD/batching is deferred to a separate measured stage.

Example host-side integration (pseudo usage inside the same gfx frame):

```cpp
// MotionWeave remains sole owner of the animation pose and skeleton palette.
// VIVID data was prepared before the draw; 60B stream contains morph only.
deformer.dispatch(shapeWeights);
meshStream.dispatch(deformer.outputBuffer(), deformer.vertexCount());

render::ModelDraw draw;
draw.model = &model;
draw.nodeWorld = &motionWeavePose.matrices;
draw.vertexOverride = meshStream.vertexBuffer();
draw.vertexOverrideCount = static_cast<std::uint32_t>(meshStream.vertexCount());
// renderer.drawModel(draw) queues this item; renderer renders model passes later
// in the same GPU frame while both compute outputs remain alive.
```

New headless `vivid_vertex_override` test verifies public default-off behavior, valid/invalid stream sizes and shared-geometry offset exclusions. It does not render pixels or test real RHI state.

**Still pending:** actual native compilation of merged Renderer3D on Mac, baseline regression, real scene and colour/shadow/LOD/culling screenshots, GPU fences/in-flight safe reuse, material/normal-map parity, visual bounds correctness, and performance benchmarks; no claim that VIVID characters render successfully yet.

## Stage 5D — native Renderer3D colour/shadow/MotionWeave visual preview (2026-11-19)

**Preview executable authored; GUI build, pixel capture, colour/shadow visual parity and renderer link NOT RUN by this agent.** Prior 3/3 Mac test evidence was for Stage 5C pre-preview source, and must not be attributed to this new program.

The new macOS-only `aiverse_vivid_renderer_preview` is linked to the full platform-owned `aiverse_runtime`, `AIVERSE::vivid_mesh` and **existing `AIVERSE::MotionWeave`**, rather than creating an alternate animation system. It has its own test-only `aiverseMain` entry and runs a small procedural sphere/head and floor under real `Renderer3D` shadow + colour passes, using the actual Metal gfx device supplied by the platform.

It composes one reusable presentation World/AnimationGraph clip for gentle vertical node motion, and VIVID's semantic `FaceController`/`FaceShapeBinding` for two exaggerated facial target regions (jaw stretch and right-side displacement) to make visual differences measurable. Each frame, before calling `Renderer3D::drawModel`, it dispatches the sparse 48B CSR morph deformer and 60B mesh stream packer. The original model path renders the baseline, while the later frames explicitly assign `ModelDraw::vertexOverride` / `vertexOverrideCount`. Both paths use the same material and existing `nodeWorld` matrices. For this deliberately isolated fixture, global geometry pooling and the new model's LOD and instancing/MDI features are disabled; no production configuration is changed.

It requests three actual RGBA screen captures from `gfx::Device::captureNextFrame`, writing `/tmp/aiverse-vivid-baseline.ppm`, `/tmp/aiverse-vivid-morph.ppm`, and `/tmp/aiverse-vivid-motionweave.ppm` by default. Set `AIVERSE_VIVID_PREVIEW_PREFIX` to change the path prefix. The fixture checks that the render profile submitted both a shadow and a colour pass, model shaders report no error, the three captures are full-sized, a meaningful number of pixels differ between base and morphed render, and a later MotionWeave-driven node motion causes another visible change. This is a **diagnostic threshold test**, not a photographic/perceptual proof of realistic facial animation or exact shadow-map parity.

The program is intentionally **not registered as CTest**, because it opens a real macOS GUI Metal window and requires drawable/present/capture. It terminates with exit code 0 only after successful capture/comparison, otherwise 1. GPU resources are released only after a real idle barrier and after finishing all render passes. The default legacy renderer code path is unchanged.

Run on the user's Mac from any directory with a GUI session:
```sh
cmake --build ~/Projects/AIVERSE-vivid/build/vivid --target aiverse_vivid_renderer_preview -j 4
AIVERSE_VIVID_PREVIEW_PREFIX=/tmp/aiverse-vivid \
  ~/Projects/AIVERSE-vivid/build/vivid/aiverse_vivid_renderer_preview
```
The executable may be an app bundle on some CMake generators; inspect the generated target for its actual path rather than assuming. Keep all model, VIVID GPU stream and MotionWeave resources alive until rendering finishes. **NOT RUN** on the current coding host.

## Stage 5E — procedural vertex-range metadata and deferred Metal capture (2026-11-19)

**Source fixes committed; new native GUI run NOT RUN after these changes.**

The user's macOS `aiverse_vivid_renderer_preview` built and printed `ready: 925 vertices, 2 GPU shapes`. It delivered the initial baseline PPM but stopped with `missing shadow/colour model submissions: 0/0` and `model vertex override: rejected invalid/pooled 60B stream`. The `[metal-assets]` report had 11 source misses, 15 pipelines and zero compilation failures. This is not a passing visual preview.

Inspection found that `render::modelFromParts` had populated `Primitive.firstIndex`, `Primitive.indexCount`, and appended CPU vertices, but left both `Primitive.firstVertex` and `Primitive.vertexCount` at **zero**. The strict `modelVertexOverrideStatus` correctly rejected the procedurally generated head because no usable primitive vertex range could be established. `engine/render/Model.cpp` now populates both fields for each procedural part using the model-relative vertex base and count. A separate regression checks two distinct parts, checks the override before and after CPU vertex eviction and retains rejection of genuinely invalid / pooled geometry. We deliberately **did not weaken** `Renderer3D`'s safety checks or permit an unknown vertex stream length.

First-time Metal model shader compilation can also leave zero submitted draws for some frames without the shader itself being broken. The preview no longer fails immediately when the profile shows zero shadow and colour draws. Instead, it records successful draw submissions for four consecutive frames before requesting a screenshot. It proceeds in three stages (baseline, GPU facial morph, MotionWeave node animation), holding the current stage until the asynchronous screenshot callback reports a real saved 960×640 RGBA image. A later stage's capture is not requested until the previous capture completes. It emits progress diagnostics every 45 frames and fails clearly after 400 frames if warmup/capture cannot finish. It also checks procedural vertex ranges at initialization and uses explicit preview shadow bounds.

**Test status:** these modifications have not yet been compiled or visually run on the user's Mac. The earlier CTest 3/3 GPU/vertex-policy passes were on an earlier commit. The test-only preview requires a macOS GUI / Metal drawable and is not a CTest. The ordinary renderer path and MotionWeave ECS skeletal-pose ownership are unchanged.

Run `aiverse_vivid_vertex_override_tests` and the GUI preview after pulling the new branch. Inspect three PPM files and the printed pixel differences; until they exist and both model pass counters become nonzero, do not declare this stage complete.

## Stage 5F — per-character GPU stream isolation and frame ownership (2026-11-19)

**Code committed; real Metal multi-actor test NOT RUN yet.** Stage 5E's real macOS visual preview **passed** on user-checked SHA `53e5f0cb069dff8d4c173001947ffce3b4791a98`: all three captured frames, real shadow and colour model submissions, 81,988 baseline-to-morph changed pixels, and 66,972 additional MotionWeave-pose changed pixels. User's selected CTests were 2/2 PASS. Handoff verification commit: `4dea755d192b70abe2501a63eba553d4230ffd06`.

Next, VIVID gains **`AIVERSE::vivid_actor`**, a per-character GPU presentation helper (`GpuActorStream.h/.cpp`). It owns one `GpuMorphDeformer`, one `GpuMorphMeshStream` and one instance-local semantic morph weight vector. Each actor has **independent mutable GPU weight and output buffers**. It may refer to the same immutable authored `SparseMorphLibrary` and uploaded `render::Model` metadata as other actors, but each actor owns its own RHI allocations in this initial implementation. Sharing pipelines and immutable CSR uploads without compromising per-instance output ownership is a later optimization.

Public workflow:

```cpp
vivid::GpuActorStream actor;
actor.initialize(dev, model, sparseLibrary, modelVertexSpan);

// Inside a platform-owned GPU frame, outside all render passes:
motionweave::MotionWeave::update(presentationWorld, dt); // sole skeleton owner
actor.dispatch(faceController.frame(), faceShapeBinding);

render::ModelDraw draw;
draw.model = &model;
draw.nodeWorld = &presentationWorld.get<
    motionweave::AnimationPoseComponent>(entity).matrices;
if (actor.attach(draw)) renderer.drawModel(draw);
```

The `dispatch` contract requires an active **Metal** frame with a nonzero `gfx::Device::frameSerial()`. An actor permits **only one two-stage compute submission per frame**; repeated calls are rejected rather than overwriting memory already queued for draw calls. `attach(draw)` is transactional and only succeeds for exactly the same `render::Model` and a valid stream produced in the **current device frame**. This prevents accidental reuse of an old character's vertex stream after frame rollover or attaching to a different model. The renderer still validates full 60B vertex metadata and does not enable pooled/static/ICB paths. No new animation graph or scheduler was implemented: MotionWeave remains authoritative for body pose, skin palettes, IK and retargeting.

At exceptional model unload or actor removal, the host must finish the GPU frame, then call `actor.shutdownAfterIdle()` **outside any active frame** before allowing the model and device to be destroyed. The method invokes `gfx::Device::waitForResourceIdle()` and returns false on failure without deliberately freeing GPU resources. This is **not a per-frame stall**; frame-to-frame updates simply reuse the actor's GPU allocations. `GpuMorphDeformer` and `GpuMorphMeshStream` still have ordinary destructors; a host must not destroy an actor or its device while GPU work is in flight. A scalable token-based nonblocking retirement queue for frequent dynamic despawns is future work.

The existing real macOS `vivid_metal_rhi` test has been **extended (not yet executed on the new revision)** with two actors referencing the same model and morph library. It checks separate output handles, three frames of different face weights, CPU-vs-GPU vertex parity for both actors, preservation of immutable UV/colour/tangent-sign/skin weights, rejection of a duplicate per-frame dispatch, rejection of stale-frame and wrong-model attachments, and explicit idle teardown. The ordinary VIVID and MotionWeave tests and the previously passing real visual preview are **not automatically certified** after this new source change.

## Stage 5G — batched asynchronous actor GPU resource retirement (2026-11-19)

**Implementation committed; Stage 5G CTests and native Mac GPU test NOT RUN yet.**

The user supplied a native macOS test log for Stage 5F: `aiverse_vivid_actor` compiled successfully; `vivid_metal_gpu`, `vivid_metal_rhi` and `vivid_vertex_override` all passed (3/3, 3.29 seconds). The real RHI test includes two independent actors over three frames. This result is recorded in the agent handoff and predates the new code below.

VIVID now adds `GpuActorRetirementQueue` (`engine/character/vivid/GpuActorRetirement.h/.cpp`) built inside `AIVERSE::vivid_actor`. The implementation deliberately reuses **`render::AsyncResourceRetirementQueue<std::unique_ptr<GpuActorStream>>`** from the renderer. There is no second retirement state machine or a new scheduler. A batch of actors queued after the GPU frame has been committed receives one backend `gfx::Device::beginResourceRetirement()` completion marker. `collect()` polls the ordered tokens and only releases actor GPU memory when `resourceRetirementComplete()` confirms success. A `Pending` or `Failed` token does not authorize release. Unsupported tokens require `waitForResourceIdle()` fallback; failed fences may only be bypassed by a successful full idle barrier on explicit `drain()`. Normal token-supported frame progression is **nonblocking** and never stalls per retired actor.

Actor GPU resource teardown is refactored through the private `GpuActorStream::releaseAfterGpuCompletion()`, accessible only to `GpuActorRetirementQueue` and invoked after token completion or a successful shutdown idle barrier. Normal `GpuActorStream::shutdownAfterIdle()` remains supported. Both paths release the two actor-owned compute pipelines and output/input/weight GPU allocations, preserving per-actor ownership and not touching shared `render::Model` resources. MotionWeave still owns body animation.

Usage at a graphics-device frame boundary:

```cpp
vivid::GpuActorRetirementQueue retirement(device);
std::unique_ptr<vivid::GpuActorStream> actor = /* initialized VIVID actor */;

// After ALL colour/shadow draws referencing this actor have been committed
// by gfx::Device::endFrame(), not during beginFrame/endFrame:
if (!retirement.retire(actor)) { /* ownership remains in actor */ }
retirement.collect(); // submits one token for the pending actor batch
// On later real frame boundaries:
retirement.collect(); // releases completed actors without blocking

// Before destroying the device or the retirement queue:
if (!retirement.drain()) { /* keep queue and device alive; retry */ }
```

The queue exposes pending counts, saturation, diagnostic observer and the shared engine's detailed stats. `retire` rejects actors owned by a different graphics device. The caller must keep the model, device and queue alive until GPU completion, must **successfully drain the queue** before destroying it, and must never start a retirement marker while commands referencing the actor are still being recorded in an uncommitted frame. A finished token is not a GPU-to-CPU readback and does not carry performance guarantees.

New portable CTest `vivid_actor_retirement` uses an instrumented mock Metal RHI and checks two actor objects retired under one marker, no GPU buffer destruction while `Pending`, resource release and statistics only on `Complete`, retention of a `Failed` marker, failed/successful idle recovery, unsupported-token fallback, and rejection of cross-device actors. It makes no claim of actual GPU work. The existing **real** Metal `vivid_metal_rhi` CTest is also extended with an actor GPU submission and a subsequently committed nonblocking resource retirement marker. The test allows a bounded polling window and fails if a real completion marker never completes.

**Outstanding:** build and run the new headless and Metal tests on the user's macOS host. A safe shared immutable CSR asset/pipeline cache, renderer frame-budget benchmark, multi-character GUI soak, and device-recreation tests remain separate milestones. No change to authoritative simulation, save/network schemas, `render::ModelVertex` layout, `gfx::Device` ABI or MotionWeave APIs.

## Stage 5H — shared immutable GPU assets and content-addressed cache (2026-11-19)

**Source implementation committed; new shared-cache C++/Metal tests not yet verified on the user's Mac.**

VIVID actors can now share immutable GPU inputs and shader pipelines while retaining independent outputs. `GpuMorphSharedAsset` uploads a model's original 48B bind-pose morph vertices, CSR deltas/offsets/links, original 60B `ModelVertex` array, and both MSL/GLSL compute programs. Each `GpuActorStream` holds only a GPU-asset reference plus its *own* shape-weight buffer, 48B compute output and 60B render output. This reduces total buffers for N actors sharing the same asset from approximately `8*N` to `5+3*N`, and compute programs from `2*N` to `2`; GPU bytes also drop because bind geometry and CSR data are no longer replicated.

The previous `GpuActorStream::initialize(device,model,library,vertices)` remains source-compatible and creates an independent asset internally. To share intentionally, create a common `GpuMorphSharedAsset` or call the new `GpuMorphAssetCache::acquire(library,originalVertices)`, then call `GpuActorStream::initializeShared(device,model,sharedAsset)` for each actor. The weak device-local cache hashes **all GPU-relevant source bytes** from model vertices, CSR deltas, offsets and links, plus counts, into a two-word digest; identical authored geometry reuses the immutable GPU asset. A changed source geometry/C SR content gets a distinct entry. This is an internal cache key, not a cryptographic trust boundary or a serialized identity.

The cache keeps only **weak references** and can be pruned with `pruneExpired()`. The final strong reference to the asset must only disappear after ALL actors referring to it have completed their GPU work (the new generic token-based `GpuActorRetirementQueue` handles ordinary despawns). The cache is per-`gfx::Device` and must not outlive that device, across device recreation or GPU loss. It runs on the device owner thread, not a parallel loader thread; the CPU C++ `SparseMorphLibrary` may still be reused by many jobs elsewhere. GPU shaders are not duplicated and MotionWeave remains the sole scheduler for skeleton pose.

New coverage: the fake-RHI `vivid_actor_retirement` includes a 128-character 4-frame scenario verifying that immutable buffers/pipelines are shared, actor outputs remain separate, and a single ordered GPU token retires all 128 actors without blocking. It also verifies a cache hit for identical geometry, a separate asset on changed geometry, weak-entry pruning, and safe last-reference destruction. Real Metal `vivid_metal_rhi` now creates two independent GPU actors from a single cached GPU asset and tests their numerical parity over three frames, plus the existing async retirement test. The 3/3 user Mac tests from Stage 5F **predate** this source refactor; these extended tests are **NOT RUN** yet.

Explicit limitations: the cache does not currently deduplicate CPU-authoring metadata, textures, skin rigs, or MotionWeave pose buffers. The render override remains a Metal-only opt-in and still excludes unsafe MDI/ICB/LOD batching. The queue requires completion-validated final references and successful drain before gfx device shutdown. Automated performance measurements on large fully shaded crowds, per-asset budget controls, Vulkan GPU parity and photorealistic face/hair/eyes remain future work.

## Stage 5I — one-pass complete VIVID native validation (2026-11-19)

**Automation authored, not a substitute for actually successful tests.** `tools/vivid-validate.sh` is the supported macOS one-command acceptance runner. Call it from the existing VIVID source tree:

```sh
bash ~/Projects/AIVERSE-vivid/tools/vivid-validate.sh
```

It aborts rather than switching a dirty tracked worktree, fetches `origin/feature/vivid-digital-humans`, records the exact detached Git SHA, configures a separate `build/vivid-onepass` tree with tests, builds every VIVID test binary and the MotionWeave API test and real GUI preview, runs `ctest -R '^(vivid_|motionweave_api$)'` sequentially (avoiding concurrent Metal GPU resource contention), then opens the macOS Metal Renderer3D/MotionWeave 3-frame visual preview. It requires three nonempty screenshots and a logged `pass=1` report before declaring one-pass acceptance. Detailed configure/build/test/preview logs, screenshots, revision and a summary are retained under `build/vivid-onepass/vivid-validation`. The shell uses `set -Eeuo pipefail` so any failed compilation, test or visual capture halts the command.

`--no-gui` performs a **partial** headless validation; `--full` builds all engine targets and runs the full CTest suite before the same GUI preview. Neither optional mode is required for the normal VIVID acceptance.

The focused GitHub Actions workflow `.github/workflows/vivid-validation.yml` independently tries the same native macOS VIVID/MotionWeave compile+CTest selection on pushes to this branch; hosted runners may lack an accessible Metal device, in which case the Metal tests report **Skipped**, not proven GPU parity. GitHub Actions does not execute the GUI visual preview, which requires a real graphical Mac. A queued CI workflow is NOT a passing test. Do not cite CI as green until its jobs conclude successfully; user Mac remains authoritative for Metal/Renderer3D captures.

Current requirements for release-quality MetaHuman-like humans that remain outside this focused GPU facial-animation path include authored human DNA/rig assets, skin/eye shaders, groom and hair simulation, facial corrective shapes on actual head scans, mesh streaming/LOD benchmarks, editor authoring UI and cross-backend GPU parity. These are separate product features and must not be claimed finished just because the current VIVID graphics core passes.

## Stage 5J — multi-actor visual acceptance (2026-11-19)

The existing asset-free Metal GUI `aiverse_vivid_renderer_preview` now also supports a crowd mode through `AIVERSE_VIVID_PREVIEW_ACTORS=1..32`. The preview creates one `GpuMorphAssetCache` and obtains one shared immutable GPU CSR/compute program set for all actors. Each instance holds a separate `GpuActorStream` with its own writable weights and 48B/60B deformation buffers. Scene snapshots still use the original three-stage baseline → expression morph → MotionWeave node pose pipeline and verify real shadow/colour draws, screenshot presence and visible pixel changes. For multiple actors, expressions vary independently while the shared skeleton pose remains authored/sampled once by MotionWeave.

During preview teardown, all actors are moved into one `GpuActorRetirementQueue` and released after **one successful GPU idle barrier**. Only then are the final shared asset reference, source geometry and renderer freed. This is a shutdown-only barrier, not a stall within each frame. The original single-character preview remains the default if the environment variable is absent.

`tools/vivid-validate.sh` now automatically launches the preview **twice** after all VIVID and MotionWeave CTests: once with the original single-character default and once with **12 actors** (`AIVERSE_VIVID_CROWD_ACTORS` overrides the count). Each pass must save three nonempty PPMs and print `pass=1`; one failed capture or missing actor output fails the entire one-command acceptance. The script retains both log streams and all six screenshots under `build/vivid-onepass/vivid-validation`.

The new code and GUI paths have **NOT** been built or executed on the user's Mac after this change. The prior successful 3-frame GUI preview (81,988 morph-changed pixels / 66,972 MotionWeave-changed pixels) was for one actor on Stage 5E; it does **not** certify the 12-actor preview. Likewise a queued GitHub Actions job has not passed until the test runner reports successful completion. This stage is about visual consistency and lifetime at small crowd scale, not production performance, geometry LOD/ICB acceleration, authored realistic faces or hair.

## Stage 5J accepted — full focused macOS GPU + GUI validation PASS (2026-11-19)

User completed `tools/vivid-validate.sh` at exact detached SHA `fa5504a9f2e1f64087ef73139525e5bde31976b4`. All **11/11** selected `vivid_*` and `motionweave_api` CTests passed (10.19s, 0 failed). In particular, real `vivid_metal_gpu`, `vivid_metal_rhi`, actor retirement / shared GPU asset stress and render vertex override all passed.

Two real Metal Renderer3D GUI previews also completed with `pass=1` and three PPM captures each: baseline → morph → MotionWeave. **One actor:** shadow=1, colour=1, 81,988 morph pixel differences, 66,972 MotionWeave pixel differences. **Twelve actors:** shadow=12, colour=12, 179,673 morph pixel differences and 183,275 MotionWeave pixel differences. Both previews reported a shared immutable GPU morph asset of **158,324 bytes**, 925 vertices and two shapes. Metal graphics diagnostics reported `pipelineFailures=0`.

The Mac runner saved the full results under `build/vivid-onepass/vivid-validation`: exact SHA, separate CMake/CTest/GUI logs, six PPM images and `summary.txt`. The full result and provenance are appended to `docs/agent-handoffs/vivid.md`.

**Acceptance scope:** VIVID's focused C++20/Metal GPU facial deformation, independent multi-character weights/outputs, shared immutable morph GPU cache, safe token-based retirement, and Renderer3D/MotionWeave single-and-12-character diagnostic rendering are **verified by actual user Mac tests**. The agent has not personally inspected the PPM pixel appearance; programmatic image differences do not prove realistic character quality or exact shadow-pixel parity. Entire-engine tests, long-duration lifetime/performance, Vulkan/OpenGL GPU parity, scalable GPU batching/LOD, authoring editor and realistic DNA/rig/eyes/skin/hair/groom assets are distinct future stages and are **not** certified by this run.

## Stage 6A — editable versioned HumanDNA assets (2026-11-19)

Added `HumanDNA.h/.cpp` and `AIVERSE::vivid_human`. A single versioned JSON recipe describes facial/body proportions, skin/melanin/hemoglobin/scattering/pore parameters, iris/cornea/tear film, groom density/length/curl/wind, semantic rig naming and the 52 expression rest weights. Four built-in presets are available. Import is bounded, strict and rejects unknown fields, nonfinite floats, invalid resources/rig names and unsupported versions; export produces a canonical JSON document. This data is presentation-only, not authoritative simulation or network state. A `vivid_human_dna` headless regression tests canonical JSON roundtrip, presets, bounds, malformed inputs and missing optional sections. It does not yet import proprietary MetaHuman DNA.

**Source committed, new Mac tests NOT RUN**. Other creator/shading/groom work is in progress; see next stage handoffs for precise tested status.

## Stage 6B — human appearance, facial joints and groom cards (2026-11-19)

New `HumanRig` binds the DNA-named root/head/jaw/eye joints to any existing `render::Model` hierarchy and produces a **separate** node-matrix overlay on top of MotionWeave's pose. It rejects invalid/nonfinite facial inputs without altering MotionWeave state. Missing jaw/eye bones are accepted for morph-only imports; required head/root must exist.

`HumanMaterials` compiles material ShaderGraphs using AIVERSE's shared GLSL/MSL generator. Skin includes pigment/hemoglobin and inexpensive view-dependent scattering with pore noise, roughness and wetness response; iris includes pupil/limbal ring and coloured fibres; sclera, cornea (separate alpha-blended surface), hair cards, lashes and teeth use the same PBR renderer. This is a real shader-graph integration but **not** diffusion-profile physically-correct SSS, ray-traced eyes or an offline hair scattering solution.

`HumanGroomRuntime` creates deterministic hair/scalp guides, generates multi-LOD indexed PBR card meshes, simulates bounded Verlet secondary motion with simple scalp exclusion and populates a full `render::ModelVertex` stream. `generateHumanPreview` creates an asset-free procedural reference head with layered eyes, eyelids, brows, jaw, cheeks, mouth, teeth, groom and five named corrective morphs. All are ordinary `render::Model` assets, and the main model/morph path remains unchanged unless a caller opts in. An imported scanned glTF/FBX human may reuse the DNA and shader/rig APIs, but the procedural face does not reproduce MetaHuman scan quality.

`HumanCreatorSession` provides immutable validated DNA transactions, undo/redo, preset changes and safe JSON load/save for a UI. New `vivid_human_rig`, `vivid_human_appearance` and `vivid_human_creator` unit tests cover hierarchy, shader graph compilation, groom LOD/finite geometry, facial corrective mapping and editing history. **These new tests are NOT YET RUN on macOS.**

## Stage 6C — standalone VIVID Creator UI (2026-11-19)

The project now has a standalone, interactive C++20/ImGui **`aiverse_vivid_creator`** built against the existing AIVERSE `Renderer3D`, `gfx::Device`, `render::Model` and MotionWeave libraries, instead of introducing a parallel engine. Its DNA panels cover head/jaw/nose/cheek/eye/lip proportions, skin pigment and scattering, iris/limbus/cornea/tear film, hair colour/guide count/segments/length/curl/clumping/stiffness/damping/gravity/wind, facial controls (jaw/blink/smile), model/groom LOD, camera and animation. A `HumanCreatorSession` owns validated transactional changes with undo/redo and versioned recipe JSON save/load. GUI font uses the engine's native asset path. The program is not embedded as a tab in the main editor; it is a standalone character-creation tool with ordinary public native C++ APIs.

The Creator renders a real editable head via VIVID GPU compute streams with MotionWeave's skeletal/node pose, layer-separated cornea/iris/sclera/PBR skin and a deformable hair-card mesh. During preview, card vertices can be updated through `gfx::Usage::Dynamic`. `--smoke` creates a real GPU capture from a GUI Metal session and checks both shadow and colour submissions and nonempty PPM output. The script `tools/vivid-validate.sh` builds the creator and runs `aiverse_vivid_creator --smoke`, saving `vivid-creator.ppm` and diagnostics. It also keeps the previous single/12-actor facial preview tests.

**Stage 6C native tests/UI compile/GPU screenshot NOT RUN yet.** This tool is a functional source implementation, not yet a certified production character artist interface or MetaHuman-level renderer.

## Stage 6D — opt-in human BRDF skin/eye/hair shading (2026-11-19)

The existing `render::Material::shading` enum now adds explicit `Skin`, `Eye` and `Hair` modes while leaving legacy `Pbr` and `Unlit` values intact. `humanProfile` provides two surface-specific float parameters; `GpuMaterialState.h` includes them in exact batching equivalence/hashes, so different skin/eye/hair shaders cannot be merged accidentally. `Renderer3DModels.cpp` passes the opt-in shading mode/profile through the existing U2 material layout without changing shader ABI size. Paired MSL and GLSL `ModelShaders.h` fragment branches add approximate skin wrapped/transmitted diffuse, tear-film/cornea highlights and tangent-directed hair glints. The shared `HumanMaterials` layer creates the matching graphs and maps DNA fields into these PBR profiles.

This is an **efficient physically inspired approximation**, not measured multi-layer skin diffusion, a spectral eye refraction model or physically accurate Marschner strand scattering. It runs through ordinary AIVERSE colour/shadow/material pipelines and does not change legacy PBR unless `HumanSurface` opts in. The renderer, new BRDF shader permutations and eye/hair screenshots **are not yet compiled or GPU-tested** on Mac after these changes.

## Stage 6E — open glTF/FBX/OBJ human interchange (2026-11-19)

An open authoring manifest (version 1) can reference a native `render::loadModel`-supported model, a JSON `HumanDNA` profile and optional sparse morph JSON with named target arrays. `HumanImport.h/.cpp` validates project-relative asset paths, required root/head rig hierarchy, material names/types, 52 facial expression channel routes, sparse morph vertex indices/finite deltas and import size limits. Model vertex ranges missing from OBJ imports are reconstructed conservatively from actual indices, and post-prepare original vertex positions are checked to reject silent topology reorder before publishing sparse corrections. `human_import` CTest covers manifest JSON roundtrip, invalid paths/channels/sizes, OBJ root/head import with two shapes and morph-free variants. `HumanCreator` now supports selecting a `--asset /path/to/manifest.json` on the command line or using the **Import glTF/FBX/OBJ + DNA + morphs** button, and can switch back to the procedural reference model. Imported geometry remains authored (anatomy sliders do not resculpt an imported scan), but DNA materials, groom and facial expressions can be edited and saved; MotionWeave still owns the resulting animation pose.

This deliberately does not decode Epic proprietary MetaHuman DNA binary assets. Imported glTF/FBX must have the required rig joint names (or a matching DNA rig map) and morph JSON must refer to imported model-relative vertex IDs. Haircard groom is currently regenerated from VIVID DNA rather than converted from an imported external strand cache. Source is committed but **native compiler / importer CTest / GPU Creator import screenshots are NOT RUN** yet.

## Stage 6F — DNA-driven appearance refinements and end-to-end creator acceptance (2026-11-19)

This stage closes several source-level correctness gaps in the new human authoring feature:

- Renderer3D's paired GLSL/MSL corneal glint now uses the authored **cornea IOR** (Schlick-normal-incidence approximation), not just tear-film wetness. The IOR slider produces a real shader effect when the `Eye` shading mode is active.
- Skin ShaderGraph uses DNA `wrinkleStrength` for extra micro-crease variation in addition to pores. Hair ShaderGraph now uses groom melanin for pigmentation, while groom geometry generation responds to `density` and `clump` settings instead of merely storing them.
- The procedural head reference now includes independent neck, shoulder and upper-torso meshes, driven by `heightMeters`, `shoulderWidth` and `bodyBuild`. These body meshes are parented to the root so they do not inherit every head nod. This is a **reference bust**, not a full scanned/rigged body.
- Creator exposes configurable root/head/jaw/eye bone aliases, more anatomy controls and support for imported authored hair geometry without adding overlapping procedural groom cards. Imported alpha-masked hair cutouts, UV transforms and albedo/normal/MR/AO maps are preserved.
- `HumanCreatorSession::saveFile` no longer deletes the existing last-good DNA recipe after a failed atomic replacement. It reports the failure and keeps the staging file available.
- A small original manifest/OBJ/CSR package under `tests/vivid/assets` allows the Creator GUI to smoke-test actual **external asset import** without bundling proprietary scans.
- The one-shot `tools/vivid-validate.sh` now builds and executes **16 VIVID/MotionWeave CTests**, then GUI-renders original 1-actor, 12-actor, procedural human Creator, warm-preset human Creator and imported external head Creator. It checks all required PPM captures and the screenshot test's render-pass results; `tools/vivid-creator/compare_ppm.py` compares neutral/warm materials **only outside the GUI inspector** to validate actual 3D viewport changes. All logs/screenshots remain under `build/vivid-onepass/vivid-validation`.

**Important:** Stage 6A–6F have been implemented in GitHub but the new native C++ test targets, shader programs, real Creator GUI and imported-head captures have **NOT RUN on the user's Mac yet**. The previous Stage 5J 11/11 passes on SHA `fa5504a9f2e1f64087ef73139525e5bde31976b4` predate all of these changes. We do not claim physically measured MetaHuman skin/groom/eye parity or release readiness; real authored high-resolution human scans, proprietary asset licensing, complete body rig/groom/LOD streaming, GPU time measurements, Vulkan parity and full AIVERSE regression remain independent gates.


## Stage 6G — groom collision respects head DNA (2026-11-19)

`HumanGroomRuntime` now retains scalp ellipsoid radii from the same `HumanDNA` parameters used to generate hair roots. Secondary-motion constraint projection excludes the DNA-proportional head shape rather than a hard-coded world-space sphere. A final contact projection closes the gap left by sequential strand-length correction. The `vivid_human_appearance` test now includes wind-driven scalp nonpenetration for multiple head widths and scales.

**Source and regression authored, native build/CTest and GPU Creator preview NOT RUN yet.** This is a procedural ellipsoid collider and does not yet support exact collision with imported scalp meshes. See `docs/agent-handoffs/vivid.md` for implementation SHAs and validation instructions.


## Stage 6H–6J — mesh collisions, bounded groom, eye DNA, Creator expression authoring (2026-11-19)

The optional `HumanGroomRuntime::setCollisionMesh` accepts a bounded closed triangle mesh in groom-local coordinates and uses nearest-surface projection/odd ray parity to exclude interior hair points. `clearCollisionMesh` restores the DNA-scaled ellipsoid fallback. Verlet travel is capped per segment and collision velocity is damped to reduce extreme-frame penetration. This CPU reference path remains **O(strand points × triangle count)** and needs spatial acceleration, stable handling of non-manifold geometry, animated mesh support and validated attachment roots before production-scale use. The current Creator does not automatically install an imported scalp collision mesh.

`HumanMaterials` now routes `scleraRedness` into actual sclera PBR pigment. The Creator exposes all 52 `Face.h` facial defaults for authoring and undoable DNA persistence; interactive jaw, blink and smile preview controls override their corresponding defaults. Existing Hair High/Med/Low is a groom presentation quality selector rather than mesh-LOD authoring.

**Source authored; native macOS build, the newly extended `vivid_human_appearance` test, actual imported character GPU preview, and all-engine regressions NOT RUN** for these commits. The next validation is `bash tools/vivid-validate.sh --full` on an appropriate Mac, with logs appended to `docs/agent-handoffs/vivid.md`. True mesh sculpting, skinned scalp collision/BVH, production hair simulation, physically correct eye refraction and multilayer SSS are still outstanding.


## Stage 6K–6O — collision acceleration and imported-human authoring (2026-11-19)

The procedural hair runtime now builds a bounded median-split **CPU BVH** from an optional closed scalp proxy. Mesh validation rejects open/non-manifold edges; nearest-surface projection and ray-parity interior checks use the hierarchy rather than scanning all triangles each simulation iteration. Guide updates run bounded internal steps up to 120 Hz, with collision inelastic response.

Open v1 import manifests may specify `"scalpColliderMesh": "closed_head_proxy"`. Creator extracts that named mesh's triangles into a compact collider, rejects explicitly skinned and open meshes, and projects guide roots onto the collision proxy, translating rest and previous positions consistently. This is **static bind-pose local-space authoring**. Dynamic skinned refits, arbitrary node transforms and full self-/cloth-/body collision are not yet supported.

Creator now also exposes imported facial ARKit channel-to-morph route gain sliders (0–4) and can atomically save edited manifests. The existing 52-channel DNA authoring and procedural groom LOD selectors remain available.

**No new C++ build, CTest, Metal GUI acceptance or performance benchmarks have been run for these changes** (implementation-first request). Source includes newly authored regression assertions covering open-surface rejection, last-good preservation, scalp root relocation, manifest roundtrip and 30/120 Hz substep equivalence. These are not passing-test claims. Full photoreal MetaHuman-level character creation still requires additional artist-authored mesh sculpt/rig/LOD tools, realistic scanned assets, accurate skin/eye/hair optics and runtime GPU performance validation. See `docs/agent-handoffs/vivid.md` for implementation SHAs and the final validation gate.


## Stage 6Q — animated groom root attachments (2026-11-19)

An imported closed scalp collision mesh now supports stable per-guide source triangle/barycentric anchors. During a successful `refitCollisionMesh`, guide roots, their rest positions, live positions and previous positions follow the deformed local-space scalp triangles. Failed geometry/refit/attachment staging retains previously published triangle/BVH and guide data. Collider replacement and clear invalidate old triangle identifiers; callers must reattach roots after changing topology.

This is a **CPU geometry API**, not yet automatic MotionWeave-to-skinned-scalp streaming. `VIVID Creator` currently attaches static import proxies at authoring time. No source changes from this stage have been natively built or tested on macOS; test assertions were added but not executed. See `docs/agent-handoffs/vivid.md` for exact commits and future integration gates.


## Stage 6R — Creator scalp proxy selection (2026-11-19)

The imported-character Creator panel now exposes the optional named `scalpColliderMesh` from the v1 manifest. UI edits validate a staged manifest, preserve the last-good selection on error and request a new model/groom rebuild on success; the existing atomic manifest Save action persists the change. Empty selection falls back to the DNA ellipsoid.

This source change was committed but **not compiled or GUI-tested**. Authoring a closed/unskinned scalp proxy, transforming an imported node hierarchy into groom-local coordinates and driving deforming scalp meshes from MotionWeave remain separate tasks.


## Stage 6S — MotionWeave-driven rigid scalp refit (2026-11-19)

An imported closed rigid scalp-proxy in VIVID Creator can now follow a named MotionWeave animated node. The Creator stores original proxy positions and bind-pose transforms, converts the current node transform into the already-used head-local groom coordinate system, and transactionally refits the CPU collision BVH before presentation hair simulation. Invalid/nonfinite computed poses are rejected while retaining the last-good collider. No new skeleton scheduler is introduced.

**Scope:** only an unskinned scalp-proxy with a single owning model node and existing groom-local vertex convention. Multi-joint skinning, morph-deformed scalp geometry, arbitrary transform authoring, collision self-contacts and full character sculpting are still unfinished. This stage is committed as source but **native build, CTests, Metal GPU GUI and visual parity have NOT RUN**. Full implementation details and next-step handoff live in `docs/agent-handoffs/vivid.md`.


## Stage 6T — MotionWeave multi-joint scalp skinning adapter (2026-11-19)

A new header-only `ScalpSkinning` CPU adapter resolves imported scalp-proxy vertices against each render primitive's skin joint palette and inverse-bind matrices. It normalizes four 8-bit weights, computes linear-blend deformation from MotionWeave node-world matrices, converts points to the VIVID head-local groom coordinate system, and passes them into transactional BVH refit. Creator now detects and labels rigid-node versus multi-joint scalp modes. The older rigid path remains available; no new animation scheduler or engine/genre-specific API is introduced.

Skinned reference source and negative-case unit checks are committed but **not executed**. This is not yet native-build-verified or real-GLB/FBX visual validated. Production-quality implementation still needs per-frame scratch reuse, animated scanned-head authoring assets, high-density performance work, full sculpt/rig/LOD tooling and more physically accurate skin/eye/hair shaders. See `docs/agent-handoffs/vivid.md` Stage 6T for exact commits.


## Stage 6V–6W — non-destructive morph sculpting and semantic authoring (2026-11-19)

VIVID Creator now includes an imported sparse morph authoring panel. Numeric source-vertex position offsets can be changed without touching the original mesh, with transactional undo/redo and export to the existing open morph sidecar JSON schema. The importer exposes the original model's vertex count so automatically appended LOD vertices cannot accidentally be exported as original morph vertex IDs.

Creators may add/delete named corrective morph targets, bind one to any of the existing 52 ARKit semantic channels, adjust gains via the existing imported route UI, and save both morph sidecars and manifests. Deletion removes dangling mappings; target undo does not currently restore those mappings.

**Actual native build/CTest/Metal screenshots NOT RUN** for these additions. The panel edits numeric offsets by vertex ID; this is not yet a viewport brush/cage sculpt, rig joint editor, authored facial LOD pipeline or real MetaHuman-quality DCC tooling. See `docs/agent-handoffs/vivid.md` Stages 6V–6W for exact commits and remaining work.


## Stage 6X — imported facial sculpt falloff brush (2026-11-19)

VIVID Creator's imported sparse morph editor now supports a source-space smooth falloff brush anchored to a selected original mesh vertex ID. A stroke applies position offsets to nearby source vertices, with bounded radius/intensity, recompiles the sparse morph once, supports undo/redo as one edit, and exports to the existing open version 1 morph JSON sidecar. Named corrective target creation/deletion and 52-channel ARKit route authoring are also available.

This is **numeric/source-ID brush authoring**, not full 3D viewport picking, geodesic brush sculpting or topology-aware symmetry. The artist's original mesh stays unchanged. Stage 6V–6X source and new test assertions are committed, but **C++ build, CTest, Metal GUI and production asset verification have NOT RUN**. See `docs/agent-handoffs/vivid.md` for source commits and remaining work.


## Stage 6Y — viewport sculpt selection and mirrored brush (2026-11-19)

VIVID Creator now permits selecting an imported model mesh and arming a 3D viewport click to select the nearest original source vertex on the frontmost intersected indexed triangle of that mesh. A screen marker shows the selected vertex. The non-destructive sparse morph brush now supports optional X=0 bilateral symmetry in source-model coordinates: mirrored X offsets invert direction, other axes keep direction, and the entire stroke participates in one undo/redo transaction. Source model vertices and MotionWeave simulation remain unchanged.

**Current limitation:** selection uses source vertices transformed by pose node matrices, not GPU-morphed or multi-joint skinned final geometry; high-density picking is an O(triangle count) CPU operation. The feature also lacks topology-aware/geodesic brush falloff and per-character symmetry planes. This stage is source-authored, **not yet compiled or GPU/GUI-tested** on Mac. Full technical handoff and commit history: `docs/agent-handoffs/vivid.md` Stage 6Y.


## Stage 6Z — rig pose corrections and conservative sparse-morph LOD transfer (2026-11-19)

VIVID Creator now has an opt-in, non-destructive facial rig correction panel for selecting a named model node, authoring a bounded local XYZ translation, undo/redo and versioned JSON save/load. These corrections apply as a hierarchy-aware **presentation pose overlay after MotionWeave**, not as destructive bind-pose or inverse-bind authoring. Imported FBX/glTF source data is preserved.

A separate `MorphLodTransfer` helper carries VIVID sparse facial morph targets to a target LOD when an explicit source-vertex correspondence table is provided. An additional strict reference matcher can infer that table only for original/LOD vertices with unambiguous near-identical positions and a bounded 32K diagnostic budget. Arbitrarily decimated LOD meshes require proper barycentric surface mapping and are not supported by this matcher.

**The code and new regression assertions are not yet natively compiled or tested.** No Metal preview, CTest or production model validation was performed for these features. Outstanding tasks include true bind-pose/skin rebinding, asset-owned rig correction publication, full LOD authoring/bake/editor integration, and physically detailed skin/eyes/strand hair. See Stage 6Z in `docs/agent-handoffs/vivid.md` for implementation commits, caveats and next steps.


## Stage 7A — barycentric facial LOD transfer (2026-11-19)

VIVID now has an opt-in CPU `MorphLodSurface` authoring helper for transferring sparse facial morphs to independently decimated LOD vertex streams. It projects each LOD vertex onto an original indexed triangle, stores stable barycentric correspondence and interpolates all authored corrective position, normal and tangent deltas before recompiling the LOD-specific sparse CSR representation. The fallback rejects nonfinite/degenerate data, outside-tolerance vertex positions and authoring workloads exceeding the bounded eight-million comparison budget.

This is source-level morph transfer, **not** automatic mesh simplification, Creator authoring UI integration or runtime LOD switching. Large meshes need a spatial acceleration structure and precise region constraints to avoid false matches across different facial features. The new implementation and regression assertions have **NOT RUN** on macOS; no compiled/GPU-tested parity is claimed. Stage 7A source SHAs and next steps are in `docs/agent-handoffs/vivid.md`.


## Stage 7B — persistent facial LOD projection mapping (2026-11-19)

A versioned `MorphLodMapping` JSON sidecar now records the source mesh dimensions and each target LOD vertex's barycentric triangle projection. Stored maps can be reloaded to produce target-specific sparse morph CSR data without recalculating Stage 7A surface proximity. The loader enforces bounded dimensions, finite coordinates and barycentric values and verifies source vertex/triangle counts.

**Not yet production-safe against unrelated same-sized meshes:** a topology fingerprint and authoring asset identity remain required. Creator sidecar UI, BVH accelerated surface search, GPU LOD switching and native Mac acceptance are still outstanding. Source and regression assertions are committed, **tests not run**. Exact commits and handoff are in `docs/agent-handoffs/vivid.md`.


### Stage 7B source-identity safety update

Cached `MorphLodMapping` now additionally requires a deterministic source geometry fingerprint calculated from original vertex float bits and source triangle indices; reusing a sidecar against changed geometry fails even when vertex and triangle counts happen to match. This is accidental mismatching protection, not a cryptographic guarantee. Earlier development sidecars without fingerprints must be regenerated. Native compilation and testing remain **NOT RUN**.


## Stage 7C — BVH facial LOD bake authoring (2026-11-19)

A new `MorphLodSurface::projectAccelerated` builds a median-split triangle BVH for nearest-source-surface mapping of independently decimated LOD vertices. It retains the same barycentric mapping structure and is cross-checked in authored source tests against the scalar reference, avoiding its quadratic eight-million-query limit. Model/triangle/vertex budgets still apply.

VIVID Creator now has `Facial LOD surface bake` controls for choosing separate source/target model meshes, selecting the allowed projection distance and exporting `vivid-facial-lod.mapping.json` plus `vivid-facial-lod.morphs.json`. The mapping includes exact source fingerprint, barycentric references and sorted model-relative target vertex IDs. This is an **offline authoring/export path**; GPU actor LOD switching, correct per-LOD materials/skinning, one-transaction multi-file publication and topology-aware seam constraints are not yet complete.

New C++ source and regression tests are committed but **not compiled or run**. Stage 7C detailed handoff and implementation commits are in `docs/agent-handoffs/vivid.md`.


## Stage 7D — full-model GPU actor facial morph parity across authored LODs (2026-11-19)

The Creator now optionally publishes an authored Stage 7C LOD facial mapping into VIVID's existing **full-model** `GpuActorStream` vertex override. `MorphLodGpuAuthoring::combine` retains all original facial morph deltas, maps compact target-LOD deltas onto their exact model-relative vertex IDs, validates identical target name/order and rejects conflicting or invalid source/LOD vertices. One existing ARKit weight buffer therefore drives original and authored LOD morphs in the same Metal GPU compute dispatch; Renderer3D continues selecting authored `MeshDef::lods` normally. No GPU buffer is replaced mid-render.

Creator enables this after a successful LOD bake and provides an optional publication toggle and validated `Load baked LOD mapping` control. A normal rare GPU-idle rebuild acquires the new full-model shared asset, preserving the existing per-actor resource ownership rules. Source fingerprint is recomputed on every reimport; prepared model streams may append vertices after the stable original prefix.

This stage is currently **one authored LOD mapping per actor**, not a multi-LOD publish pipeline. It does not generate decimated meshes, skin palettes or Renderer3D LOD metadata. The VIVID actor remains Metal-only, so Vulkan/GLES compute parity is still open. New code and regression checks are committed, but **native C++ compile, CTest, real Metal GPU actor/LOD image parity and automated Creator acceptance have NOT RUN**. Consult `docs/agent-handoffs/vivid.md` Stage 7D for commit SHAs, test provenance and next tasks.


## Stage 7E — multi-LOD facial GPU morph publication (2026-11-19)

VIVID Creator now author-stages up to eight distinct facial LOD mappings instead of replacing the previous one after each bake. Each chosen target mesh gets its own `vivid-facial-lod-{meshId}.mapping.json` and `.morphs.json` sidecars, with explicit reimport and remove-level controls. A full-model `MorphLodGpuAuthoring::combineMany` pass validates compatible named shape ordering, disjoint sparse delta destinations and correct model-relative vertex ranges; one existing `GpuActorStream` dispatch therefore writes both source and all included authored-LOD facial vertices under the same ARKit weights. The existing Renderer3D LOD selectors remain responsible for drawing, without GPU buffer swaps inside frames.

The feature does **not** automatically create decimated meshes or embed a LOD-chain manifest; true imported-LOD discovery, multi-file atomic publication, Vulkan/GLES GPU morph backend implementation and visual Renderer3D LOD parity are still open. New source/tests were authored but **not built or run**. Exact commits and next-agent handoff: `docs/agent-handoffs/vivid.md` Stage 7E.


## Stage 7F — manifest-indexed facial LOD chain and immutable publication (2026-11-20)

`HumanImportManifest` now lists up to eight authored `facialLods` with named source mesh, target mesh and mapping sidecar path. Creator automatically discovers indexed maps during package import, checks source topology fingerprint and exact target IDs, preflights all sparse morph layers as one combined GPU Actor Stream CSR, and only then commits the loaded editing state. Missing, malformed, duplicate and incompatible layers reject package import.

New baked LODs publish through content-addressed immutable mapping/morph sidecars, followed by a staged atomic replacement of the **manifest last**. Removing an LOD also atomically updates the manifest index. Already published sidecar bytes are not overwritten; abandoned old sidecars can be collected separately. This gives readers a consistent package index on normal local filesystems, not a multi-file atomic transaction.

The source-level implementation and negative-case tests have been committed; **no new native C++ build, CTest, Metal Renderer3D LOD frame parity, or Vulkan/GLES compute test has been run**. True target-mesh content identity, multi-LOD performance, GPU backend coverage, full bind-pose editing, hair self-collision, and high-end skin/eye/hair rendering remain open. See `docs/agent-handoffs/vivid.md` Stage 7F for exact commits and next handoff.


## Stage 7G–7H — RHI-neutral GPU dispatch gate and GLES CPU fallback (2026-11-20)

VIVID's `GpuActorStream` no longer hard-rejects non-Metal GPUs; it requires the RHI to actually advertise compute support, a valid frame serial and healthy GPU state. Existing MSL and GLSL 4.30 CSR/mesh-packing kernels remain unchanged. This unlocks eligible compute devices at the VIVID API level but **does not by itself implement or verify Vulkan backend compute**.

VIVID Creator can now fall back to a separate `CpuActorStream` on baseline GLES3/WebGL2 or any non-compute device. That actor uses the same sparse morph source and ARKit semantic bindings to deform the full `ModelVertex` stream on CPU and upload a dynamic vertex override through standard gfx::Device; original UVs, joints, material data and model LOD vertex ranges remain intact. It requires no compute shaders or frame serial, and retains the existing safe resource-release contract. This is a CPU fallback, not GPU morphing on GLES.

A fake-GLES headless CTest for output bytes and retirement was added, but **not compiled/run**. Real Android/WebGL2 vertex-buffer update reliability, bandwidth, GPU image parity and native Vulkan compute remain to be measured/implemented. Detailed ownership, commits and test gate are in `docs/agent-handoffs/vivid.md` Stages 7G–7H.


## Stage 7J — integration with merged universal ECS animation source lifetime (2026-11-20)

The VIVID branch now contains the full `feature/universal-ecs-animation-pipeline` history via two-parent merge `8c9048fb6f0407ad87cc62d112b53fa467d43f78`. The merged animation system introduces safer model publication epochs, read leases and GPU/CPU retirement across shared native presentation hosts. Standalone VIVID Creator continues owning its private Model and World directly and does **not** borrow a `ModelResourceCache` lease that it cannot legitimately acquire.

Creator teardown now synchronously drains its GPU actor and then releases MotionWeave ECS / AnimationGraph and copied pose owners **before** releasing the old head/hair Model resources, preventing stale borrowed pointers through the new animation lifetime contract. `HumanRigBinding::compose` now rejects any nonfinite input MotionWeave node-world matrix before modifying the last-good presentation pose. A negative regression source case was authored.

**Validation status:** source committed, but merged-tree native build, CTest, GPU/Creator preview and retirement tests have NOT RUN. Full transactional replacement upload, render-thread retirement performance and animation epoch integration in shared non-Creator asset hosts remain further work. Full source SHAs, handoff and next actions: `docs/agent-handoffs/vivid.md` Stage 7J.


## Stage 7K — two-phase Creator model and GPU morph publication (2026-11-20)

VIVID Creator now stages its replacement character before tearing down the old one. All CPU validation, new `AnimationGraph` and MotionWeave `World`, head/groom GPU uploads and selected `GpuActorStream` or GLES `CpuActorStream` creation occur on local candidate state. Only a fully prepared candidate can pass the old-character retirement barrier and publish its Model, ECS animation ownership, facial actor and hair/scalp runtime into the live viewport.

This fixes the earlier unsafe ordering where a new GPU buffer allocation could fail **after** the previous character was already destroyed. With an ordinary failed candidate upload and a working GPU idle barrier, the previous visible character is kept. Shutdown still detaches ECS animation owners before releasing their Model pointers.

**Not production-ready yet:** the operation still uses a synchronous GPU-wide idle barrier at the commit boundary. If the GPU cannot confirm idle after a failure, the candidate's native resource owners are deliberately retained/leaked rather than risking premature GPU destruction; proper device-loss quarantine/recovery and nonblocking retirement remain future work. Authored DNA edits are not rolled back, only the last-good visible GPU character is protected. Source committed but **no integrated native build, CTest, GPU failure injection or Creator visual test was run**. Exact source SHA and next-agent handoff: `docs/agent-handoffs/vivid.md` Stage 7K.


## Stage 7L — nonblocking, whole-human completion-token retirement (2026-11-20)

VIVID Creator can now replace the current character without a normal full-device GPU idle stall. A new `HumanRetirementQueue` owns the **entire old character** (head/hair Models and their GPU buffers, the active GPU/CPU morph Actor Stream, MotionWeave World and AnimationGraph) while an ordered GPU completion marker is pending. A verified completed token, or explicit successful idle fallback, is mandatory before freeing the old actor and Model resources. Pending or failed markers are never treated as successful just because frames elapsed.

To ensure completion-token submission happens **after** the GPU command buffer has been submitted, `platform::AppCallbacks` has a new optional `afterFrame` callback, invoked after `endFrame()` by macOS, Linux, Windows, iOS, Android and Web platform loops. Existing applications can omit the hook. Creator uses it to progress queued retirements; no GC GPU work is emitted from inside its rendering callback. This preserves the original single MotionWeave animation owner.

**Limits and release status:** the token-supported path is nonblocking during ordinary character swaps, but a backend with no completion-token support falls back to a synchronous idle barrier. Device-loss recovery, safe handling of a failed queue drain, GPU residency backpressure and benchmarked peak VRAM use remain open. Headless fake-RHI regression source and CMake registration are committed; **C++ compilation, all CTests, graphics backend runtime testing and device-lifetime acceptance have NOT RUN** for this stage. See `docs/agent-handoffs/vivid.md` Stage 7L for commits and risk details.


## Stage 7M — retired GPU residency backpressure (2026-11-20)

`HumanRetirementQueue` now reports pending old-character count, estimated bytes in pending resident Models and VIVID actor streams, historical high-watermark and blocked rebuild attempts. Creator applies a **three-retired-human / 512 MiB estimated-residency** default admission budget before allocating the next character candidate. When a prior GPU completion token remains unresolved, requests stay pending and the old visible character remains live; only a genuinely completed token or successful GPU idle fallback releases the old model and makes room for a subsequent rebuild.

These bytes are a bounded *source-derived estimate*, not driver-accounted VRAM: shared GPU allocations, memory layout overhead and transient two-character peak require device-specific profiling. A new fake-RHI regression for pending-count/estimated-byte limits and reopening admission on fence completion was written, but **not compiled or run**; no actual native Metal/Vulkan/GLES GPU performance measurements are claimed. Stage 7M details and source commit SHAs are in `docs/agent-handoffs/vivid.md`.


## Stage 7N — native bind-pose and skin inverse-bind authoring (2026-11-20)

A new `BindPoseRebind` stage applies authored local bone translation changes to a private CPU Model candidate before its AnimationGraph is compiled or vertex buffers are uploaded. It validates the complete skeleton parent chain, finite/non-singular bind transforms, skinned node palettes and vertex joint influences. It then recalculates the bound skin transforms using `inverse(newJointWorld) * oldJointWorld * oldInverseBind`, retaining original imported glTF/FBX mesh bind frames rather than replacing them with identity-based assumptions.

Creator exposes an **opt-in skeleton rebind** checkbox in Rig correction authoring. When enabled, existing `RigAuthoring` offsets rebind the candidate Model's bind nodes and inverse-bind matrices; they are *not* added a second time as MotionWeave pose overlays. Source FBX/glTF assets and the visible last-good character are preserved, and staged GPU retirement follows the existing two-phase publication workflow.

This is **actual skeleton/inverse-bind rebinding**, not joint-weight painting, shape sculpting or animation keyframe retargeting. Source vertices/UVs/morph vertex identities are unchanged; the rest-pose skin transform stays equivalent to the original while animated deformation reflects the edited skeleton. Missing/invalid skins fail closed. New regression assertions include an imported nonidentity mesh binding frame and invalid joint palette, but **native compilation, CTest and real GPU skinning preview have NOT RUN**. Exact commit SHAs, architecture and limitations are in `docs/agent-handoffs/vivid.md` Stage 7N.


## Stage 7O — Skin Weight Authoring for imported skinned meshes (2026-11-20)

VIVID Creator now offers model-relative vertex selection, named skeleton bone selection and individual target-weight authoring through `SkinWeightAuthoring`. An explicit 0..1 bone weight is redistributed across existing influences, reduced to at most four palette slots, and deterministically quantized to a 255-byte sum. Imported source vertex position, UV, normal, tangent, morph vertex IDs and other per-vertex attributes remain untouched. All edits are preflighted transactionally on a private un-uploaded character model, then published via the existing two-phase GPU actor replacement; a failed palette/skin edit preserves the last-good visible character.

Edits can be serialized to a versioned JSON sidecar and restored through Creator. Global vertices referenced by more than one skinned primitive require compatible skin+joint-palette mappings; ambiguous shared seams are rejected rather than changing weights under the wrong skeleton. The authoring API currently stores one named-bone weight adjustment per vertex (the latest per-vertex edit); it does not yet implement weight painting brushes, seam splitting or automatic bone assignment.

**Verification:** source tests added for U8 normalization, JSON roundtrip, invalid-palette rollback and conflicting-seam rejection; actual C++ compilation, CTest and GPU image parity are **NOT RUN**. See `docs/agent-handoffs/vivid.md` Stage 7O for source commit IDs, risk discussion and next-agent work.


## Stage 7P — viewport weight painting and skin diagnostic overlay (2026-11-20)

The existing `SkinWeightAuthoring` supports a transactional brush stroke over selected model-relative mesh vertices, using a configurable local-space radius and smooth falloff, target influence, brush strength and optional X=0 bilateral mirroring. One stroke is one undo/redo record; all affected vertices are validated before the new authored edit map is published, with a provisional maximum of 4096 vertices per stroke. The actual model is updated only by rebuilding a fresh private character candidate and passing the previous skin palette/influence validation, then publishing through the already integrated GPU-safe Creator actor replacement.

The Creator exposes a selected mesh, selected named joint, weight painting controls, an arm-and-click triangle ray-picking interaction, Undo/Redo, and a screen-space red/blue diagnostic heatmap of sampled original model vertex weights. **Accuracy limitation:** this overlay uses the mesh owner's node transform and does not yet reconstruct final skinned and morphed GPU vertices. GPU skinning-aware surface picking, continuous drag paint, spatial indexing for very dense meshes and actual per-vertex GPU-accurate heatmap are still open. Mirroring assumes local X symmetry; ambiguous shared seams still reject edits.

Source-level regression assertions cover symmetric brush influence, bounded falloff, undo/redo, invalid-stroke rollback and U8 weight normalization. The changes were committed but **not compiled, run or visually tested**; see `docs/agent-handoffs/vivid.md` Stage 7P for exact source commits and remaining work.


## Stage 7Q — skin-aware viewport picking and continuous drag weight paint (2026-11-20)

`SkinnedWeightPick.h` reconstructs the current mesh's CPU-skinned world-space positions from the authoritative MotionWeave node-world matrices, imported joint palettes, U8 vertex influences and `Skin::inverseBind` matrices. VIVID Creator uses these deformed triangles for its skin-paint raycast and projects the same staged positions for the sampled weight heatmap. The brush now supports holding and dragging the left mouse button; the in-memory series of dabs becomes a single Undo/Redo record at mouse-up, with full cancellation for invalid palette/skin strokes and no per-dab model upload.

The implementation preserves the existing single source skeleton evaluator and stable model-global vertex IDs. Limitations remain significant: GPU facial morph displacement and cloth deformation are not represented in CPU picking, and this is not a GPU depth/object-ID selection pass. Selected skinned mesh must have exactly one owning Model node and consistent primitive palette mappings; ambiguous instancing/shared seams fail closed. The current CPU scratch surface is recomputed while painting/heatmap is active, so high-poly acceleration remains an optimization task.

Tests for translated-joint picking, invalid-pose staging, multi-dab undo history and cancelled drag rollback have been added as source only. **No C++ build, CTest, GUI or GPU parity run has been executed.** Exact commits and next-agent handoff are in `docs/agent-handoffs/vivid.md` Stage 7Q.


## Stage 7R — authored skin-weight LOD transfer and palette seam diagnostics (2026-11-20)

`SkinWeightLodTransfer` now projects source skinned weights to existing authored LOD mesh vertices using the same validated barycentric `MorphLodMapping` used for facial morph authoring. It resolves source joint palette indices into stable skeleton node IDs, blends up to four incoming per-vertex weights at each projected source triangle, then maps the strongest four back to the destination LOD joint palette and normalizes them to exact U8 sum 255. It validates the mapping's source geometry fingerprint and target vertex set, stages all updated `ModelVertex` data and only commits after full validation. The Creator uses this before GPU upload for LODs whose source/target meshes are explicitly recorded in the imported package.

`SkinSeamAnalysis` provides non-mutating diagnostics of shared global vertices that resolve to incompatible bone palettes across skinned primitives. **Automatic seam splitting remains open**, since cloning shared vertices requires a coordinated update of geometry IDs, morph CSR, multiple LOD mappings and primitive ranges. No speculative vertex duplication is attempted.

Source regression cases cover reversed LOD palettes, barycentric weights, U8 sum-255, invalid LOD palette rollback and seam-conflict detection. **No C++ compilation, CTest, native GPU skinning comparison or Creator visual acceptance has been performed.** Exact commits and next-agent handoff are recorded in `docs/agent-handoffs/vivid.md` Stage 7R.


## Stage 7S — constrained skinned seam splitting and facial morph preservation (2026-11-20)

`SkinSeamSplit` now implements transactional **automatic separation of conflicting skinned primitive palettes** on a private pre-upload character candidate. It appends a new contiguous vertex range for the conflicting primitive, updates only its model-relative indices and vertex interval, and duplicates every applicable extended sparse facial morph delta to each new cloned global vertex ID. The original vertices and previously authored source IDs remain unchanged, so the tool avoids destructive remapping of imported authoring files. `SkinWeightAuthoring::withSplitClones` extends authored named-joint weight edits to the candidate clones without rewriting the source JSON or undo history.

The safe supported subset requires a unique skinned mesh owner, independent index ranges, CPU-owned source geometry and no legacy per-primitive morph atlas or precompiled material/shadow batches. Affected authored *target* LOD mappings that do not include the cloned new vertices still fail closed, rather than silently producing missing LOD morphs or skin weights. Full rebaking of legacy renderer morphs, target LOD sidecars, material and shadow batches is still outstanding.

Regression source asserts cloned primitive ranges, source ID stability, preservation of extended facial morph CSR deltas and normalized skin weights on source and clone. **No C++ compilation, runtime CTest or actual Metal/Vulkan/GLES GPU comparison has been performed for this stage**. Complete implementation SHAs, restrictions and next-agent steps appear in `docs/agent-handoffs/vivid.md` Stage 7S.


## Stage 7T — seam-clone target LOD correspondences (2026-11-20)

`SkinLodSeamRemap::expandTarget` now generates a temporary, sorted barycentric target map for newly appended skinned seam-clone vertex IDs. Each clone inherits its original target vertex's source triangle and barycentric weights, so `SkinWeightLodTransfer` can populate the clone's destination palette without changing saved LOD sidecars or existing source/target IDs. Creator carries the `SkinSeamSplit::Result` forward and passes this expanded map to transfer only for affected target meshes. Incomplete mappings reject the candidate, preserving the last-good character.

This is **not** legacy morph-atlas or material/shadow batching rebake. Those paths still fail closed rather than silently corrupting GPU draws; fully general automatic seam splitting remains incomplete. Source regression scenarios have been committed for cloned correspondence inheritance, stable IDs and missing-sample rollback, but **compilation, CTest and GPU validation have NOT RUN**. See `docs/agent-handoffs/vivid.md` Stage 7T.


## Stage 7U — legacy primitive morph CPU repack and skinned batch invalidation (2026-11-20)

The seam splitting authoring transaction now handles an additional previously rejected class of imported skinned assets: unuploaded legacy `Primitive::morphTargets/morphDeltas`. When a primitive is cloned, its legacy per-target position/normal delta array is validated against the original primitive's contiguous vertex interval and rebuilt in the new clone interval's local vertex order. Its `morphTexel` is reset; the **existing** `render::uploadModel` path constructs a fresh morph atlas after candidate publication. Independently, extended VIVID sparse morph CSR deltas are replicated to every cloned global vertex, as before.

Any cached shadow/material batch descriptors belonging to a seam-affected mesh are invalidated, preventing a stale batch draw from referring to the old primitive layout. This is safe for skinned meshes, which normal AIVERSE batch preparation intentionally excludes; it is **not** a rebuild of optimized material/shadow batches. Resident morph textures, shared GPU geometry pages, index-range aliasing and multi-owner skinned meshes are still refused.

A regression source case checks legacy morph data, batch invalidation and rejection of malformed morph strides without input mutation. **Native compilation, CTest and actual GPU atlas/skinning parity were NOT RUN**. See `docs/agent-handoffs/vivid.md` Stage 7U for implementation SHAs, API contracts and next-agent instructions.


## Stage 7V — bounded groom hair self-collision (2026-11-20)

The `HumanGroomRuntime` Verlet guide solver now offers opt-in **inter-guide self collision**. A CPU uniform-grid broadphase hashes movable groom guide particles and checks only neighboring cells, with a deterministic unique-pair rule and a fixed per-substep candidate budget. The Jacobi contact correction separates overlapping particles belonging to *different* guide strands, keeps roots pinned, and adjusts `previous` coordinates to avoid injecting artificial velocity. The previous closed-mesh BVH or procedural scalp ellipsoid collision runs again after these contacts.

Library users can call `setSelfCollision(GroomSelfCollisionConfig)` and inspect `selfCollisionStats()`; unchanged projects have self-collision **off by default**. Creator previews expose enable, strand separation, response, pair checks, contact count and budget diagnostics and enable the feature by default. Runtime behavior is still CPU guide-particle-level, not exact continuous segment/strand/card collision or GPU compute.

Source regression scenarios cover root pinning, repeatability, two-strand separation, finite positions, invalid configuration rejection and bounded candidate checks. **No C++ compilation, CTest, GPU Creator demo or performance benchmark has been run for Stage 7V.** Source SHA/architecture/handoff: `docs/agent-handoffs/vivid.md` Stage 7V. Remaining improvements: persistent spatial indexing, segment-segment narrowphase, clump friction, high-guide stress, and optical skin/eye/hair realism.


## Stage 7W — segment-level hair collisions, friction and curl preservation (2026-11-20)

The existing groom-local Verlet system now has an opt-in segment-capsule narrowphase in `GroomSegmentCollision.h`. Unlike the earlier node-to-node repulsion, it detects collisions where two guide *segments cross between sampled particles*. It uses an ordered spatial grid, bounded candidate-pair budget, deterministic Jacobi projections, and keeps root particles pinned. A configurable friction factor damps remaining Verlet velocity after contacts. `HumanGroomRuntime::step` then partially restores authored local rest curvature and segment lengths, and reapplies the existing scalp collision.

`GroomSelfCollisionConfig` appends `segmentEnabled`, `friction`, `bendPreservation`. The segment pass stays off by default for library consumers, while VIVID Creator previews enable it and expose sliders for all three controls. The additional CPU segment pass may be skipped when its capped 16,384-segment or 524,288 grid-membership budget is exceeded; in this case diagnostic `budgetHit` is raised without failing the complete groom update. Dense preview may therefore retain the earlier particle-only behavior.

This is bounded current-frame capsule collision, not swept continuous time-of-impact CCD, GPU hair compute, card-card intersection prevention, or physically calibrated wet-hair fiber friction. New headless test source covers crossing segments, pinned roots, friction validation, pair search caps and dense-skip behavior; **no native build, CTest, visual screenshot, Metal/Vulkan/GLES comparison or benchmark has been run**. See `docs/agent-handoffs/vivid.md` Stage 7W for source commits and next tasks.


## Stage 7X — reusable segment broadphase workspace (2026-11-20)

The VIVID hair segment-collision solver now uses a reusable contiguous broadphase rather than allocating a per-cell tree map and per-pair ordered set on every substep. Cell entries are sorted once and unordered segment pairs are deterministically deduplicated before distance evaluation. Each `HumanGroomRuntime` owns a `GroomSegmentWorkspace` retaining capacity for the next substep/frame; the old standalone `GroomSegmentCollision::resolve(guides,config,stats)` API remains available via temporary scratch for tests/tools.

The broadphase is bounded by 16,384 guide segments, 524,288 cell memberships and a provisional 1,000,000 raw-pair safety guard before sorting. When this optional pass would exceed the memory budget, it skips without writing any positions, reports `budgetHit` and allows the base Verlet/point/scalp collision path to proceed. This is a performance/allocations-oriented source change, **not an observed speedup**: no native compiler, CTest, GPU preview, hardware benchmark or profiler run has been executed. Regression source checks reuse/capacity, deterministic results and preserved pinned roots. Exact commits and handoff are in `docs/agent-handoffs/vivid.md` Stage 7X.


## Stage 7AJ — deformed source-vertex sculpt picking (2026-11-20)

VIVID Creator's imported morph sculpt selection now uses the currently
published sparse morph targets **before** applying the current MotionWeave
skin pose. The portable `DeformedSculptPick.h` builds a CPU reference
for the selected mesh's morphed bind-space vertex positions from the same
vertex-major sparse CSR offsets/links and active `FaceShapeBinding`
weights as the GPU actor. `prepareSkinnedWeightSurface` accepts those
optional positions and applies existing bone palettes and inverse binds;
the resulting triangle raycast still returns the **original source ID**,
rejecting generated/LOD vertices. No gameplay ECS, texture/RHI handles,
model vertex buffers, DNA or skeleton runtime are mutated. The standalone
three-/four-argument skin-weight picker path is preserved for existing
weight painting and heatmap features.

A viewport click is reconstructed only on user input, not continuously
in the render loop. The selected world-space sample marker is drawn in
that click frame for animated previews; paused previews can retain it.
If skin palettes, morph weights or node matrices are inconsistent, no
fallback to the misleading undeformed source mesh is attempted. A
`human_creator` regression-source fixture checks one 100%-weighted
joint, a 0.4 m morph displacement, source IDs, neutral weights and
invalid geometry/palette/pose rollback.

**Still incomplete:** matching the exact rasterizer's occlusion and
per-pixel visibility, GPU compute precision, cloth deformation,
topology-aware sculpt/brush falloff and real imported scan validation.
The CPU reference is bounded to 1M model vertices per click. It is a
correctly ordered presentation approximation, not proven Metal/Vulkan/
GLES pixel parity. **Native compilation, CTest, GPU/GUI screenshots and
performance benchmarks for this stage are NOT RUN.** See
`docs/agent-handoffs/vivid.md` Stage 7AJ for exact SHAs and next steps.


## Stage 7AL — reusable deformed picking workspace and refitted BVH (2026-11-20)

VIVID Creator's morphed+skinned source-ID picker now uses a persistent
`DeformedSculptWorkspace`. The original brute-force
`pickDeformedSculptVertex` remains a reference path for direct parity
tests. A selected mesh's triangle topology is median-partitioned once
into `SculptPickBvh` (up to 1,000,000 source/model vertices and 1,000,000
selected-mesh triangles). Each user click computes the current deformed
world-space surface from the published facial CSR and MotionWeave bone
pose, refits the BVH bounds, then tests ray intersections only against
candidate leaves. Picking still returns the source authoring ID after
Stage 7AK seam-clone mapping; nonauthorable LOD triangles are skipped.
Nearest-hit and equal-depth triangle tie-break semantics match the
previous brute-force reference.

The CPU scratch vectors for morphed positions, per-vertex skin palette
identity and staged world positions are owned by the Creator and reused
across clicks. Invalid input never publishes an incomplete replacement
world surface or bind-space morph buffer. A candidate Model/mesh
publication resets the BVH and invalidates cached model identity; the
same cache also rebuilds when the selected mesh changes.

This is bounded **CPU editing acceleration**, not a GPU ID/depth picking
pass. Skin/morph reconstruction remains O(model vertices) per click and
BVH refitting O(mesh triangles); source triangle BVH construction is
O(triangles log triangles) on first click. `SculptPickBvhStats` counts
nodes visited and triangles tested to support future performance profiling,
but no wall-time or memory-speedup measurement has been made.

The `human_creator` regression source compares BVH against the linear
reference for a 41×41 grid in neutral and active morph poses, checks
candidate filtering, capacity reuse, invalid pose rollback, recovery and
seam-clone/LOD source-ID behavior after topology reset. **Native compiler,
CTest, Metal/GLES/Vulkan capture and performance tests NOT RUN.** Exact
source commits and handoff: `docs/agent-handoffs/vivid.md` Stage 7AL.


## Stage 7AM — cached palette plan and opt-in CPU sculpt benchmark (2026-11-20)

The Stage 7AL authoring-time BVH/CPU picker now caches the selected mesh's
validated joint-palette and influence mapping in `SculptSkinningPlan`.
A single plan is built on the first click after a published imported model
or mesh selection, and invalidated by `DeformedSculptWorkspace::reset()`
when the Creator publishes a new character. Unlike the original
`prepareSkinnedWeightSurface` reference, subsequent picks skip
revisiting all triangle indices to discover each included vertex's
primitive palette and skip rebuilding global skin joint indices. Each
click recomputes the actual MotionWeave `jointWorld * inverseBind` matrix
**once per skin joint**, then evaluates each included model vertex using
the cached U8-normalized weights. The GPU morph-before-skin ordering, BVH
raycast and Stage 7AK original-source seam-clone mapping remain unchanged.

Invalid pose/skin/vertex data fails closed and never publishes an
incomplete CPU world surface. The cache is **not** a new animation
scheduler or deterministic ECS state, nor a renderer-wide material or
texture cache. In-place edits to model topology, skin weights, skin
palettes or inverse bind matrices require an explicit workspace
`reset()` by the owning editor.

`tools/vivid-creator/sculpt_pick_bench.cpp` is an optional standalone
CMake target (`AIVERSE_VIVID_SCULPT_BENCH=ON`), **not a CTest**, with
three synthetic rigid-mesh sizes (10,000 / 100,489 / 1,000,000 model
vertices). It captures cold + median warm click time, one legacy linear
reference time, BVH node/triangle counts and true hit-source/ray-distance
parity in JSON. The synthetic 1M-vertex case uses a checkerboard mesh
with fewer than 1M triangles to fit the existing editing budget.
New `human_creator` regression source compares one- and two-joint
skinning to the baseline across changed poses, checks transactional
failure/recovery and ensures changed palettes require a new plan.
Creator also reports measured CPU click milliseconds and narrowed
triangle count in its selection status.

**No new Stage 7AM build, CTest, GPU/Metal/Vulkan/GLES execution, or
CPU benchmark results have actually been run.** These are committed
source changes and a way to produce evidence on the target Mac, not
observed speedups. Correctness across actual imported scanned heads,
nonlinear/cloth deformation, renderer visibility and high-density
morphs remains to be validated. See the Stage 7AM handoff in
`docs/agent-handoffs/vivid.md`.


## Stage 7AN — GPU ID/depth source contract (2026-11-20)

The current render RHI supplies ordinary offscreen color/depth targets,
a screen capture and asynchronous **buffer** readback, but the VIVID
branch does **not** expose the stable, cross-platform asynchronous
offscreen **pixel** readback required to ship GPU depth-tested
selection. Implementing an RHI/Renderer3D fork inside VIVID would
duplicate functionality under construction in Texture RHI.

Instead the VIVID-facing ID/depth pass ABI has been implemented in
`SculptGpuPickContract.h` and `SculptGpuPickShaders.h`. It includes
reversible linear RGBA8 triangle-index packing (zero reserved for
background/nonauthorable occluders), a fixed 16-byte expanded triangle
vertex layout, GLSL ES300/MSL shaders and RHI shader/pipeline descriptor
helpers, and a versioned one-outstanding-request token gate which
rejects responses after model publication, viewport/frame changes,
or newer user clicks. CPU reconciliation uses the original
morphed-before-skinned source-ID resolver and intersects only the
GPU-selected triangle, including seam-clone mapping.

The existing `DeformedSculptWorkspace` can resolve an externally
verified GPU triangle ID only if its CPU surface is explicitly stamped
with **exact** captured `frameSerial` and `publicationSerial`.
An unstamped CPU click cannot be treated as a GPU-pose oracle.
All bounds, finite positions, invalid IDs, depth range and revision
checks fail closed. The Creator continues to use the tested-in-source
CPU BVH path; nothing in this stage allocates GPU ID targets or changes
Renderer3D's normal rendering.

**NOT IMPLEMENTED / NOT VERIFIED**: recording a true GPU ID/depth
draw pass, correct alpha/occluder submission and renderer-owned GPU
deformation, asynchronous offscreen color/depth pixel readback,
Metal/Vulkan/GLES shader compilation, actual camera/raster depth
parity or hardware tests. Source regression checks in
`vivid_human_creator` were added but NOT RUN. Do not advertise the
feature as full GPU picking until these gates pass. See Stage 7AN
handoff and `tools/vivid-creator/README.md` for exact API contract.


## Stage 7AO — Metal offscreen ID/depth pass (2026-11-20)

The Stage 7AN ID/depth ABI has advanced from source-only shader and
callback token definitions into an **opt-in real Metal offscreen pass**.
A new portable virtual method `gfx::Device::readTexturePixelAsync`
returns four RGBA8 pixel bytes on a later render-owner frame, or returns
`false` on an unsupported backend. The Metal implementation blits
one top-left-origin pixel to padded shared memory and converts
BGRA8 memory ordering to canonical RGBA8. It does not stall
the renderer to read the pixel.

`SculptGpuIdPass` creates the standard RHI ID/depth attachments,
GLSL/MSL ID shader and depth-tested pipeline, and records a
selected-mesh draw using the already morphed+skinned CPU reference.
The imported Creator supports **Experimental Metal GPU ID check**,
disabled by default. The existing CPU BVH result is immediately
available; a later GPU response confirms it only if the captured
click/frame/publication/viewport and source triangle agree.
GPU resources are not destroyed before completion; the owner
performs GPU idle retirement at shutdown. Hardware readback is
implemented **for Metal only**; on GLES3, GL33 and Vulkan, the
new RHI method explicitly returns unsupported and the CPU BVH
continues to work.

A real native macOS GPU test `vivid_metal_sculpt_id` now covers two
overlapping depth-tested triangles, a cleared ID pixel and the
asynchronous Metal pixel callback, with channel-order validation.
The test is registered in the VIVID CMake target and included in
the final one-pass validation script.

**Important:** this optional ID pass includes only the selected
mesh. It does not yet render unrelated scene occluders, hair,
transparent materials or exact alpha masks, and does not read the
actor's actual GPU-deformed vertex-override stream. It therefore
does **not** establish final-scene pixel visibility parity.
The CPU-derived expanded ID mesh can consume substantial memory.
Portability and mobile performance require further work on shared
Renderer3D/Texture RHI integration, not a second VIVID-specific RHI.
No C++ compiler, CTest, GPU test, Metal shader compile, pixel capture,
or actual performance measurement was executed by the assistant
on this stage's new source. See Stage 7AO in
`docs/agent-handoffs/vivid.md` for file/commit-level handoff.


## Stage 7AP — conservative scene depth occlusion (2026-11-20)

The experimental Metal GPU ID pass now receives occluding triangles
from all *currently drawn VIVID Creator preview geometry*: the selected
imported model mesh, other node-attached head/body meshes, the static
floor and any dynamically simulated procedural hair cards. Other
imported meshes use the published face morph CSR and MotionWeave pose,
with `prepareSkinnedWeightSurface` reconstructing their world-space
vertices. The floor retains its CPU `MeshBuilder` triangles and the
procedural groom uses the same current vertex positions and rigid-head
transform as the normal colour pass.

The shared depth-tested offscreen draw writes actual selectable IDs
only for fully opaque selected primitives. Other geometry writes
zero-ID depth; alpha-masked or translucent cards/cornea are
**conservative whole-polygon blockers**, without texture alpha
discard. Thus the GPU validation can deliberately fail to confirm
a CPU-selected vertex through an alpha hole; this does not fabricate
a visible GPU hit. This is a safe diagnostic policy, not exact visual
alpha parity. A failed/skipped other-mesh skinning reconstruction,
invalid geometry, missing source pose or exceeded triangle budget
fails closed to the established CPU BVH path. The existing callback
token must still match the captured mesh, frame, actor publication
and viewport dimensions.

New pure C++ regression sources assert material eligibility and
zero-ID depth of other meshes, procedural hair and floor. The real
Metal fixture adds closer/farther zero-ID scene blockers to the
selected depth-tested triangles. All new fixture and GPU work is
source-only so far: **actual compilation, CTest, driver/shader pixel
oracles and performance profiling have NOT RUN** on these commits.

Remaining gaps: exact per-material alpha masking and transparency
ordering, shared renderer GPU-deformed vertex override, true LOD
selection/culling, arbitrary external scene geometry outside Creator,
and cross-platform Vulkan/GLES async pixel readback. This stage does
not merge with Texture RHI or claim complete GPU-visible parity.
Handoff: `docs/agent-handoffs/vivid.md` Stage 7AP.


## Stage 7AQ — sampled GPU alpha masks and LOD safety (2026-11-20)

The experimental Metal ID/depth pass now supports per-fragment
alpha-cutout geometry for representable ordinary `AlphaMode::Mask`
materials. `SculptGpuAlphaMask.h` supplies paired GLSL ES300/MSL
fragment shaders which sample the **same GPU base-color texture**
and respect the existing material's UV scale, signed rotation and
offset, vertex-color alpha, `baseColor.w` and `alphaCutoff`.
The pass first renders opaque and zero-ID conservative occluders,
then renders supported mask batches against the **same hardware
depth target** with a fragment-level alpha discard. This permits a
source triangle behind a hole in a representable mask to survive,
while solid texels in nearer masks block it correctly.

ShaderGraph custom alpha, team blending, missing GPU base textures,
vertex-occlusion encoded alpha, partially blended cornea and other
unknown shader semantics still emit conservative whole-polygon
zero-ID blockers. The real model's exact ShaderGraph, all LOD and
custom GPU vertex streams cannot be inferred by a VIVID-specific
mask shader. `SculptGpuPickEligibility.h` returns a documented
rejection for authored LOD chains, missing or multi-owned selected
mesh and invalid node references, and the Creator only attempts GPU
confirmation for eligible scenes. Regardless of eligibility the
existing CPU BVH picking workflow remains active.

Native GLES3/GL33 now provide an opt-in `readTexturePixelAsync`
implementation through one-pixel PBO, GPU fence and polling on
subsequent device-owner frames, preserving GL FBO/pixel-pack state
and mapping top-left coordinates to native bottom-left GL pixels.
WebGL2 and the Windows GL loader return explicit unsupported;
Vulkan still has no offscreen pixel readback. Importantly, the
VIVID ID pass itself is still gated to Metal because the existing
native GLES Device does not expose the completed-slot-safe
`frameSerial` required by the Stage 7AN actor/pose response token
contract. No fake serial or synchronous fallback was introduced.

New source fixtures in `tests/vivid/human_creator.cpp` validate
mask material eligibility, geometry batching, shader/pipeline
attributes, UV and alpha factors, conservative fallback for
unknown inputs and authored-LOD rejection. The native
`vivid_metal_sculpt_id` regression fixture adds transparent and
opaque single-texel GPU mask samples to the original depth and
scene-occlusion checks. **These checks have not been compiled or
run in this stage.** No shader result, timing, GPU parity, Vulkan
or GLES native execution is claimed. See the detailed Stage 7AQ
handoff for changed files, commits and remaining requirements.


## Stage 7AR — Vulkan/GLES native GPU ID readback and CPU-source parity (2026-11-20)

The experimental ID/depth pass from Stages 7AO–7AQ is no longer
hard-coded to Metal. It may now initialize on **Metal, Vulkan and native
GLES3/GL33** RHI devices. `SculptGpuIdPass::record` still requires a
nonzero active device frame serial and records the current actor
publication, selected source mesh and viewport in a one-click async
request token. The CPU BVH result remains immediate and authoritative;
a matching GPU callback only corroborates the **CPU-fed snapshot**,
and a differing ID cannot silently replace the source vertex.
This is deliberate while the ID pass does not use Renderer3D's
`ModelDraw::vertexOverride`/real visible LOD/ShaderGraph vertex
program. The normal editing workflow and default `gpuSculptEnabled=false`
are unchanged.

The Vulkan backend now implements true asynchronous
`gfx::Device::readTexturePixelAsync` for single RGBA8 linear
offscreen target pixels through a 4-byte host-visible coherent
staging VkBuffer, `vkCmdCopyImageToBuffer`, explicit transfer-to-host
memory barriers and the existing frame-slot fence completion
timeline. Only a **signaled GPU frame fence** permits delivering bytes
on the later device-owner `beginFrame`. Failure reports empty bytes
and never publishes an unverified serial as completed. Vulkan's
`frameSerial()` exposes the current submission identity only while
the frame is recording, not completion proof. Top-left RHI readback
coordinates are converted to the current GL-oriented Vulkan
render-target storage.

Native GLES3/GL33 now exposes a frame-local monotonic serial,
complementing the already implemented per-pixel PBO/GLsync
readback. The real fence, not the counter, authorizes the callback.
WebGL2/Emscripten and custom Windows GL loader still return
unsupported and retain CPU BVH. The GPU ID pass is only an opt-in
diagnostic and a successful test pixel is **not** evidence of
whole-scene Renderer3D parity.

Two independent real-hardware CTest fixtures have been added:
`vivid_vulkan_sculpt_id` (offscreen Vulkan/no window) and
`vivid_gles_sculpt_id` (native EGL GLES3 pbuffer). Both verify
near/far hardware depth, clear RGBA8 and a closer independent
zero-ID scene occluder with async readback; they return CTest SKIP
77 if the native driver/context is unavailable, and FAIL if a
working GPU supplies the wrong result. The macOS Metal fixture is
unchanged. **All new source/GPU tests remain NOT RUN.** The user
requested one consolidated test pass only after feature closure.
See `tools/vivid-creator/README.md` and the detailed Stage 7AR
handoff for independent Linux/Vulkan/GLES commands, exact commit SHAs
and remaining renderer-stream integration work.


## Stage 7AS — real Renderer3D source geometry provenance (2026-11-20)

The opt-in VIVID GPU ID/depth diagnostic now queries the actual
Renderer3D source view rather than reconstructing visible meshes
and LOD decisions independently. The genre-neutral renderer gained
`Renderer3D::modelViewProvenance(const Model&)`: a lightweight,
read-only, **current-scene** record derived directly from the
`ModelItem` primitive list after `drawModel()` has applied
actual source frustum culling, selected mesh/LOD, batching,
skin/morph and `ModelDraw::vertexOverride` decisions.

Each record includes the actual renderer-selected mesh and
primitive index range, owning node, one invocation identity per
`drawModel()` submission (distinct model instances can share
mesh/node IDs), GPU override handle and count, alternate LOD,
GPU-conditional LOD, fade, batching, baked skin and
ShaderGraph vertex-offset flags. It is a **pre-render submission
snapshot**, not a promise that a later GPU pipeline or postfx
shows a particular pixel. All borrowed model/handle metadata is
invalidated at the next `beginScene()` or candidate publication;
it is not persisted into gameplay ECS or save files.

`SculptGpuRendererView.h` validates that snapshot against the
immediate CPU pick's original source mesh, frame and every source
primitive index range. It accepts an asset that *contains*
authored LOD levels when Renderer3D actually selected LOD0
without fade and still draws the original source index ranges.
It refuses alternative selected meshes, missing/incomplete
primitives, multi-instance reuse, GPU conditional LOD, material
batches, unsupported vertex shader offsets, baked deformers,
unknown node ownership and mismatched frame. Other visible
model mesh occluders are taken from the renderer snapshot
instead of walking all nodes and accidentally introducing
invisible GPU depth blockers.

Creator now stages its normal floor/head/hair `drawModel`
calls **before** the experimental ID draw (the regular
`renderer.render` still owns actual colour/shadow execution).
Its immediate CPU BVH selection stays authoritative: the
GPU ID from a CPU-expanded pose may corroborate that selected
vertex only, and the UI specifically states when the main
Renderer3D draw used an actual 60-byte GPU `vertexOverride`
stream that the ID pass has **not** matched. Late or canceled
asynchronous GPU callbacks no longer overwrite a newer CPU
selection's status message. Nothing here secretly enables
a full GPU-only vertex stream picker.

New unit fixtures in `tests/vivid/human_creator.cpp` cover
exact node/mesh/primitive ranges, same-frame identity,
actual renderer-selected LOD0 eligibility, alternative
LOD/fade/batching/ShaderGraph rejection, single vs multiple
Renderer3D instance ownership and preserved GPU override
provenance. The existing real Metal
`tests/vivid/renderer_preview.cpp` now queries the renderer
during a single-actor preview and checks that its GPU actor
vertex override is actually represented in the selected
model source view.

**Native compiler, CMake/CTest, Metal/Vulkan/GLES shaders,
actual GPU ID pixels and performance testing remain NOT RUN**
on Stage 7AS. The real GPU vertex-buffer/deformation and
post-culling visible command stream still need a fully
fenced Renderer3D-owned integration, plus isolated Texture
RHI conflict resolution before main merge. See the 7AS
handoff and Creator README for exact API and limits.


## Stage 7AT — Metal ID rendering from the actual GPU actor vertex buffer (2026-11-20)

The opt-in VIVID GPU picker on Metal no longer needs to reconstruct
the selected character's vertex *positions* on CPU when the normal
Renderer3D colour view has an active 60-byte
`ModelDraw::vertexOverride`. After the renderer-owned
Stage 7AS source mesh/primitive/LOD/override provenance passes,
`SculptGpuVertexSource.h` builds only original source vertex
indices, triangle ordinal IDs, immutable local-to-skin influence
resolution and the current MotionWeave joint matrices. The
Metal compute kernel `kSculptGpuSourceExpandMSL` reads the
**same current-frame GPU `ModelVertex` buffer** that Renderer3D
will draw, gathers the already-morphed positions at the exact
60-byte ABI stride, applies rigid source world or the current
joint palette, and writes GPU-resident 16-byte triangle-ID
vertices. That output is drawn directly in the existing
offscreen RGBA8+depth pass; no GPU source vertex readback
or CPU source position stream re-upload is required.

`SculptGpuIdPass` owns the optional Metal compute pipeline
and GPU-generated index/skin/matrix/output buffers, but **does
not own the actor's borrowed source handle**. Every temporary
GPU buffer remains alive until the frame's asynchronous pixel
callback is complete or an actual owner-thread GPU-idle barrier
permits retirement. Exact primitive index order, original
source IDs (including seam clones), model vertex count and
skinning data are validated before the compute dispatch. A
missing/unrepresentable GPU source fails closed to CPU BVH.

For scenes without a GPU vertex override, the earlier CPU-fed
ID diagnostic remains supported on Metal, Vulkan and native
GLES3. The new true GPU-source expansion is currently
**Metal-only**: Vulkan/GLES native one-pixel readback does
not imply that a native GPU expansion shader exists there.
Selected masked/custom ShaderGraph source primitives,
ambiguous/skinned palette input, non-selected authored LOD,
and other unsupported renderer source decisions continue to
reject GPU confirmation rather than fabricate provenance.
Other Creator occluders still use staged CPU reference
positions as in previous stages.

New pure C++ regression *sources* validate GPU ID input
topology and 60B override count, seam-clone/LOD mapping,
one-/two-joint weights, matrices and invalid-input rollback.
The native `vivid_metal_sculpt_id` GPU fixture now explicitly
compares a moved GPU source buffer against an unchanged CPU
reference, plus a skinned GPU source with translated joints,
to demonstrate that the ID path uses GPU buffer positions
rather than stale bind-pose/CPU morph coordinates.

**Actual compiler/CTest/Metal shader execution, GPU pixel
capture, Vulkan/GLES driver and full acceptance were NOT RUN
on Stage 7AT.** A matching source buffer, joint data and
pre-render primitive provenance still do not guarantee final
Renderer3D colour-pass pixel parity under custom shader
displacement, depth-after transparency, GPU command culling,
postfx and material selection. A complete cross-platform
runtime selection pass and polished topology-aware editor
tools remain subsequent work. See the dedicated 7AT handoff.


## Stage 7AU — Metal GPU-source alpha-tested selected faces (2026-11-20)

The opt-in GPU ID diagnostic now supports **homogeneous standard sampled
`AlphaMode::Mask` selected meshes** when Renderer3D's current-frame source
provenance points at the real VIVID GPU actor `ModelDraw::vertexOverride`.
Previously the sampled alpha fragment shader only received CPU-reconstructed
selected triangle positions. The masked variant now gathers already-morphed
60B `ModelVertex` positions from that exact borrowed GPU buffer, applies the
same Stage 7AT MotionWeave joint matrices, and writes a 32B selected ID vertex
(`packed_float3 world`, RGBA8 ID, `float4` UV/vertex alpha). The existing
Metal mask shader samples the actual model base texture using its validated
UV scale/rotation/offset, base alpha and cutoff and discards invisible
fragments with native GPU depth testing.

`prepareSculptGpuVertexSource` exposes `maskedSelection`,
`maskBaseTexture`, `maskUniform` and `maskInputs` (16B per original
vertex, not positions). Only a live ordinary base texture with a supported
Mask material is accepted. Mixed opaque+Mask, different Mask materials,
transparent/Blend, custom ShaderGraph, unsupported occlusion and malformed
source UV/vertex count/palette fail closed. Both opaque and masked
GPU-source paths preserve the original source triangle/author-ID mapping
and existing async GPU completion/retirement contract. Other occluders
continue using the conservative CPU-fed opaque/Mask geometry. VIVID
Creator's CPU BVH selection remains the sole authoring authority.

**Status:** source modifications and C++/Objective-C++ regression fixtures
were committed. Connector-side code/ABI inspections were performed.
**CMake, native C++ build, CTest, MSL shader compilation, real Metal GPU
pixels, Vulkan/GLES driver tests and full acceptance: NOT RUN.** The native
Metal fixture is intended to distinguish a moved actual GPU actor source
from unchanged CPU reference geometry under sampled Mask, and transparent
versus opaque texture texels; those expectations are not measured results.

This is NOT alpha parity for material-graph displacement, blended
cornea/depth-after passes, GPU-culling/ICB/MDI, selected alternative
LOD, nonselected GPU-deformed occluders or exact Renderer3D colour depth.
Metal is the only native backend with this GPU stream expansion;
Vulkan/GLES3 still have CPU-fed diagnostic selection. The broader human
skin/eyes/groom fidelity and topology-aware sculpt remain open. Do not
merge overlapping Renderer3D/Texture RHI work to main without an isolated
semantic integration and real hardware tests. See
`docs/agent-handoffs/vivid-7AU-gpu-mask-2026-11-20.md`.


## Stage 7AV — GPU-source skinning arithmetic parity with native Renderer3D (2026-11-20)

The Stage 7AT/7AU Metal GPU ID kernels now use the **same 3-row × 4-float
skinning matrix semantics and operation order** as the native
`ModelShaders.h` `SKINNED` vertex deformation. Instead of first
multiplying each skin matrix by the selected model-world transform
and then blending transformed positions, VIVID now builds
`nodeWorld[joint] * inverseBind[joint]` in *model space*, converts
matrices to exact 48-byte `SculptGpuSkinRows`, accumulates weighted
`float4` rows, computes three row–position dot products, then applies
the Renderer3D-selected `I.model` world matrix. Both the 16-byte opaque
and 32-byte alpha-masked GPU output variants use this same arithmetic.

The source preserves original UNORM8-to-float vertex weights and normalises
them in the GPU kernel, as Renderer3D does. Per-primitive palette sizes
larger than `kMaxPaletteJoints = 80` fail closed, because the normal
Renderer3D 4 KB joint uniform block cannot represent them. Source
preparation retains its transactional last-good inputs on errors.

The Creator now requires all selected Renderer3D source primitives
to have one identical model-world matrix and skinned/unskinned mode,
as well as one actor GPU override; otherwise CPU BVH remains authoritative.
The selected sampled-mask path further verifies that the GPU expansion
uses precisely the original indexed source UV/vertex alpha; an outdated
mapping cannot claim GPU visibility.

New unexecuted unit fixtures compare nonuniform two-joint U8 weight
blends and a nonidentity world transform to the Renderer3D row algebra
and check its MSL shader contract. Metal GPU fixture sources add a
two-joint position-shift / inverse world-shift pixel oracle and reject
stale sampled-mask UVs before GPU submission. Static source review
checks passed on the GitHub branch. **Native C++/ObjC++ compile,
Metal shader generation, GPU pixel capture, CTest and full AIVERSE
regression: NOT RUN.** These source checks cannot establish actual
numerical/pixel parity until the hardware suite executes.

Remaining: renderer-owned actual final colour submission/depth parity,
per-primitive alpha material/final colour occluders, GPU ICB/MDI
culling, nonselected GPU-deformed occluders, ShaderGraph displacement,
Blend/cornea and active LOD correspondence. Vulkan and GLES3 still lack
the true GPU actor source expansion, despite their existing CPU-fed
diagnostic ID support. Independent native driver testing and wider
MetaHuman-level authoring work remain separate milestones. See
`docs/agent-handoffs/vivid-7AV-renderer-skin-parity-2026-11-20.md`.


## Stage 7AW — renderer-encoded main-colour source provenance (2026-11-20)

The VIVID Metal GPU ID diagnostic now requires **two independent source
contracts**: (1) the current frame's staged Renderer3D
`modelViewProvenance()` (original mesh, primitive, actual LOD, actor
GPU vertex override); and (2) a new opt-in renderer-owned record of
**actual main-colour draw calls encoded by Renderer3D**, obtained
only after `Renderer3D::render()`. The new
`trackModelSubmissions(model)` and `modelSubmissionProvenance(model)`
APIs collect only the explicitly selected model's commands. Each
records source mesh/node/primitive/draw invocation, the actual index
range, 60B vertex override handle/count, renderer world transform,
skin and material classification, compiled pipeline handle, pass
and dispatch class (`Direct`, `DirectInstanced`,
`IndexedIndirect`, `GpuIndirect`, `GpuIcb`). A newly
begun scene clears these records; a late opt-in marks the result
truncated rather than pretending earlier submissions were captured.

`SculptGpuSubmittedView.h` checks the renderer's staged source against
the accepted opaque main-colour commands **one-to-one** with the same
frame serial and original source identity. Only an actually encoded
single direct indexed draw with a valid pipeline is currently eligible.
Pipeline-warming without a draw, mismatched index range/world/skin,
instancing/ICB/MDI/conditional GPU LOD, missing material source,
a non-main pass or an incomplete tracked scene fails closed. GPU
ID confirmation never overrides the previously CPU-BVH-selected
source vertex.

Creator now invokes its optional ID diagnostic **after** the real
Renderer3D render. Because the renderer deliberately leaves the main
pass open for UI, Creator temporarily ends that pass only when the
renderer/scene preflight succeeds, records the ID compute and
offscreen RGBA8/depth pass, and resumes the original pass with
`clearColorEnabled=false`, `clearDepthEnabled=false`, full
viewport and full scissor. The original selected actor GPU stream
remains borrowed only for the current RHI frame. Other actors and
engine worlds pay no per-model tracking cost unless they opt in.

The source-only `vivid_human_creator` tests now cover missing accepted
commands, pipeline pending, wrong frame, mismatched transform/index
and indirect/ICB rejection. The existing real Metal renderer preview
fixture now checks that its actor GPU override has an encoded direct
main-colour draw and matches the staged original model view.

**Actual verification:** GitHub reads/writes and 16 static source
contract checks. **Native C++/ObjC++ build, CTest, GPU shader
compile, real Metal pixels, frame timing/VRAM, Vulkan/GLES and full
`bash tools/vivid-validate.sh --full`: NOT RUN.** This validates
source-level submitted-command identity, **not** a completed GPU
fragment, final depth/colour pixel or physical GPU-fence parity.
An actual final colour/depth ↔ GPU ID pixel oracle (with another
GPU-moved alpha-masked occluder) is a distinct upcoming stage.
Other GPU-deformed occluders and final material/Prisma depth may
still disagree. See
`docs/agent-handoffs/vivid-7AW-accepted-draw-provenance-2026-11-20.md`.


## Stage 7AX — actual single-actor Metal colour/ID pixel coverage probes (2026-11-20)

Stage 7AW proved only that the normal Renderer3D **encoded** an eligible
colour draw. Stage 7AX adds a real Metal **GPU pixel test source** to the
existing macOS `aiverse_vivid_renderer_preview` acceptance. Its
one-actor normal renderer screenshot and the optional VIVID RGBA8
triangle-ID target are now sampled at the **same projected head-centre
pixel in the same render frame** for (1) GPU facial morph and
(2) MotionWeave-animated actor frames. The actual 60-byte
`ModelDraw::vertexOverride` position source is used by the GPU ID
compute (not a CPU-morphed position surrogate).

Before the ID draw, the native preview demands an exact
`modelViewProvenance` ↔ `modelSubmissionProvenance` match, an
available Metal GPU-source compute shader and the current GPU actor
buffer. It first records and presents the real Renderer3D colour pass,
then submits its offscreen GPU ID compute/raster/readback outside
the colour pass. Both results arrive asynchronously: the screenshot
callback and the independent RGBA8 pixel callback are correlated
with the immutable stage/request/frame/viewport coordinates and
checked only after **both** have arrived. Timeout, stale frame,
empty native GPU reply or absence of an actual vertex override fails
the acceptance; pending GPU scratch remains owned until its callback
or a verified device-idle barrier. The 12-actor crowd preview is
unchanged and does not run the one-actor parity probes.

`SculptGpuColorPixelOracle.h` makes the comparison explicitly a
**coverage test**. A nonzero triangle ID and a displayed colour
pixel visibly different from the presentation clear colour yield
`Covered`; a nonzero ID at clear-colour screen is `Mismatch`.
A nonzero scene-colour pixel with ID zero is **inconclusive**, not
automatically a mismatch, because another opaque/alpha-masked
object can legitimately hide the selected vertex. RGB bytes are
not compared directly with encoded triangle-ID bytes and this
does **not** test numerical depth-buffer equality.

The separate production-Metal `vivid_metal_sculpt_id` fixture source
now tests an *unrelated* nearer, GPU-sampled zero-ID Mask triangle
in front of the source: an alpha-hole texel must preserve source ID,
while a solid texel must suppress it through native depth testing.
This tests negative alpha/depth **inside the real Metal ID pass**,
but the extra occluder is CPU-geometry-staged, **not** a shared
GPU-deformed occluder from a final Renderer3D colour pass.

`tools/vivid-validate.sh` demands actual log records for both
new same-frame one-actor probes in addition to the three original
Renderer3D screenshots. Source-only unit fixtures validate
`Covered`, `Mismatch`, `ClearBackground` and
`Inconclusive` outcomes. Existing GPU scene selection and HumanDNA
asset contracts remain unchanged.

**Actual verification:** GitHub connector reads/writes and static source
inspection. **NOT RUN:** CMake, ObjC++/C++ compile, the native
`vivid_human_creator` and `vivid_metal_sculpt_id` CTests, the
GUI preview, Metal shader/raster/readback, full
`bash tools/vivid-validate.sh --full`, Vulkan/GLES tests, or performance
and VRAM measurements. These are executable test *definitions*,
not verified real-hardware pass claims.

Still open: compare a final **Renderer3D colour plus depth target**
against the VIVID ID pixel under an unrelated, independently GPU-deformed
sampled Mask occluder. RHI currently exposes only RGBA8 offscreen
pixel readback, not arbitrary native depth readback, so true numerical
depth parity requires an explicit RHI-safe depth encoding or an owned
shared-depth ID target. Final Blend/cornea/ShaderGraph/LOD/ICB parity,
Vulkan/GLES actor GPU vertex expansion, and complete MetaHuman-grade
authoring/appearance are separate incomplete milestones. See the
dedicated Stage 7AX handoff for exact files and next steps.


## Stage 7AY — native Metal Depth32Float parity and independent GPU-sourced Mask blocker (2026-11-20)

Stage 7AX compared a real presented Renderer3D colour pixel against a
source-GPU-vertex ID pixel, but could not read actual depth. Stage 7AY
adds an optional `gfx::Device::readTextureDepthPixelAsync` RHI method:
with a valid Depth texture handle it reads that actual offscreen
attachment; with an invalid handle it explicitly selects current
framebuffer screen depth. Metal implements asynchronous single-pixel
`MTLPixelFormatDepth32Float` blit to a 256-byte-aligned shared staging
buffer. Its callback is delivered on the gfx owner thread at a later
`beginFrame`, with a failure boolean and an actual finite [0,1] GPU
depth sample. Other backends use the default unsupported/fail-closed
implementation, never CPU-projected depth masquerading as a GPU result.

`SculptGpuIdPass::record` accepts optional `captureDepth=false`
without breaking existing callers. `Completed` exposes
`depthValid` and `depth01` only after both asynchronous colour
RGBA8 and depth callbacks have arrived. GPU scratch buffers are
retained until those callbacks or a verified GPU-idle barrier on
failed submission/shutdown. The one-actor Metal
`aiverse_vivid_renderer_preview` now gets **both** the native
Renderer3D main-screen Depth32Float and the independent GPU ID
depth for the same source frame and projected head-centre pixel.
Normal colour is also copied **during the exact source frame**
through Metal `copyColor(screen, RGBA8 target)` and asynchronous
single-pixel RHI readback; its older full-frame
`captureNextFrame` is retained only for visual screenshot A/B
and is not used as evidence of same-frame pixel parity.
`SculptGpuColorPixelOracle.h` adds a pure depth comparison that
rejects NaN/Inf, out-of-range/clear values, and depth differences
larger than the current test tolerance of 0.003. GUI acceptance
requires real `depth_scene`, `depth_id` and delta fields, not
just a claimed draw submission.

A second optional
`SculptGpuIdPass::record(..., captureDepth, gpuMaskOccluder)`
input permits one **independent sampled Mask object** to use
its own borrowed current-frame 60-byte GPU vertex stream for
occlusion in the ID pass. The separate Metal compute dispatch reads
that object's actual GPU positions, original topology/skin palette
and immutable UV/alpha, then zeroes every editable triangle ID
before emitting 32-byte Mask output. Native sampled alpha discard
and hardware depth testing now determine whether that second
object obscures the first. GPU scratch and borrowed source lifetime
follow the existing asynchronous retirement policy. The pure
`sculptGpuMaskedOccluderInputsValid` gate rejects identical actor
source handles, invalid indices, masks, palettes, UVs and ranges.

The existing `vivid_metal_sculpt_id` fixture now defines real
Depth32Float numeric assertions for the nearest selected triangle,
a front opaque blocker, sampled-alpha hole and solid-mask blocker.
It additionally defines a **two-distinct-GPU-vertex-buffer**
scenario with the masked object's GPU positions first displaced
outside the click while its CPU bind triangle remains in view,
then restored under opaque/hole texels. The test input buffers
are CPU-uploaded 60-byte GPU-resident vertices to isolate the GPU
gather behaviour; the fixture does not yet prove that the second
buffer was written by an actual deformation compute kernel.
The production path can borrow a GPU-written actor buffer.

Two existing GPU Mask bugs were also corrected: (1) selected Mask
index-count validation now includes mask triangles rather than
comparing only against opaque triangles, and (2) the GPU-selected
Mask output counts toward the non-empty/bounded scene when its
old CPU geometry was intentionally omitted.

**Validation status:** 22 static source-contract inspections matched
the expected code. **Native CMake, C++/ObjC++ compile, Metal shader
compilation, actual GPU depth/pixel comparisons, GUI smoke, CTest,
Vulkan/GLES driver tests, VRAM/performance and full
`bash tools/vivid-validate.sh --full` were NOT RUN.** In particular,
the native RHI depth blit behaviour and real pixel tolerance are
still awaiting macOS hardware acceptance.

**Remaining boundary:** A true end-to-end test must draw the
independent moving alpha-mask object through the **normal Renderer3D
colour/depth pass as well as VIVID ID**, using the same validated
current-frame renderer-source provenance and GPU deform output. The
current two-actor alpha test exercises the actual Metal ID/depth pass
only. Do not infer final Renderer3D occluder parity, Blend/cornea,
ShaderGraph displacement, ICB/MDI, alternative LOD, Vulkan/GLES3
GPU-source expansion or MetaHuman-grade realism from it. See the
Stage 7AY dedicated handoff.


## Stage 7AZ — two independently GPU-deformed models in normal Renderer3D + ID (2026-11-20)

The native one-actor VIVID Metal visual fixture gains a **separate
alpha-masked occluder Model** and a second genuine Metal compute
vertex-stream producer. The host first draws the usual selected
VIVID GPU-morphed human, then the independently GPU-deformed mask
actor through **normal `Renderer3D::drawModel` colour/depth**.
The same GPU-written 60-byte `ModelVertex` source of the mask actor
is passed into the existing 7AY GPU-ID zero-ID occluder compute
path. The immutable CPU bind mesh is not rewritten with the
per-frame delta. Only the test fixture enables this opt-in mode
via `AIVERSE_VIVID_MASK_PARITY=1`; normal Creator and crowd
rendering remain unchanged.

`Renderer3D::trackModelSubmissions(selected, occluder)` now accepts
an optional second Model pointer, preserving the old single-model
call. Its bounded per-frame accepted-command log stores both
sources, and `modelSubmissionProvenance(model)` filters the recorded
commands by the requested Model. Both pointers are cleared at
`beginScene`, and late tracking still fails closed.
`sculptGpuPairedRendererViewsAccepted` validates each main-colour
draw through the already strict original source, current frame,
direct index/LOD/vertexOverride and pipeline gate; it requires
distinct actual GPU source handles. If the second Mask actor is
missing, culled, batched unexpectedly, using an alternate source,
stale or compiled without a usable pipeline, the one-pixel
corroboration is rejected before an independent GPU-ID draw.

The Metal fixture creates two immutable 1×1 RGBA8 base textures
with exactly the same colour but alpha 0 or 255, and a
conventional double-sided `AlphaMode::Mask` model with a
real renderer-owned material/UV/cutoff. During morph-stage
(stage 1), its producer writes an offset +0.075 world-X into
a distinct 60-byte vertex buffer and selects the alpha-hole
texture; the normal colour renderer should show the original
selected human behind the discarded mask fragment, with a
nonzero selected GPU-ID triangle. During MotionWeave animation
(stage 2), the producer writes a different -0.065 X offset,
selects the solid texture and the mask actor should visibly
occlude the source. Both native main colour/depth and VIVID
offscreen ID/depth must then agree on the same closer
GPU-written Mask triangle and yield **selected ID zero**.

The same frame is checked with the 7AY exact-frame
`copyColor`/single-pixel RGBA8 readback and actual hardware
`Depth32Float` readbacks from both colour and ID depths.
The existing `compareSculptGpuColorIdCoverage` reports
`Inconclusive` for any coloured pixel with ID zero; Stage
7AZ interprets that as an expected Mask occlusion **only
after** two separate direct renderer submissions, distinct
current-frame GPU streams, the explicitly known alpha-solid
texel, and measured same-depth comparison all validate.
This does not make unrelated unknown occluders positive hits.

`tools/vivid-validate.sh` adds a separate GUI process
with `AIVERSE_VIVID_MASK_PARITY=1`, three actual
Renderer3D screenshots and required `stage=1
mask=alpha-hole` / `stage=2 mask=solid-occlusion`
GPU coverage/depth log entries. The original single-actor
and 12-actor crowd modes remain intact. A portable synthetic
fixture tests that a second renderer source with a shared
GPU buffer, stale frame, absent accepted draw or altered
index range cannot claim 7AZ parity.

**Status:** implementation, test sources, GitHub commits and
23 source-only contract inspections are complete. **NOT RUN:**
C++/ObjC++ build, native Metal compute/pipeline compilation,
GUI screenshots, GPU pixel/depth readbacks, CTest,
`bash tools/vivid-validate.sh --full`, Vulkan/GLES device
tests and performance. No hardware PASS or main-merge
claim is made here.

**Limitations:** This is a bounded, non-instanced, single
screen-pixel alpha-hole/solid scenario, not general automatic
GPU culling/LOD/transparency/multiple-occluder parity.
The mask model's GPU buffer is written by a small fixture
compute shader rather than the production facial morph
algorithm; it nevertheless uses the normal Renderer3D
colour path and the same actual GPU-produced buffer in
the ID path. Arbitrary ShaderGraph displacement, cornea/Blend,
late compositing, GPU ICB/MDI, alternate LOD, skinned
unrelated occluders and Vulkan/GLES true GPU-source
readback remain open. A full native Mac acceptance and
isolated semantic integration with concurrent renderer
and Texture RHI work are required before merging.
See `docs/agent-handoffs/vivid-7AZ-second-actor-mask-parity-2026-11-20.md`.


## Stage 7BA — bounded renderer-verified multiple GPU Mask occluders (2026-11-20)

Stage 7AZ verified source code for one independently GPU-deformed
sampled-alpha scene blocker in both normal Renderer3D colour/depth and
VIVID's ID/depth pass. Stage 7BA expands the optional diagnostic to
**one selectable Model and up to four independent GPU-deformed Mask
occluder Models** with conservative current-frame provenance.

`Renderer3D::trackModelSubmissionsMany(std::span<const Model* const>)`
records a bounded maximum of five models (the selected actor and
four scene blocker candidates). Duplicate/null/late/over-capacity
registration fails closed; source pointers and accepted colour
commands are cleared at `beginScene`. Existing
`trackModelSubmissions(selected)` and
`trackModelSubmissions(selected,blocker)` calls still work.
A per-Model `modelSubmissionProvenance(model)` result contains
only that Model's real accepted commands.

`SculptGpuRendererMaskSources.h` is a new portable, renderer-owned
**candidate filtering layer**. The host supplies the scene's known
visible candidate Models and their current MotionWeave poses;
the collector determines each actual visible primitive, original
index range, live 60-byte GPU override, transform/skin/LOD and
successful native direct main-colour submission exclusively
through Renderer3D. Only homogeneous standard sampled
`AlphaMode::Mask` actors may use this exact GPU Mask route.
Duplicate models, missing submitted draws, unsupported
ShaderGraph/Blend/LOD/materials, stale frames or over-limit
candidate lists invalidate the whole output transaction, rather
than silently omitting an occluder and claiming a false-positive
selectable ID.

Renderer3D now freezes each sampled mask's **base texture handle,
baseColor alpha, alphaCutoff, UV offset/scale/rotation and
vertexColors choice** in its per-frame staging and accepted
submission provenance. The collector requires exact equality
with the mask parameters used in VIVID's independent ID pass.
This rejects changed or republished textures/materials between
normal render and picking.

`SculptGpuIdPass::record` retains the old optional single
`gpuMaskOccluder` pointer and adds a trailing optional
`std::span<const SculptGpuVertexSource> gpuMaskOccluders={}`.
The total number (legacy pointer + span) may not exceed four.
Every distinct 60B GPU actor source has an individual immutable
index/UV/skin/3×4-row input set, a 32-byte output per indexed
vertex and an ordered Metal mask-expand compute dispatch.
Every blocker triangle ID is unconditionally replaced with
zero; sampled alpha and hardware depth decide occlusion.
The combined triangle count remains bounded to the existing
3M vertex budget and all expanded scratch allocations remain
pinned through the asynchronous GPU ID/depth callbacks or
a verified native GPU idle barrier. Opaque CPU scene depth
blockers and existing unsupported-material CPU fallbacks are
preserved; exact **GPU opaque** occluder expansion is not yet
included in this stage.

The existing GUI preview now routes its real GPU Mask actor
through the collector, not manually constructed source metadata.
`AIVERSE_VIVID_MASK_PARITY=2` is a new opt-in native Metal
mode: one selected human and **two independently GPU-compute-written
Mask Models** are all submitted to normal Renderer3D. Their
individual original GPU buffers are collected and passed to the
same actual VIVID ID/depth pass. The existing alpha-hole
(stage 1) and solid-occlusion (stage 2) scene colour/depth/ID
checks remain, with the closest solid Mask winning depth.
`tools/vivid-validate.sh` runs this separately, alongside
the old one-human, one-mask and 12-human modes.

The native `vivid_metal_sculpt_id` fixture defines an
additional third GPU-resident source scenario with an
independent .15-depth Mask, alpha-hole/solid outcomes and
actual Metal depth assertions. Headless fixtures cover
the 4-source cap, distinct GPU buffers, invalid last source,
and wrong/stale accepted renderer material/draw source.

**Actual verification:** GitHub connector code read/write,
commits and 28 static source-contract inspections. **NOT RUN:**
native CMake/C++/ObjC++/Metal compilation, CTest, GUI screenshots,
actual GPU colour/depth/ID readbacks, full
`bash tools/vivid-validate.sh --full`, Vulkan/GLES drivers,
performance/VRAM or main integration. These are committed
functional/test sources, not measured Metal PASS evidence.

**Remaining:** Native Mac acceptance and fixes are required before
the feature is production-proven. General automatic discovery
of all other renderer objects is not implemented: the host
must supply bounded known visible scene candidates before
`drawModel` so their submissions can be tracked; unknown
objects are not guessed. Multiple GPU-mask handling is
implemented; GPU opaque, arbitrary shader offsets, blended
cornea, GPU ICB/MDI, conditional LOD and late compositing
still fail closed or retain conservative CPU depth fallbacks.
Vulkan and GLES3 still lack true 60B GPU-source expansion.
See dedicated `docs/agent-handoffs/vivid-7BA-multi-gpu-mask-2026-11-20.md`.

## Character wardrobe assembly (native CPU, 2026-11-20)

`HumanWardrobe.h` provides versioned wardrobe validation, model inspection and
assembly. Clothes and accessories share one item contract; imported visibility
masks multiply body alpha. `bakeHumanAppearance` converts the existing appearance
DAG into ordinary PBR textures/factors. Body bind deformation and facial morphs
are retained in the output; garment fitting is not automatic. The Studio wardrobe section now exposes native asynchronous assembly, masks and
accessory sockets. See [character assembly](vivid-character-assembly.md) for
workflow, output contract and limitations.

## Studio lighting rigs — 2026-11-20

VIVID Appearance → Студийное освещение provides eight original preview rigs:
neutral, portrait, warm, cool, dramatic, rim, gel and catalog. Each combines a
shadowed directional key and three point lights, with hemisphere fill. Rotate
the rig, adjust overall brightness, or toggle the smooth floor/wall cyclorama.
These are AIVERSE rigs inspired by the environment-switching workflow, not copies
of Epic assets, UE area lights, Lumen or ray tracing. Point sources use the existing
High light tier and do not each cast independent shadows. The cyclorama is real
smooth-normal geometry, scaled to the imported character's bind bounds.

The settings are preview-only and retained until project reset; they never alter
character textures, exported appearance or map lighting. Native validated method:
`studio.humanStudioLighting` with optional `settings:{preset,rotation,gain,backdrop}`.
The response contains settings and eight preset labels. Switching light settings
does not advance the character binding revision or rebuild its GPU meshes.
Native Creator accepts `--studio-lighting neutral` (or another preset ID) for
repeatable captures. Without this option its legacy optical-test setup is retained.

Workflow reference: [Epic MetaHuman viewport environments](https://dev.epicgames.com/documentation/metahuman/navigating-metahuman-creator-in-unreal-engine).

## Default preview idle and poses — 2026-11-20

Native Studio now defaults to a relaxed pose with a generated six-second facial
idle, sampled by its existing Animator. Four poses, pause, speed and facial idle
toggle are in VIVID Appearance. Source data is not changed. See
[preview animation](vivid-preview-animation.md) for semantic rig requirements,
clip ownership, native methods and diagnostic capture controls. Full anatomical
correctives, clothing collision and imported material repairs are not included.

## Portable eye material baseline — 2026-11-20

The single-layer glTF eye uses roughness 0.6 by default (still adjustable through
`eyes.roughness`). Low roughness produced a broad gray highlight over the iris
and pupil under the Studio lights. This is a portable PBR fallback, not a
refractive cornea shader. The importer retains eye occlusion/lacrimal geometry
with restrained translucent factors because the original UE procedural shaders
are unavailable. Existing authored appearance values are preserved. Creator
`--closeup` captures the real native viewport at a shorter camera distance.
See [native eye comparison](qa/vivid-eyes-2026-11-20.md).

## Layered eye optics and eyelid material repair — 2026-11-20

The previous single-layer fallback is superseded for opted-in eye materials.
`Material::eyeOptics` stores iris UV centre, radius and optical depth. Zero radius
keeps the legacy renderer unchanged. GLSL/MSL implement tangent-frame refracted
UV displacement and a separate Fresnel wet coat with directional/point-light
highlights. Medium/high evaluate up to eight actual point/spot-light directions for the coat;
low keeps directional/environment reflection and skips point glints/parallax. This is a bounded real-time approximation,
not a ray-traced volumetric cornea or a claim of Unreal visual parity.

Material glTF `extras.aiverseEye` v1 stores `optics:[u,v,radius,depth]`, `ior` and
`tearFilm`; import bounds invalid values and falls back with a warning. Export
and character assembly preserve these settings plus portable PBR colour textures.
The appended GPU uniform participates in material equality/hash and merge checks.
Appearance adds `eyes.corneaIor`, `eyes.tearFilm`, `eyes.depth`; the native schema
exposes all three. Existing explicit values are preserved. Default sclera
lightness is now .59; source texture defaults retain authored fiber contrast.

Companion baking fits the original iris fiber/crypt atlas inside the limbus,
retains a dark pupil, and adds restrained peripheral vascularity. The blue radial
mask no longer wipes out iris detail. A matte pigmented cartilage fallback fixes
bright triangle-sized flecks along the eyelid strip; no eye/face geometry is
deleted or shrunk. Existing eyelash alpha and all51 morph targets are retained.
Creator `--eye-detail` provides a closer native capture; user-facing Studio
remains the native viewport. See [QA evidence](qa/vivid-eye-optics-2026-11-20.md).

Companion blink-normal repair: `tools/vivid-dna-import/repair_morph_normals.cjs`
repairs the known smooth head surface's `eyeBlinkLeft`/`eyeBlinkRight` normals
from deformed topology during `prepare_full_character.cjs`, retaining geometry,
UVs and skinning. This addresses black lighting bands from malformed exported
morph normals; it does not replace RigLogic. Creator diagnostic captures accept
`--shadow-quality off|medium|high` (default medium) to distinguish shadow-map
artifacts from morph shading. See eye optics QA report for native evidence.

### Adaptive character-stage shadows

Creator and native Studio use High-tier maps selected at1024/2048/4096 from
viewport density, camera distance/FOV and character-stage coverage. The selection
has hysteresis and delayed downsizing; no texture-UV dependence. General reusable
policy: `engine/render/AdaptiveShadowResolution.h`. Portrait lighting uses soft
fixed PCF with bounds fitted to the character instead of a room-sized shadow
volume. Ordinary game views keep their existing shadow behavior. This is a
single-map adaptation, not virtual shadow maps. See
`docs/qa/vivid-adaptive-shadows-2026-11-21.md` for actual verification and limits.
## Stage 6G — safe live Human Creator publication (2026-11-19)

The existing standalone VIVID Creator now stages a full replacement human before releasing the last-good visible one. Editing HumanDNA, changing eye/skin/hair material parameters, adjusting groom quality or reimporting an authored glTF/FBX/OBJ asset builds a new CPU head and groom, compiles the MotionWeave graph, uploads a replacement GPU model, creates a fresh immutable morph asset and separate actor GPU stream and allocates a presentation World. Only after each step succeeds is the previous instance retired behind a real GPU idle boundary. Failed edits preserve the old preview and report a concrete error; staged GPU allocations are released only after a successful idle barrier. The change preserves imported texture maps through `humanMaterialFromSource` and does not schedule or duplicate any skeletal animation.

Versioned DNA JSON saving on the VIVID base already uses failure-safe atomic rename without deleting the previous file. The `vivid_human_creator` regression now exercises a deliberately nonreplaceable target with a sentinel to ensure an unsuccessful save cannot erase existing user content. An isolated branch, `feature/vivid-human-safe-publication`, keeps these integration hardening changes separate from parallel new appearance and human importer commits.

**Native tests and GUI screenshots on this branch are still NOT RUN.** Current procedural skin/eye/groom is a preview reference, not photorealistic MetaHuman scan quality. HumanCreator and MotionWeave share established runtime modules; no proprietary DNA parser was introduced. Validate the existing 16-test VIVID suite and four real GUI capture modes on the Mac before promoting this branch, and run full AIVERSE regression before merging into main.
## Stage 6F — Creator transaction and last-good save protection (2026-11-19)

The existing full VIVID Creator (`tools/vivid-creator/main.cpp`, not a second editor) now uses a two-phase publication transaction. DNA edits, imported glTF/FBX material changes and groom-LOD adjustments first generate a complete CPU model, bind its MotionWeave graph, allocate/upload new head and hair GPU geometry and initialize the GPU morph actor. Only after the whole candidate is valid and the GPU has confirmed prior work idle does the Creator release and replace the previous visible character. A failed candidate upload/rig/ShaderGraph initialization leaves the last-good character rendered. Staged GPU resources are cleaned up behind an explicit idle barrier. This is an edit-time barrier only; the normal frame loop remains nonblocking.

The versioned `HumanCreatorSession::saveFile` no longer removes the target DNA recipe when atomic `rename` fails. If a platform cannot replace an existing destination atomically, it reports an error and leaves the previous destination intact; the `.vivid.tmp` staging file remains recoverable. The `vivid_human_creator` regression now verifies that a directory/sentinel destination survives a failed save.

Work is isolated on `feature/vivid-human-creator-ui`, because `feature/vivid-digital-humans` has progressed through other agents' Stage 6A–6E edits. This branch intentionally uses their existing full Creator, HumanDNA/Rig/Materials/Groom/Import, not a duplicate implementation. The macOS native CI workflow `.github/workflows/vivid-human-creator-validation.yml` builds the existing Creator and all 16 VIVID/MotionWeave focused test binaries and runs CTest. `AIVERSE_VIVID_REF=feature/vivid-human-creator-ui` selects this branch in `tools/vivid-validate.sh` (the branch default is changed within this branch only). That script also runs real Renderer3D one/12 actor and procedural/imported human Creator GPU GUI previews on a graphical Mac.

**Status:** changes are committed, but the CI job was pending at last inspection and no user Mac test for this isolated branch has been reported. The earlier 11/11 CTest and one/12 actor GUI passes apply to older Stage 5J source only. Production-grade photorealism still requires authored licensed/scanned geometry, high-quality skin/eye/hair maps, rig corrective authoring, physically evaluated performance/lighting and actual end-to-end visual asset QA. Do not claim MetaHuman parity from the procedural reference.

## Studio Human Creator — Studio window, MCP tools (2026-11-21)

The `tools/vivid-creator` authoring sections are available in the HTML5 Studio as the **Human Creator** window
(centre dock, panel `human_creator`) and to agents as `aiversemcp` tools. One shared core:

- `engine/character/vivid/HumanCreatorWorkbench.h` — `vivid::HumanCreatorWorkbench`: `HumanCreatorSession` (DNA, undo/redo),
  `RigAuthoring`, `SkinWeightAuthoring` and facial `ShapeRoute`s as one validated document. `schema()` lists 10 sections
  (identity, skin, eyes, groom, anatomy, expression = 52 rest channels, routes, rig bone names, rigCorrections,
  skinWeights) with DNA paths and the same limits as `HumanDNA::validate()`. Every edit is all-or-nothing.
  `buildModels()` generates the procedural head + groom cards, bakes `expressionRest` through the routes into the CPU
  vertices (`FaceShapeBinding` + `SparseMorphLibrary::evaluate`) and applies skin weights before upload.
- Storage: `<project>/humans/<name>.json` `{version:1, dna, rigCorrections, skinWeights, routes}`; exclusive staging +
  `renameat`, never truncates or deletes the last-good file; names `[A-Za-z0-9_-]{1,64}`, no symlinks.
- Studio native: `studio.humanCreatorInfo|Set|Preset|Undo|Redo|Route|Rig|Skin|New|Save|Load|List|Preview`
  (`apps/studio-native/StudioSession.cpp`). `Preview {enabled}` binds `StudioFacialPreviewBinding::bindCreator(document)`;
  every accepted edit rebinds with a new revision. `StudioFacialGpuPreview` regenerates on the Metal owner thread,
  publishes head + hair under fresh generation keys (last-good stays drawable until publication), applies rig
  corrections to node world matrices at submit and draws the hair cards with the head framing.
- Studio web: `apps/studio-web/src/panels/generated/HumanCreatorPanel.tsx`, helpers `panels/humanCreatorSchema.ts`.
- MCP: `human_schema`, `human_list`, `human_get`, `human_create`, `human_edit`, `human_validate`
  (`apps/editor/core/HumanCreatorOps.cpp`, GPU-free). Recipe: `skills/editor/aiverse-human-creator`.

Limits of this stage: the Studio preview is static (no groom simulation; hair follows the head framing, not the
animated scalp); viewport brush painting, scalp collision meshes and imported glTF/FBX/OBJ packages remain in
`tools/vivid-creator`; `aiversemcp` cannot capture the Studio viewport.

Tests: `studio_human_creator` (workbench: schema, validated edits, routes, rig/skin, rest bake moves vertices,
document round trip, atomic storage, traversal rejection) and new `studio.humanCreator*` checks in
`studio_human_appearance_session`; web `tests/humanCreatorSchema.test.mjs`.

### Character editor (model source) and viewport sculpt — 2026-11-21

The Human Creator window is the single character editor. Source "Модель проекта" edits an imported character
(e.g. `Ada/model.gltf`) through the existing `studio.humanAppearance*` / face / wardrobe methods, in tabs:
Тело (body proportions), Лицо (face meshes, jaw/nose/eyes sliders, photo fit, 3D-scan fit, separate head + body
import), Скульпт, Кожа и волосы (skin, hair, lashes, eyes, material roles), Одежда (wardrobe recipe and bake),
Поза и свет, Профили (`human-looks/`). Source "VIVID DNA" is the procedural workbench above. Shared state lives in
`apps/studio-web/src/panels/useHumanLook.ts` (also used by the VIVID Appearance window).

Viewport sculpt: `studio.humanSculpt {enabled, radius 0.01..1, strength 0..0.25, mode push|pull}`,
`studio.humanSculptStroke {point, normal}` (normalized head coordinates), `studio.humanSculptUndo`. While armed, a Scene
click in facial mode is a stroke, not a selection: `StudioMetalScene::sculptPick` -> `StudioFacialGpuPreview::sculptRaycast`
-> `vivid::raycastHumanFace` (selected face meshes, bind pose) -> `vivid::humanSculptLandmark` -> a `HumanFaceShape`
landmark on the bound character; the native event `human.sculpt` refreshes the window. Strokes are landmarks (max 256),
baked with the character build. Each stroke re-imports the preview model (seconds on large characters).

### Character Creator — MetaHuman-style workflow (2026-11-21)

- Viewport sculpt: drag paints strokes (one per brush spacing, coalesced while native is busy), a brush circle follows
  the cursor (`components/ViewportPlaceholder.tsx`, `panels/sculptBrushStore.ts`); up to 1024 landmarks.
  The Scene keeps the previous character on screen while another one imports.
- Live sliders: `useHumanLook.live()` applies while dragging without toggling busy; one history step per gesture.
  One undo/redo history (look + face + sculpt), ⌘Z / ⇧⌘Z in the window.
- Presets: saved looks get a 256 px picture `human-looks/<name>.png` (`studio.humanLookCapture` -> next facial frame via
  `Device::captureNextFrame`, `studio.humanAppearanceThumbnails` returns data URLs). Random character with locked parts
  and an offline Russian description parser (`describeToLook`) plus a copyable agent prompt.
- Face blend per part: `studio.humanFacePick {enabled}` turns viewport clicks into head points (`human.facePick`
  event); the picked part (nose, mouth, eyes, brows, jaw, cheeks) gets its own blend handle (`blendFace(..., perRegion)`).
- Before/after: `studio.humanCompare {original}` draws the untouched source model (edit-in-place path).
- Test expressions: `HumanPreviewAnimation.expression` (idle smile laugh surprise anger sad disgust talk), ARKit-named
  morph weights layered over the clip pose (`applyHumanPreviewExpression`); models without those targets are unchanged.
- Wardrobe: live try-on `studio.humanTryOn {wardrobe|null}` assembles base + outfit on a worker at 256 px textures
  (nothing written); item `tint` [r,g,b] 0..4 multiplies the item colour; the build reports `lodTriangles` per LOD and
  `textureSize`; "В игру" = `studio.humanPlaceInGame {type, path}` merges an RTS type with `view.model` and places one unit.
- Pose & light tab: expression chips, pose segments, light preset cards (8 rigs) applied on click.
- MCP: `human_look_schema`, `human_look_list`, `human_look_get`, `human_look_save` (`HumanCreatorOps.cpp`).
- Second pass: agent snapshot handshake (`studio_character_snapshot` writes `.aiverse/agent-requests/character-snapshot.json`,
  Studio's tick answers with `.aiverse/agent-snapshots/<id>.png`); makeup layers `makeup.*` (UV ellipses in the skin
  graph, only compiled when used); hairstyle library (models with hair/hairstyle in the path go into the outfit's
  `hair` slot and hide the model's own hair meshes, shared try-on via `useOutfit`); age control (`ageLook`); crowd
  variants (N looks, optionally built + placed as unit types); model clips in "Поза и свет" (`HumanPreviewAnimation.clip`,
  clip list from the GPU preview); index-only LODs for skinned characters at build (`vivid::generateHumanCharacterLods`,
  shares LOD0 vertices, skips morph meshes); reference photo overlay over the viewport; character packages
  `packages/<look>.aivchar` (`studio.humanPackage{List,Export,Import}`, import never overwrites); height in cm and a
  10 cm ruler (`studio.humanRuler`).
- Gap fixes: `studio.humanMakeupAlign` places makeup from the face geometry (`vivid::humanFaceUvAt`: nearest front skin
  vertex at lips/cheeks/eyelids/chin of the normalized head box -> UV); hairstyles skin to the character when every
  hair bone exists on it, otherwise attach to the head bone (`keep_world`); external animation files are added to the
  engine clip sidecar `<model>.clips.json` (`studio.humanClip{List,Add,Remove}`, bound by bone path like
  `applyClipSidecar`), and the preview re-imports through `StudioFacialPreviewBinding::reload()`.

## MetaHuman compatibility adapter — 2026-11-21

`engine/character/vivid/MetaHumanAdapter.{h,cpp}` (`vivid::adaptMetaHuman(const render::Model&)`): the runtime side of
MetaHuman compatibility, with no Unreal code or .dna at runtime. Input is an ordinary imported model from a permitted
MetaHuman export (Unreal glTF/FBX export or the metahuman-to-glb companion). It recognises the UE5 MetaHuman skeleton
(spine_05, neck_02, head, pelvis; importer suffixes stripped), FaceMesh / head_shader naming and FACIAL_* joints, and
returns: material roles for the Character Creator look (head/body skin, eyes, lashes, hair/brow cards, clothing; eye
shell/edge, saliva and cartilage reported as hidden, teeth keep source PBR), the face mesh selection (FaceMesh, brows,
lashes), ARKit coverage against VIVID's 52 channels, joint/skin/LOD facts and plain notes about what is NOT supported:
RigLogic corrective drivers (joints stay in bind pose), FACIAL_* joint animation (expressions run through ARKit morphs),
strand groom (cards only), proprietary .dna (offline `tools/vivid-dna-import` only), Draco-compressed GLB (re-export).

Surfaces: Studio Character Creator > Пресеты > "MetaHuman" (`studio.humanMetaHuman {guid}` applies roles and face
meshes to the look); MCP `human_metahuman_check {path, save_look?}`.

Verification (KINGSTON, Mac build, ctest `vivid_metahuman_adapter` PASS): synthetic fixture plus two real local
MetaHuman exports (not vendored) via `AIVERSE_METAHUMAN_SAMPLES`: Ada (`VIVID-Look-Lab/content/Ada/model.gltf`):
detected, score 5, head bone `head`, 2 face meshes, 10 material roles, 4 hidden, ARKit 51/52 (missing `tongueOut`),
843 FACIAL joints, 271 corrective joints, 5 skins; Bruce (`Characters/Bruce-Full/model.gltf`): detected with >=40
ARKit channels (assertion). The original `bruce.glb` is Draco-compressed and is rejected by the importer.
- Follow-up: `vivid::buildMetaHumanRig` / `applyMetaHumanRig` (presentation drivers, not RigLogic): `*_half_<side>`
  joints take half their neighbouring base joint's rotation (counter-half when parented to it), `lowerarm_twist_0N`
  share N/3 of the hand roll, FACIAL_C_Jaw / FACIAL_L_Eye / FACIAL_R_Eye follow jawOpen / eyeLook* in world space only
  when the morphs do not already move teeth / eyeballs. Applied in Studio's facial preview after the clip pose.
  Draco GLB: offline `tools/vivid-dna-import/decompress_draco.cjs` (the importer error points to it); the real
  `bruce.glb` decodes and is part of the adapter verification samples.

