# AIVERSE MotionWeave — Stage 180: semantic facial Control Rig, reusable IK and ECS

**Date:** 2026-11-20  
**Branch:** `feature/universal-ecs-animation-pipeline`  
**Starting HEAD:** `f00a68be9aaf0962a3f7cdbb3ed32f92f972875a` (Stage 179)  
**PR:** https://github.com/chromealex/AIVERSE/pull/22 — leave **DRAFT**, do not merge before native acceptance.  
**Immutable rules:** no RTS/FPS, networking, Box3D or gameplay authority in core animation. The pose from one parent Graph/Animator is evaluated exactly once, then transformed cosmetically. One dated independent handoff per stage. Defer full tests until all requested features close.

## Source implementation

| Commit | File | Source change |
|---|---|---|
| `159d21ef8d4f6c82c4675c1c0af9bdff7500aced` | `libraries/include/aiverselib/animation/FacialControlRig.h` | NEW compiled semantic 64-channel per-mesh morph control mapping; v1 JSON binding policy; independent actor smoothing state; calls existing native AnimationRig::compile/execute |
| `59015572`, `0eeed96a`, `82a8b4a7`, `e96585b2` | same | Protect against stale model bind T/R/S, node/mesh hierarchy and morph capacities despite reused model pointers; no morph-name allocation per frame; pass dt to existing IK state; reject missing named IK params and nonfinite host inputs |
| `fa76295b5fcf28290a870c1c7fefc3680edf69c2` | `libraries/include/aiverselib/animation/EcsAnimationSystems.h` | NEW optional AnimationFacialControlComponent with immutable rig, typed controls, per-actor FacialControlState. Extend prepared ECS write pools and one Graph->Montage->physical/recovery->Face->worldMatrices evaluation; reset face state on Graph rebind/incompatible asset; retain diagnostics |
| `6d1266420a3b6649b82aec45e27e3b796f1c6f69` | `libraries/include/aiverselib/animation/MotionWeaveScheduler.h` | Classify IK/host-callback face rigs as serial owner-thread work. Pure morph-only rigs remain worker-eligible with existing Graph safety checks |
| `420161ef1876f7aaa8d499fac6f83d1f2ff75ef9`, `177d72c7`, `6ad95573`, `ea6fe0b3`, `008420b8` | `tests/animation/facial_control_rig.cpp` | Deferred Release-active native fixture: imported names, eye aim using existing IK, half-life smoothing, invalid/reimport atomics, bad JSON, **52** distinct morph channels, one Graph+face ECS update, worker eligibility/serial barrier and missing IK controls |
| `b0aad54ea7179dacc04efaf58c4601009fcc212c` | `CMakeLists.txt` | Register `aiverse_animation_facial_control_rig_tests`, CTest `animation_facial_control_rig` |
| `428756b6bf02bb047be9e41ff3c211e5fb287e04`, `1dafa9c2` | `docs/motionweave-facial-control-rig.md` | Public authoring/host-facing API, JSON v1, blending, smoothing, existing IK, reimport, GPU/worker lifetime, limitations, delayed tests |
| `410af223`, `6be19d92` | `skills/gameplay/aiverse-{motionweave,animation-ecs}/SKILL.md` | Teach other agents to reuse this compiled rig instead of regenerating a facial/IK system or putting it into genre code |
| `0199e54c380d9bd64158a80b416990d7c230fc20` | `docs/motionweave.md` | Index new public API and diagnostics |

## Native API and ordering

`FacialControlRig::compile(model, authoredJson, declaredAnimationParameters)`
compiles a source rig into an immutable Model-bound program. `morphs` is
an ordered array of `{parameter,node,target,gain,bias,halfLife,blend}`.
Nodes and morphs are resolved from the REAL `Model` and importer-
authored mesh-local semantic names (glTF `extras.targetNames`, FBX
blend channel names). This does not invent ordinal mapping. Every
selected `(node,channel)` must be unique. Up to 256 authored bindings
can target the existing per-mesh 64-channel GPU facial capacity. A single
control can drive several different channels; typical 52-expression
naming sets fit in 64. Rigs with missing/ambiguous names, duplicate
morph assignments, invalid finite ranges, mismatched model binding
or missing typed inputs reject atomically.

Optional `rig` compiles the ACTUAL existing
`render::AnimationRig` program. This reuses existing
`aim`, `angularLimit`, `twoBoneIK`, `fullBodyIK` etc. for
head/jaw/eye bones; no second IK solver has been added. Runtime
`FacialControlRig::apply(model,controls,pose,state,dt,weight,error)`
is CPU-only, per-character, transactional: it copies an evaluated
pose+state, merges direct and half-life-smoothed control inputs into
NodePose::w, solves compiled native rig, and commits only on success.
Missing finite float/vector IK controls are rejected, not silently
substituted. `FacialControlState` contains only runtime smoothing and
native IK state; it is **never** inserted into authoritative ECS,
serialized, hashed or used for rollback authority.

`AnimationFacialControlComponent` attaches the immutable rig,
per-instance state, typed `AnimationParameters`, enabled and weight
to the separate presentation World. Existing GameModule/Graph assets,
JSON Graph schema and save/checksum are unchanged. The normal
`MotionWeave::update` now sequences: ONE Graph evaluation ->
Montage -> external physical blend/inertialization -> facial morph/IK ->
one final worldMatrices pass. Face controls need not be declared by
the parent Graph. Scheduler prepares the facial write pool before
dispatch and routes any native IK/host environment callback to the
serial owner thread; safe morph-only programs may run in parallel.

A face rig is bound to the Model generation and depends on its GPU
owner being resident. It stores a raw borrowed Model identity plus
immutable copies of node names/parents/mesh, bind T/R/S, channel counts
and named semantic morph channel ordering. It checks those values
before mutation to reject a reimport using the same pointer but
different rig geometry. Reimport requires recompilation and reset of
per-actor FacialControlState; these checks are NOT GPU completion tokens.
Lifetime/retirement still follows Stage 174–179's real native Model
resource owners.

## Verification: actual vs deferred

**ACTUAL:** GitHub connector reviewed source definitions for
AnimationRig, AnimationGraph, AnimationParameters, Model/Primitive/
MorphTargetNames, importer-backed ModelPoseRetarget, ECS prepared
writer pools, MotionWeaveScheduler safety classification and existing
test registration/fixture patterns. Committed exact source/CMake/
docs/agent skill files via GitHub; re-fetched relevant files, verified
source changed, no merge conflict markers observed. This environment
does **not** have the user's native AIVERSE Mac worktree or GPU.

**NOT RUN:** CMake configure/build, native C++ compiler/link,
`animation_facial_control_rig` or any CTest, actual artist FBX/glTF
52 blendshape Model, Metal/Vulkan/GLES uniform/shader/pixel parity,
ASan/TSan/UBSan, host callback/worker runtime, multi-scene
GPU residency, 1k/10k/100k actor benchmark, VRAM/stalls/fences.
**NO PASS/PRODUCTION READY CLAIM.** Test fixture is committed,
not proven; compiler/runtime issues may remain until Mac validation.

Deferred command on owner's macOS worktree:

```sh
cmake --build build/ecs-test --target aiverse_animation_facial_control_rig_tests
ctest --test-dir build/ecs-test --output-on-failure -R '^animation_facial_control_rig$'
```

## Readiness after Stage 180

| Capability | Status | Remaining |
|---|---|---|
| Generic AnimationGraph/Montage/ECS/one parent pose owner | Earlier stage SOURCE IMPLEMENTED | Native acceptance |
| 64-channel morph glTF/FBX, sparse GPU morph palette, explicit morph LOD maps | Stage 167–172 SOURCE IMPLEMENTED | Real artist assets, RHI tests |
| SceneAsset/GameHost and real shared GPU Model residency | Stage 176–179 SOURCE IMPLEMENTED | Runtime/fault/VRAM acceptance |
| **Compiled semantic expression-to-Model morph mapping + smoothing** | **Stage 180 SOURCE IMPLEMENTED** | Native/artist preview |
| **Compiled existing IK for eye/jaw/head + strict runtime controls** | **Stage 180 SOURCE IMPLEMENTED** | Real eye/jaw rigs, constraint comparison |
| **Face ECS integration + serial callback safety** | **Stage 180 SOURCE IMPLEMENTED** | Worker/pose native suite |
| Visual 52-expression authoring sliders, visemes and animation preview | NOT IMPLEMENTED by Stage 180 | Stage 181 editor/ModelView |
| Automatically generated morph-preserving LOD meshes | NOT IMPLEMENTED | Separate geometry stage |
| Full CMake/CTest, Metal/Vulkan/GLES/sanitizer/performance | **NOT RUN** | One coordinated Mac acceptance after feature closure |

## Next Stage 181 — concrete artist workflow

Inspect the existing real `apps/editor/animation` and
`apps/modelview` facial/rig authoring code. Add an **opt-in**
interactive 52-channel/ARKit-style facial authoring inspector with
user-supplied morph mapping and named IK targets, live preview using
the compiled `FacialControlRig` (one Animator/Graph owner),
transactional JSON v1 save/load and native asset reimport
compatibility. Do not invent geometry for 52 targets when the
source Model lacks them; show missing channel diagnostics and
validation failure. Keep the core genre-neutral.

Stage 182+: morph-preserving automatic mesh simplification with
source semantic channels and bounded GPU budget; then perform ONE
coordinated comprehensive native Mac CMake/CTest/Metal/Vulkan/GLES/
sanitizers/crowd/VRAM acceptance. Fix source/runtime issues revealed
before undrafting or merging PR #22.

**If another agent advances the branch, re-fetch HEAD before editing.
Never overwrite its work, rebase blindly, force push, or claim
unexecuted test results.**
