# AIVERSE MotionWeave + Studio HTML5 — Stage 186: FBX morphs, semantic LOD and GPU device epochs

**Date:** 2026-11-20  
**Branch:** `integration/motionweave-studio-facial-stage184`  
**Starting HEAD:** `ef5ca6acd7139a44a501a99f4160a6e3af360db8` (Stage 185 handoff)  
**Implementation HEAD before handoff:** `d11504cc1b80f4a092c8364c2403d4e776d41a35`  
**Studio base:** `feature/studio-html5`, changing concurrently (observed `dad3a77c0b1e2595084a51796f0cf203d80ff049`)  
**MotionWeave reference:** `feature/universal-ecs-animation-pipeline`, reviewed at `1a11a21af8389b246983244b742cc8a65d9a2713`  
**PRs:** Studio integration [Draft PR #89](https://github.com/chromealex/AIVERSE/pull/89); MotionWeave [Draft PR #22](https://github.com/chromealex/AIVERSE/pull/22). Neither merged into main; PR #89 currently reports non-mergeable with parallel Studio branch changes.

## Scope and delivered code

This stage is deliberately **genre-independent**: visual imported Model, native facial Controller/Animator, shader/LOD and GPU owner lifetime only. No Simulation/gameplay ECS, FPS/RTS-specific animation logic, alternate IK solver, browser fake face or extra Graph writer. Stage 185's 64-slot GLSL/MSL sparse palette remains the shared native render path.

| Commit | Source path(s) | Function |
|---|---|---|
| `12af57c02ac8be2960274615ef72ba9a6255f62b` | `engine/render/ModelFbx.cpp` | Port Stage 168–170/64-channel FBX morph sources: actual ufbx named blendshape channels, flattened in-between shape targets and their weight interpolation, per-primitive POSITION/NORMAL delta spans, source default weights, sampled animated morph weight channels. Model importer handles split/final vertex ranges and preserves exact authored names; >64/invalid morph source fails closed. LCS review confirmed source differences from Studio FBX were limited to these morph-related insertions and replaced unsupported/ignored diagnostic. |
| `92b98acb4dd58e4a33f17c35a6a9d0f0d33ce0a9` | `engine/render/Model.h`, `AutoLodRuntime.cpp` | Add `Primitive::morphAliasMesh/morphAliasPrimitive` (default -1) and `ModelLod::morphSourceForTarget`. Port Stage 182 *index-only* morph-aware autoLOD: signed POSITION/NORMAL morph envelopes across up to 64 channels, complete unique mesh names, identity semantic target-to-source mapping, original vertex and morph-delta arrays borrowed rather than copied for coarse geometry. Skinned and unsafe material targets remain excluded. |
| `a2cb51c9affb144f0de8a130f494f369f18b8968` | `engine/render/Model.cpp` | Add conservative source morph bounds for normalized [0,1] weights, include parent source envelope in alias LOD; prepare CPU LOD **before** GPU alias preflight, validate alias invariants and non-chained source; source-only RGBA32F allocation and exact alias `morphTexel` sharing after upload. Preserve Studio's geometry/material revision marks and existing RHI upload path rather than wholesale overwriting Model.cpp. |
| `40dedf152c6a8d0308ef77b5e2b3bc1a4db36193` | `engine/render/Renderer3DModels.cpp` | Enable morph-aware LOD with finite normalized source weights and complete remap validation; fallback LOD0 for malformed source/target maps; steady LOD and both crossfade halves encode target channel index with the exact mapped source `NodePose.w[]` weight into Stage 185's 64-slot sparse U4 GPU palette. Preserve Studio-specific ICB/LOD, diagnostic, material, culling and batching code. |
| `d66d642d52637b37ee3fe9e52fa50c513fbc1229` | `tests/render/auto_lod_runtime.cpp` | Port deferred 52-channel source-only AutoLOD regression: LOD0 unchanged, identity maps, source-delta aliases, conservative bounds, missing semantic names/malformed deltas rejected, idempotence and original rigid model tests retained. |
| `b22a35309450b459101c18752cc4ef77575f6e96` | `engine/render/ModelGltf.cpp` | Port semantic **authored** glTF `mesh.extras.aiverseLods` (legacy `rtsLods` alias), exact `extras.targetNames` source/target remap for skinned/unskinned mesh LOD, missing/duplicate/target-only channel hard rejection, incompatible morph/skin presence rejection and 64-channel glTF import checks. Surgical diff review confirmed this adds morph LOD semantics to Stage 185 without rewriting unrelated Studio subsystems. |
| `c8f7a4a75ba3018d78acd38740bc5ab48c5e99f6` | new `engine/render/RenderDeviceLifetime.h`, `StudioFacialGpuPreview.{h,cpp}`, `StudioMetalScene.{h,cpp}` | Port an actual generation-safe `RenderDeviceLifetime` weak lease from MotionWeave, bind it to Studio Metal Device on owner thread. The facial GPU owner checks epoch/thread before CPU/GPU publish, submit or retirement; stale device address reuse fails closed. Lifetime is retired after facial/renderer shutdown attempt. This is **not** a replacement for GPU command completion fence or the missing shared ModelAssetProvider. |
| `922b393cf2a3dc2758c3a451d45cf7f12dfb1db4` | `apps/studio-native/mac/StudioFacialGpuPreview.cpp` | Remove Stage 185's forced `batching.autoLod=false`. The real Studio preview now honors imported project/model autoLOD policy, enabling the new morph-safe index LOD when eligible; explicit sidecar/ENV opt-out remains. |
| `d11504cc1b80f4a092c8364c2403d4e776d41a35` | `apps/studio-native/tests/StudioFacialRuntimeTests.cpp` | Extend deferred native GLTF fixture with source 52 named morph channels and a coarse authored one-channel `expression51` LOD; expected exact remap `{51}`; importer must reject target-only invented channel. |

## Runtime/public contract

- `render::loadModel("content/models/face.glb|.gltf|.fbx",reader)` now supports up to 64 native morph targets with real importer names for eligible glTF and FBX. Generic `FacialControlRig` uses `MeshDef::morphTargetNames` / imported bones, never index guessing.
- `ModelLod::morphSourceForTarget` is target-LOD-local morph slot -> **source node morph slot**. Original LOD0 and its Animator/Graph time stay untouched. Renderer packs current **source** weight and **target** channel ID separately (Stage 185 U4 136 floats / 544 bytes), in steady and crossfade draws.
- `Primitive::morphAliasMesh/morphAliasPrimitive` are import/preparation owned identifiers only for generated index-only LODs. They share the source contiguous vertices and source morph texture offset; GPU texel data is NOT duplicated across LODs. `Model::finalize()` bounds span normalized source morph deformation conservatively.
- `AIVERSE_AUTOLOD=off`, per-asset `batching.autoLod=false` and `AIVERSE_MORPH_TEXTURE_MIB` remain controls. Morph-aware autoLOD **reduces triangle/index work only**, not vertex buffer or source morph texture VRAM. Skinned auto-deformation-aware decimation, per-channel retopology and morphological VRAM reduction remain OPEN.
- Studio native `StudioMetalScene` owns one `RenderDeviceLifetime` epoch; its `StudioFacialGpuPreview` has a weak lease that enforces exact GPU owner thread, device and generation. It still owns its Model directly and uses existing `waitForResourceIdle()` fallback to safely retire replaced models. Device epoch is NOT a GPU completion token and cannot replace a real fence.
- Scene, Material preview and HTML5 Assets GUID + Lock remain untouched. Stage 185's one Animator / compiled FacialPreviewSession / real native Renderer3D pathway is retained.

## Verification actually performed

**ACTUAL:** Connected GitHub source audits on current Studio/animation branches, LCS review of FBX & glTF source transformations, exact GitHub code commits with file source readback, Stage 182 AutoLOD CPU test transplant, Stage 185 52-channel imported glTF fixture extended with semantic authored LOD map. The native test file has been authored and registered in existing CMake; **no C++ executable was run**.

**NOT RUN:** CMake configure/build/link, CTest, 52-channel glTF/GLB/FBX in-between artist assets, Scene viewport native Metal pixels, Vulkan/GLES shader build/pixel parity, shader U4 validation with actual GPU, resource epoch/reuse fault injection, Thread/Address/Undefined Behavior Sanitizers, 1k/10k/100k crowd, p50/p95 CPU frame/VRAM and memory budget checks. **This is SOURCE IMPLEMENTED, NATIVE UNVERIFIED**; do not claim tested or green CI.

## Deferred owner Mac commands

```sh
git fetch origin
git switch integration/motionweave-studio-facial-stage184
cd apps/studio-web
npm run typecheck && npm run build && npm run test:web
cd ../..
cmake -S . -B build/studio-stage186 \
  -DAIVERSE_BUILD_STUDIO_HTML5=ON \
  -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_TOOLS=ON \
  -DAIVERSE_RUNTIME_AUTOLOD=ON
cmake --build build/studio-stage186 --target \
  aiverse_studio aiverse_auto_lod_tests \
  aiverse_studio_facial_runtime_tests \
  aiverse_studio_facial_preview_binding_tests
ctest --test-dir build/studio-stage186 --output-on-failure \
  -R '^(auto_lod_runtime|studio_facial_runtime|studio_facial_preview_binding|studio_facial_rig_document)$'
```

After source feature closure, the owner's one coordinated full native Mac acceptance must include real FBX/ufbx in-betweens, authored skinned LODs, 1/8/16/52/64+reject65 GPU rigs, morph crossfade at edge distances, resource lifetimes including failed GPU completion and device recreation; build/check Metal, Vulkan and GLES where available. Do not claim feature release before logs.

## Readiness table

| Component | Source status | Outstanding |
|---|---|---|
| Universal Animator/Graph and facial session | Stage 185 source | Native compile and artist QA |
| 64-channel sparse MSL/GLSL render | Stage 185 source | Real GPU parity / benchmarks |
| Studio 3D native facial viewport | Stage 185 source | Mac screenshots / UI validation |
| **FBX blendshapes/in-between & animated morph weights** | Stage 186 source implemented | Artist FBX/ufbx runtime proof |
| **Auto unskinned morph index LOD** | Stage 186 source implemented | CPU fixture + GPU LOD transitions |
| **Authored glTF morph-aware (including skinned) visual LOD remap** | Stage 186 source implemented | Real skinned morph/IK pixels |
| **Conservative morph bounds and GPU texture alias** | Stage 186 source implemented | Culling/VRAM tests |
| **Studio GPU generation-safe lease** | Stage 186 source implemented | Native rebuild/device-epoch tests |
| Actual shared `ModelAssetProvider` + `ModelReadLease` + nonblocking GPU retirement | **NOT INTEGRATED** | Major Stage 187 source work |
| Automatic skinned mesh decimation + morph vertex/VRAM compression | **NOT IMPLEMENTED** | Offline/compute deformation-aware generation |
| CMake/CTest/Metal/Vulkan/GLES/sanitizers/perf | **NOT RUN** | Coordinated owner-Mac verification |

## Exact next Stage 187

Implement genuine shared Model provider and residency **without replacing Studio's divergent Model/Renderer3D source wholesale**. Compare peer `ModelAssetProvider`, `ModelResourceCache`, `ModelReadLease`, `AsyncModelCpuQueue`, `ModelStagingTransaction`, `ModelTextureCache`, `GlobalGeometryPool`, uploadModel signature and RHI fences. Stage adoption should first establish a type-safe transient read lease that spans synchronous Renderer3D frame submission, then async CPU decode with owner-thread-only GPU publish, rollback-safe texture cache and generation-checked GPU retirement. Do not copy the provider without its texture and transaction dependencies. Before switching owners, protect the last-good Model on failed import/compile/GPU upload; bind project-specific asset GUID/revision to source and invalidate stale resolved paths. Verify safe project change, active frame, reimport and failed completion. Keep explicit fallback to conservative GPU idle barrier where token completion is unknown.

**Update this handoff after actual source work**, keep PR #89 and #22 DRAFT, resolve the continuously moving Studio base conflict-aware and do NOT merge into main until native tests succeed.


## Stage 186 final owner-frame selection correction

- `0c333331f0d6b47d0ed42f7d1293bf8fd4aed2bc` — `apps/studio-native/mac/StudioMetalScene.cpp`: while facial (or material) preview is visible, native viewport clicks are not forwarded to unrelated MapDoc entity picking behind the imported Model. Existing Scene selection remains unchanged outside the preview.
- **ACTUAL:** GitHub source commit/readback and exact final remote HEAD verification, including `kMaxMorphTargets=64`, FBX named/in-between import, morph AutoLOD source aliases and Renderer3D target morph remapping.
- **NOT RUN:** native compile/CTest, Metal pixels, Vulkan/GLES parity or full GPU-lifetime checks. Next Stage 187 and full acceptance remain unchanged.
