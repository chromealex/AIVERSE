# MotionWeave Stage 180 — semantic facial Control Rig and native IK composition

**2026-11-20 · source implementation, native verification NOT RUN**

Stage 180 extends the existing **genre-independent** MotionWeave core rather than
reimplementing Graph/IK, adding a second pose writer or modifying gameplay ECS.
It uses real imported glTF `mesh.extras.targetNames` / FBX blend channel names,
a compiled **semantic** mesh-local morph mapping (up to the existing 64 slots),
the existing native `render::AnimationRig` solver for eyes, jaw and
joint constraints, and exactly one host-evaluated parent pose.

## Public native C++ API

Use the normal `AIVERSE::MotionWeave` target and include
`<aiverselib/animation/FacialControlRig.h>`:

```cpp
using namespace aiverse;
using namespace aiverse::lib::animation;

// Source import/model lifetime belongs to the host. After an actual
// Graph/Animator evaluates one parent pose, apply the cosmetic controls.
render::AnimationParameters declared, current;
declared.values["jawOpen"]=0.f;
declared.values["eyeBlinkLeft"]=0.f;
declared.values["lookWeight"]=1.f;
declared.vectors["lookAt"]={0.f,1.5f,3.f};

const Json authoring=Json::parse(readYourFaceRigJson());
auto rig=std::make_shared<const FacialControlRig>(
    FacialControlRig::compile(model,authoring,declared));

FacialControlState perActor; // Never share mutable state across actors.
render::Pose pose;           // Produced by ONE Animator/AnimationGraph.
std::string error;
const bool ok=rig->apply(model,current,pose,perActor,
                         1.f/60.f,1.f,&error);
if(!ok) {
    // The incoming pose and per-actor state stay unchanged. The render
    // host may keep the previous healthy pose or a source Graph fallback.
}
```

The `readYourFaceRigJson()` helper is project-supplied, not an engine method.
Native hosts can integrate this directly after evaluating their Animator/Graph.
For the recommended presentation ECS mode:

```cpp
auto& facial=presentation.add<
    lib::animation::AnimationFacialControlComponent>(visualEntity);
facial.rig=rig;
facial.controls=declared;  // Update values/vectors from expression input.
facial.weight=1.f;
// MotionWeave::update(presentation,dt) now applies facial AFTER Graph,
// Montage, physical overlay/recovery and BEFORE final worldMatrices.
// No second Graph/Animator, no authoritative Simulation::world() writes.
```

Facial parameters are **owned by the facial component**, so existing
AnimationGraph programs do not need to be modified to declare extra
52-expression inputs. Input drivers may come from artist controls, lip-sync,
gaze tracking, captured facial animation or other host streams. The
`AnimationFacialControlComponent` is optional, and old projects see no
new behavior without it.

## Authored JSON format v1

```json
{
  "version": 1,
  "morphs": [
    {
      "parameter": "jawOpen",
      "node": "Face",
      "target": "jawOpen",
      "gain": 1.0,
      "bias": 0.0,
      "halfLife": 0.06,
      "blend": "override"
    },
    {
      "parameter": "eyeBlinkLeft",
      "node": "Face",
      "target": "eyeBlinkLeft",
      "halfLife": 0.035,
      "blend": "additive"
    }
  ],
  "rig": [
    {
      "bone": "Eye.L",
      "op": "aim",
      "target": "lookAt",
      "axis": [0, 0, 1],
      "weightParameter": "lookWeight",
      "weight": 1
    },
    {
      "bone": "Eye.L",
      "op": "angularLimit",
      "min": [-0.45, -0.65, -0.2],
      "max": [0.45, 0.65, 0.2]
    }
  ]
}
```

`node` and `target` are exact imported node/mesh-morph semantic names,
not guessed ordinal indices. A given `(node,morph channel)` can be
driven by **one** binding, but a single parameter may drive multiple
different channels/nodes. A missing/ambiguous bone, unknown named target,
duplicate channel, incomplete morph metadata, invalid or nonfinite
gain/bias/half-life, absent declared parameter or malformed rig operation
rejects the **entire compile**, not a subset. Use unique explicit
semantic aliases in a new asset if a project's exporter uses different
names. Typical ARKit-style 52 input sets are supported up to the native
64-channel budget; no claim of automatic ARKit-to-custom-rig guessing.

Morph blend semantics: `override` moves the incoming animation channel
toward the smoothed facial target. `additive` adds the smoothed target
to the incoming clip weight (saturated 0..1). `globalWeight` blends the
result against the already evaluated Graph/Animator pose. Parameter
input is gain/bias adjusted and clamped to 0..1; the half-life smoothing
uses `1-exp2(-dt/halfLife)` with **independent per-actor state**. The first
frame snaps to current controls. `dt=0` preserves the prior smooth
value unless the program has just been initialized; `halfLife=0` is
immediate. The output supports the existing sparse 64-channel GPU
morph palette; no new shader packing is introduced.

`rig` is an actual `AnimationRig::compile/execute` program using all
its already supported bone/IK operators: `transform`, `copy`,
`aim`, `twoBoneIK`, `angularLimit`, `limit`, `fullBodyIK`
and `surfaceLocomotion`. Eye/look-at and mandible/head rotations
therefore reuse compiled native IK and joint limits instead of
maintaining a face-specific solver. The host specifies model-space
targets through typed `render::AnimationParameters::vectors`;
there are **no collision or gameplay-authoritative writes** here.

## Runtime ordering and lifetime

1. The host reads the authoritative simulation state into separate
   cosmetic presentation inputs; it does not run float IK inside
   `Simulation::world()`.
2. `AnimationGraphInstance::update/evaluate` (or native Animator) is
   the **one** pose writer for the parent.
3. Montage, external physical pose blend, recovery/inertialization
   occur through the existing MotionWeave presentation pathway.
4. The optional `FacialControlRig` processes named mesh-local morphs
   and executes the already compiled native IK program against the
   **same pose**, transactionally using a candidate copy.
5. `worldMatrices` and Renderer3D consume the final pose. GPUs already
   support sparse 64-channel morph source indices on Metal/Vulkan/GLES.

The immutable compiled rig and Model pointer belong to the host's
resource generation and must outlive their instances. On Model/Graph
reimport or GPU device recreation, replace the rig, reset each actor's
`FacialControlState` and use the new immutable pack/graph revision.
A defensive Model compatibility check compares exact node names,
parents, mesh references, bind translations/rotations/scales, per-mesh
morph capacities and ordered imported semantic names; even a reused
Model pointer will be rejected on those changes. It is not a GPU
fence or an automatic asset-loader.

MotionWeave detaches facial **write pools before worker jobs**; no
two graph workers may write one actor's facial state. The
`MotionWeaveScheduler` dispatches **morph-only** rigs without an
environment through its normal worker-safe graph path, but any facial
rig using the existing native IK (or host environment callbacks)
runs at the serial owner-thread barrier. Runtime IK vectors and named
scalar weights required by the compiled rig must be present; missing
controls fail atomically instead of silently using default targets.
Facial state is cosmetic and never serialized, hashed or replayed as
gameplay.
The user has deferred native tests to the final coordinated Mac pass;
`animation_facial_control_rig` is newly registered but NOT RUN.

## Known remaining work and acceptance

This stage implements a reusable expression-to-morph plus existing
bone/IK authoring/runtime foundation, **not** a complete MetaHuman
equivalent: there is no visual 52-slider authoring panel yet, no live
audio-to-viseme solver, no automatic expressive facial performance
capture, no new contact-aware eyelid-to-cornea system, no automatic
morph-preserving mesh LOD generation and no measured 100k-crowd CPU
budget. These are distinct future stages.

Run on macOS only in the requested final acceptance pass:

```sh
cmake --build build/ecs-test --target aiverse_animation_facial_control_rig_tests
ctest --test-dir build/ecs-test --output-on-failure \
    -R '^animation_facial_control_rig$'
```

Tests cover two named morph expressions, smoothing one half-life,
concurrent eye aim through native AnimationRig, all-or-nothing invalid
input, renamed/re-imported source skeleton/morphs, invalid JSON,
52 distinct semantic channels and ECS Graph-to-Face composition.
**No compilation or CTest PASS has been observed from this stage.**

Next: complete the artist facial authoring UI + direct preview workflow
and automatic morph-preserving mesh simplification, then one full
native CMake/CTest, GPU Metal/Vulkan/GLES, sanitizers, crowd/perf and
RHI-lifetime acceptance. Do not merge draft PR #22 prematurely.
