# MotionWeave — automatic morph-preserving index LOD (Stage 182)

> 2026-11-20. Implemented in source on `feature/universal-ecs-animation-pipeline`. Native compile/CTest/RHI verification **NOT RUN**.

For imported **unskinned** GLB/glTF/FBX meshes without an authored LOD chain, the existing `prepareModelUpload(Model&)` / `generateMissingModelLods(Model&)` CPU stage can now produce 1–3 index-only coarse triangle streams even when the mesh has facial blendshapes.

The source must expose complete, unique, exact ordered semantic morph names (`mesh.extras.targetNames` for glTF; real importer-authored names for FBX). Every primitive in the morph mesh needs matching channels and a contiguous original vertex/delta span. No anonymous ordering assumptions are made. Existing `ModelLod::morphSourceForTarget` represents the identity for generated variants, so the parent Graph or facial rig continues supplying **one** pose for all steady LODs and fades.

The simplification metric protects all 64 possible channels through accumulated *signed POSITION and NORMAL deformation envelopes*, in addition to base vertex UV/tangent/color/lightmap attributes. Because a generated coarse mesh retains original vertex indices, it also retains all 52–64 semantic morph weights and the exact original GPU delta layout. `Primitive::morphAliasMesh` and `morphAliasPrimitive` identify the source delta owner; `uploadModel` checks this relationship and reuses the source `morphTexel` after allocating the original RGBA32F texture. Coarse bounds retain the source morph envelope. This is **not** a second GPU morph upload and does not spawn another AnimationGraph evaluator.

**Important limitation:** this is a *triangle/index-count* LOD, NOT mesh vertex decimation or morph texture compression. It saves GPU raster/vertex index work but retains the full source vertex buffer and full 64-channel morph texture; do not claim VRAM savings. Real-time skinned morphs, material variants, displaced/cutout/transparent geometry and authored LODs are not modified. To reduce skinned deformable vertex counts or VRAM, a different offline remap/resampling strategy with real joint weights and per-channel morph correspondences is necessary.

Model-level switches are unchanged: `batching.autoLod=false` (asset sidecar) or environment `AIVERSE_AUTOLOD=off` disables CPU generation; `AIVERSE_MORPH_TEXTURE_MIB` limits upload of source morph texels. No new serialization or platform shader variant is required.

Deferred native validation:

```sh
cmake --build build/ecs-test --target aiverse_auto_lod_tests
ctest --test-dir build/ecs-test --output-on-failure -R '^auto_lod_runtime$'
```

A new 52-channel fixture in `tests/render/auto_lod_runtime.cpp` checks semantic ordering, unchanged original deltas, bounded coarse geometry, exact source alias, missing-name refusal and malformed-delta refusal. It is committed as source but has **not** been run.
