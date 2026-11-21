# MotionWeave Stage 183 — preview Studio Facial Rig JSON with real ModelView rendering

**Status 2026-11-20:** source implemented; CMake/C++, GPU, Metal/Vulkan/GLES, tests and screenshots **NOT RUN**.

Stage 181's HTML5 Studio Facial Rig panel saves a project-local `facial-rigs/<name>.json` v1 document. This stage does **not** merge the HTML5 Studio branch. It implements an actual native ModelView reader of that exact JSON plus a reusable C++ session for the eventual Studio native backend. No mock face or browser-side GPU renderer is introduced.

## View a saved Studio facial rig on a real imported model

```sh
cmake --build build/ecs-test --target aiverse_modelview
./build/ecs-test/aiverse_modelview /path/to/face.glb \
    --facial-rig /path/to/project/facial-rigs/my-face.json \
    --face jawOpen=0.8 --face eyeBlinkLeft=0.5 \
    --face-vector lookAt=2,1,3
```

The names on the command line must be declared by the Studio JSON and must match the real imported Model's nodes, morph names and IK declarations. If your face model uses `Head` instead of `Face`, use the corresponding *real* name in Studio. An invalid rig rejects explicitly; there is no ordinal guess or fabricated channel. Only a regular ModelView imported-model preview is currently supported; not `--scene-preview`, `--game`, attachment preview or baked GPU crowd clips.

The Viewer automatically disables static primitive merging for this opt-in mode so exporter-authored morph identities survive unchanged. The **existing native Animator** evaluates once, then the optional `FacialControlRig` applies morphs/native IK, then ModelView calculates world matrices and calls its current `Renderer3D` draw. Save a modified JSON from HTML5 Studio to the same pathname: the running ModelView detects its timestamp and transactionally recompiles the facial program before a subsequent render pass. Invalid edits retain the last-good preview program; no model GPU owner is swapped. This file-reload capability does **not** stream unsaved browser slider changes; use `--face` arguments to initialize expression controls.

## Reuse one session in a native project / future Studio host

```cpp
#include <aiverselib/animation/FacialPreviewSession.h>
using aiverse::lib::animation::FacialPreviewSession;

FacialPreviewSession session;           // one per visible actor
FacialPreviewSession::Scalars controls{{"jawOpen",0.8f}};
FacialPreviewSession::Vectors vectors;
std::string reason;
bool good=session.configure(model,studioAuthoredJson,
                            controls,vectors,reason);

// Your existing Animator/Graph generates the one parent pose.
animator.evaluate(pose);
if(good && !session.apply(model,pose,dt,reason)) {
    // last-good source pose remains unchanged on failure
}
aiverse::render::worldMatrices(model,pose,nodeWorld);
// renderer then consumes that SAME pose
```

`FacialPreviewSession::scalar(name,weight)` / `vector(name,target)` are bounded per-character live setters. `configure()` compiles a complete candidate using real `FacialControlRig` and the declared Studio v1 parameters; it publishes **only after full success**, preserving old rig/smoothing state when a file or model revision is invalid. A Model resource reimport requires recompiling against the new Model and resetting state, with owner GPU retirement handled separately. No persistence, gameplay authority, or device resource ownership enters the session.

## What remains

Native ModelView displays a real imported Model, but the **inline HTML5 Studio Scene preview is still not integrated** because the Studio and animation branches are currently separate. Stage 184 must connect Studio's existing native viewport and typed IPC, real project Model GUID/lock selection and RHI/ModelAssetProvider leases to this exact shared session after a conflict-aware branch integration. It must not create a second animation evaluator or a fake browser canvas face.

Native proof has NOT RUN: `animation_facial_control_rig`, ModelView startup/real GLB preview/hot reload, shader pixels on Metal/Vulkan/GLES, GPU device epoch failures, ASan/TSan and artist/budget benchmarks.
