# TerraWeave Caverns — Stage 37 signed six-face mixed-LOD handoff (2026-11-24)

## Provenance

Repository: `chromealex/AIVERSE`
Feature branch: `feature/terraweave-caverns-core-20261123` (**not merged into main**).

Stage37 commits (GitHub connector returned these commit SHAs):
- `8668397d0185cfdbdbf60dda9e08cb559347ecd9` — `engine/world/TerraWeaveCavernsLodTransition.h`: accept coarse on either sign of shared X/Y/Z face.
- `ed531d7358b7cae63bf556aa6f1cb469c25c8134` — `engine/world/TerraWeaveCavernsLodPolicy.h`: use signed six-face collinear stitching symmetrically.
- `abb002045d2b2828a8e2ca1090780ca500f514e3` — new `engine/world/TerraWeaveCavernsMixedLodPlan.h`: deterministic multi-face visual seam stitching and final verification.
- `ed7210b9250f7fa50e10c4ba3fc23bf258675595` — `engine/render/TerraWeaveCavernsBatchGeometry.h`: preserve separate sourceRevision vs render revision.
- `161f4a8a901f3b1d8e547bc3b1d32d06e42c0d2f` — `engine/render/TerraWeaveCavernsShardCatalog.h`: build GeometryScene models from visual stitched plan, leave native collision source unchanged.
- `7c40f2e2f9d42a4fdc85426f304ae33b0f810520`, `9e2328c1b3679e4bfc2b2746b931d04f741e4af1`, `ed104f31a0e7953ad9771eee7c353d7c0e6e5a88`, `9dc4da133a8dbd9271a3c5cbbc2f50ae93007d97` — propagate authored SDF epoch through editor, gameplay and GPU batched source data.
- `77e6fe6a09c68cb89b8f78a86553317b80db3994` — allow exact contour parity across distinct source revisions; reject stitching divergent contour geometry across source epochs.
- `5c4a4547441e8da2c5027d5404d5797a8562d3e5` — new `engine/render/TerraWeaveCavernsMixedLodShardBatch.h`: connected loaded neighbor traversal and cross-shard publication planning.
- `22b18b01ea36244f8039ec73a61ae07c5e4bf635` — optional shared cross-shard visual override in full-shard GeometryScene preparation.
- `1da148f6595b10842ffef0fdeeea49e30e9b86ca` — `CaveShardGpuHost` stages group and neighboring coarse-patched GPU shards together; exposes fallback rebuild list/source conflict.
- `986bcb1806f015a9e885d7f8bf8b283848b058a1` — Stage16 seam gate allows supported subdivisions.
- `d267b5f4f4552a5fbc6ac1c2efcebb93d0077900` — request common-lattice regeneration on authored source epoch conflicts.
- `cfee3614e12bed3b61776a11b559e6557f5a80d5` — Stage22 CPU worker per-key LOD override and auto queue of shared-lattice repairs.
- `7bb072b9181586e500d36ccf2ee70cb5b3291e0a` — divergent even-same-resolution faces force rebuilding BOTH sides.
- `8b13af88b619eabf9af8076d32c5810fe08121ac` — trigger async seam fallback requests from prospective CPU edit batch.
- `a2a944ec01a1622f6e1cd0cf9c141c10d0843b7e` — Stage34 gameplay host requests repairs discovered by cross-shard GPU planning.
- `d7e9c59051bf3ed6c3a53370fdf2d58ab28e4908` — expose one planned GPU shard staging method for per-frame pacing.
- `aafb3ad6fefef4dfaab4ef58772ab9b11851ae1b` — Stage32 Editor preview uses the ONE cross-shard plan and the existing GPU pacing budget.
- `7809fb6c3477890817422694ab71ec617a6a68e2` — new `engine/world/TerraWeaveCavernsDistanceLod.h`: distance policy, hysteresis, constant-span mesh LOD.
- `f6e582771725000299b041b8da0c0217ba95f873` — Stage36 residency selects visual LOD per camera-relative chunk.
- `9b909631b68fd0fff26ec17aa2354294a38f54be` — Stage36 async streaming workers build independent visual and full-resolution authoritative collision meshes.
- `ce9c7bd389ed44d871388047917e09176fc78c52` — native streaming callback receives both visual and collision geometry; adaptive LOD exposed in runtime frame.
- `0433e93510297e31f95d0563b6f93caf23a15b6c` — legacy/unversioned source never silently stitched against an incompatible source.
- `1b78c7717757cb76406b59597b6b950d105012ab` — independent include declaration correction.
- `c595f35374854ee1bdb896e75a09b09c88bc6d07` — mandatory `skills/world/aiverse-terraweave-caverns/SKILL.md` update.
- This handoff is the final Stage37 commit; read current HEAD for its SHA.

## Architectural changes

1. **Symmetric six-direction visual face stitching:** Previous Stage14 could split a coarse edge only when coarse stood on the negative side. Both positive and negative adjacency for each of X/Y/Z are now accepted. `resolveCaveFaceLod` routes to the appropriate coarse/fine source and modifies only a copy of the coarse visual mesh when the finer edge segments exactly partition the coarser contour. Divergent topology is NOT bridged speculatively.

2. **Multi-face coherence:** `planCaveMixedLodVisual` collects versioned `CaveCertifiedChunk` meshes, walks three canonical positive axes (covering every adjacent pair once, regardless of which side is coarse), applies multiple compatible subdivisions to the same coarse chunk, then re-audits ALL resulting face-edge sets after the complete batch. A repair map describes each unresolved face's lowest shared spacing, target resolution, and chunk key; mismatched source epochs with non-identical contours are flagged and both sources must rebuild from one frozen snapshot. A successful result is presentation-only.

3. **Cross-shard planning:** `planCaveMixedLodShardBatch` joins staged snapshots of all edited shards and connected currently loaded resident neighbors, up to a bounded source cap. Whenever a patched coarse visual source is located in an UNEDITED neighboring GPU shard, it adds that shard's complete source list to the same prospective batch, enabling a simultaneous owner publication rather than rendering only one side of a repaired seam. Unknown authored epoch sources require regeneration if stitching would otherwise be used. A mismatched/over-budget connected world fails closed.

4. **GeometryScene and STRATA source route:** `CaveBatchChunk::sourceRevision` is distinct from `revision` (render source). `prepareFullCaveShardPresentation` accepts an optional verified global plan, derives only visual model triangles from stitched copies, and leaves catalog/native Box3D+SpatialWorld geometry untouched. `CaveShardGpuHost::stagePreparedEdits` now uses one global LOD batch, stages every required shard or none, publishes only under the existing owner barrier, and exposes `lodRebuilds()` when incompatible contours require CPU work.

5. **Actual fallback work:** `CaveSeamPublicationGate::analyze` certifies truly supported collinear subdivisions instead of always forcing fine LOD. Divergent edges, including two sources with equal nominal sampling but incompatible contours, request repairs of both sides. `CaveEditRuntimeBridge::requestLodRepairs` enqueues affected chunk cells in its existing versioned dirty queue, stores specific per-key meshing resolution/spacing (preserving world chunk span), invalidates stale candidates, and lets its normal async workers regenerate those chunks using the latest immutable SDF. `tick` attempts this after collecting a full prospective edit batch. Stage34 game GPU staging can also enqueue repairs discovered by Stage37's cross-shard audit. **Dirty gameplay ACK continues to require the complete Stage35 external physics/GPU/NavMesh commit.**

6. **Distance-selected visual resolution and authoritative physics:** `CaveDistanceLodSettings` and `caveDistanceLod` give a configurable per-chunk Chebyshev-distance policy with hysteresis. `CaveWorldResidency::enableDistanceLod` invalidates and regenerates a versioned streaming entry only when its desired visual LOD changes. `caveVisualLodMeshSpec` divides resolution and multiplies grid spacing by the same power of two, preserving the chunk's physical world extent. Stage36 `CaveWorldStreamJobs` now returns a coarsened `CaveStreamCpuResult.mesh` (presentation) AND an always full-resolution `collisionMesh` (gameplay); the native build callback receives both and MUST use the latter for Box3D/SpatialWorld.

7. **Editor partial upload pacing retained:** `apps/editor/Editor.cpp` now computes and retains one fully negotiated cross-shard visual plan per authored epoch, requests CPU repair on incompatible faces, stages only one affected GPU shard per renderer device frame with the existing Stage32 pacer, then visual-only activates every prepared shard together. Editor preview does NOT ACK gameplay edits or update physics/NavMesh.

## Changed/new files

New:
- `engine/world/TerraWeaveCavernsMixedLodPlan.h`
- `engine/render/TerraWeaveCavernsMixedLodShardBatch.h`
- `engine/world/TerraWeaveCavernsDistanceLod.h`

Modified:
- `engine/world/TerraWeaveCavernsLodTransition.h`
- `engine/world/TerraWeaveCavernsLodPolicy.h`
- `engine/world/TerraWeaveCavernsSeamGate.h`
- `engine/world/TerraWeaveCavernsEditRuntime.h`
- `engine/render/TerraWeaveCavernsBatchGeometry.h`
- `engine/render/TerraWeaveCavernsShardCatalog.h`
- `engine/render/TerraWeaveCavernsShardGpuHost.h`
- `engine/render/TerraWeaveCavernsEditPresentation.h`
- `engine/world/TerraWeaveCavernsGameplayEditHost.h`
- `engine/world/TerraWeaveCavernsWorldResidency.h`
- `engine/world/TerraWeaveCavernsStreamJobs.h`
- `engine/world/TerraWeaveCavernsRuntimeFrame.h`
- `apps/editor/Editor.cpp`
- `skills/world/aiverse-terraweave-caverns/SKILL.md`
- This handoff.

## Known limits and next features

- The LOD transition subdivides only genuinely collinear coarse/fine boundary contours, with a verified exact fine-edge partition. For arbitrary divergent SDF contours, the correct strategy is CPU regeneration on one common lattice. This could temporarily promote multiple neighboring chunks to finer mesh resolution and raise GPU/CPU cost.
- A single large contiguous loaded cave volume beyond the bounded cross-shard planner limit must be split into smaller publication regions with explicit certified border leases. Stage37 fails closed rather than silently omitting distant neighbors.
- New native streaming callback signature `StreamBuildCommit(ticket,visualMesh,collisionMesh)` is intentionally different from Stage36's old two-argument form. Agent integrating the actual client/server must adopt the new API, keeping authoritative collision independent of view distance.
- The default renderer path remains GeometryScene with STRATA selection configurable, not device-verified. GPU resource retirement still depends on the Stage24/Stage38 graphics owner lifetime.
- Stage34–36 are reusable runtime hosts, not yet automatically registered into an actual native client or dedicated server. Stage35's final Box3D body destruction can require a full world reload if a late operation fails; do not claim atomic rollback.
- The Stage22 fallback queue triggers only for loaded-neighbor certified candidates. Camera-based streamed chunks published from a different path must still be staged with a matching full cross-shard visual plan and must not ACK a Build ticket on CPU completion.
- **Potential compile/acceptance risks:** no C++ compiler/build was run in this stage, so the expanded header includes, C++20 API use, rendering model manifests and pointer lifetime need verification during final feature-completion QA. Actual screen-space watertightness (all 6 axes, corners, negative world chunk keys, thin walls, empty/nonempty transitions, and author version rollback), Metal/Vulkan/GLES geometry and Box3D/nav interactions are unverified.

## Tests and actual verification

- User explicitly ordered **all features before tests**. No Stage37 test source was added and no CMake, CTest, native compiler, hardware GPU, Box3D or NavMesh test was executed.
- All above source and skill changes were acknowledged by the GitHub connector with concrete commit SHAs. That confirms source publication ONLY, not native build or runtime correctness.

## Stage38 next

Implement nonblocking GeometryScene/STRATA resource retirement with actual GPU completion fence tokens (no full device-idle sync per painted cave dab); bounded GPU upload bandwidth and GPU owner-pressure handling. Complete a safe capacity/backpressure state for mixed-shard LOD rebuilds, including deferred dynamic resolution changes without dropping last-good presentation. Keep a separate skill and dated handoff. Later stages must still attach Caverns to the actual native client/server frame host and repair the Stage35 world recovery path before claiming full in-game cave editing.
