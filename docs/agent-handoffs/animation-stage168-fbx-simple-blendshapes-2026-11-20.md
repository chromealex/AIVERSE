# AIVERSE MotionWeave — Stage 168: FBX simple blendshape import

Date: 2026-11-20
Branch: `feature/universal-ecs-animation-pipeline`
Stage 167 starting SHA: `fab502c6808b43ec89e72b8a57f89d4ea0a8d895`
Draft PR: https://github.com/chromealex/AIVERSE/pull/22 — DRAFT/unmerged, no permission to merge before completion.

## Source implemented

1. Commit `2a30d4c8890ec141bce397ada84670820d303598`, `engine/render/ModelFbx.cpp`: For ufbx `mesh.blend_deformers[].channels[]`, read ordered REAL names and single-target shape references, rejecting duplicate/empty names, more than `kMaxMorphTargets`, absent target shape, or in-between channels. Store names in Stage 167 `MeshDef::morphTargetNames`, store normalized default weights in `MeshDef::weights`, and create `Primitive::morphTargets` plus interleaved `morphDeltas` in existing glTF-compatible `[channel][finalVertex](pos.xyz, normal.xyz)` format.
2. Preserve the original FBX control-point index as `RawVertex::sourceVertex`; after material splitting, face-corner deduplication and skin joint-palette splitting in `appendChunk`, transfer each shape's offsets via ufbx `ufbx_get_blend_shape_vertex_offset` and `ufbx_get_blend_shape_offset_index`. This avoids wrongly indexing shape offsets with triangle corner or rebased GPU vertex indices. An unavailable normal offset defaults to zero; nonfinite offsets reject import.
3. Commit `8f550d4feffb13c72c166275987f7b9d7d79a639`: Set every FBX mesh-instance `Node::weights` from its mesh default shape weights, equivalent to the existing glTF node default morph weights. No animation Graph/player or genre-dependent code was touched.

## API / integration

No new loader API: `render::loadModel(... .fbx ...)` invokes `loadFbx`. Imported output uses existing `Model::meshes[*].morphTargetNames`, `Model::meshes[*].weights`, `Model::nodes[*].weights`, `Primitive::morphTargets` and `Primitive::morphDeltas`. Stage 167 `aiverse::view::compileImportedModelMorphRetarget(source,visual,...)` can now receive FBX-origin semantic morph names (for simple single-target channels) in the same generic Model representation. Existing Model upload owns GPU texture lifetime.

## Explicit limitations and risk

- Only **one shape keyframe per channel** with a positive finite target weight. In-between/multi-shape keyframe interpolation and animated channel weight curves from FBX animation stacks remain **NOT IMPLEMENTED**. Reject unsupported channels instead of incorrectly displaying them.
- No claim of full Character Creator/MetaHuman blendshape parity. Shape vertex offset coordinate conversion/normal-space correctness for authored pivots and transform compensation require real artist FBX/GPU acceptance.
- Current ufbx loader option `UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY` and `UFBX_GEOMETRY_TRANSFORM_HANDLING_MODIFY_GEOMETRY` apply to the mesh; verify morph delta alignment with those transformations on real FBX files.
- Only up to the engine's `kMaxMorphTargets` (8) channels per mesh. Large facial-rig shape counts require a redesigned GPU/CPU morph data model (not silently truncated).
- Full Model GPU lifetime token-complete nonblocking shutdown is NOT IMPLEMENTED; this importer did not modify GPU fences.
- The existing glTF inline `extras.rtsLods` path still rejects morph LOD source/target meshes; Stage 168 does not lift it.
- Existing animation stacks currently bake TRS; FBX blend-channel animated keys are not exported to `AnimChannel::Weights`.

## What was actually verified

ACTUAL: Remote GitHub source audit of current `ModelFbx.cpp`, `ModelGltf.cpp`, `Model.h`, vendored `third_party/ufbx/ufbx.h` APIs and their named shape structs; SHA-guarded source commits and subsequent readback of FBX ↔ glTF morph packing.

**NOT RUN:** Native Mac C++ compiler/link, CMake/CTest, real FBX blendshape visual pixels, Metal/Vulkan/GLES GPU rendering, animation weight curves, ASan/TSan/UBSan, fault injection, crowd benchmark, VRAM. Stage 168 is SOURCE-IMPLEMENTED, not tested or ready to merge.

## Acceptance tests still required (deferred to full Mac campaign)

- Single-target named shape FBX, including POSITION and optional NORMAL offsets, reproduces the same GPU morph deltas as expected ufbx reference.
- Mesh triangles with shared face corners and split skin palettes; verify source FBX control-point offsets survive final vertex remapping.
- Multiple FBX mesh instances with independent default node weights; morph weight 0 and full target weight.
- Malformed/duplicate/empty shape names and >8 shape channels; in-between channel fails closed without corrupting previous published asset.
- Source/visual FBX/glTF semantic morph name remap using Stage 167, with publication epoch invalidation.
- Coordinate/unit/pivot transform parity on artist FBX samples, Metal/Vulkan/GLES backend parity, memory and performance tests.

## Readiness matrix

| System | Status | Remaining |
|---|---|---|
| Universal ECS MotionWeave/AnimationGraph | Incremental source integrated | Full native suite |
| Independent animated child Graphs | Stage 161–164 source | CMake/runtime and GPU pixels |
| Playback/parameter hot reload | Stage 163–164 source | Native continuity |
| Retarget safety and semantic morph LOD | Stage 165–167 source | Importer/rig tests |
| FBX single-target blendshape geometry/name/defaults | Stage 168 source | Real native/gfx tests |
| FBX animated blendshape weights/in-betweens | NOT IMPLEMENTED | Curve baking and interpolation policy |
| glTF morph inline LOD chains | NOT IMPLEMENTED | Mesh and GPU parity |
| Complex rig / IK / many-channel facial morph | PARTIAL | Advanced authoring pipeline |
| SceneLoadCoordinator | NOT INTEGRATED | Review peer branches |
| Full per-resource GPU retirement | NOT IMPLEMENTED | Cache/texture/rollback/fence |
| Native compile/CTest/backends/sanitizers/perf | NOT RUN | One final Mac acceptance |

## Stage 169

Implement FBX blend-channel animation weight curves with correct ufbx interpolation and normalized values, or reject/flag unsupported curves explicitly, then tackle the 8-channel limitation/complex morph. Continue updating separate handoff after every step. Do not merge while feature suite or acceptance remains open.
