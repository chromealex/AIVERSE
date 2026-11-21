# AIVERSE MotionWeave / Universal View — Stage 121 handoff

Date: 2026-11-19
Branch: `feature/universal-ecs-animation-pipeline`
PR: https://github.com/chromealex/AIVERSE/pull/22 (**Open / Draft**)
Previous: [Stage 120](animation-stage120-canvas-gpu-retirement-2026-11-19.md)

## Git status and ownership

- Continued Stage 120 HEAD `1e1153ed9bba9767fe2ebd5f2cf0c84cf5e203f9`.
- At start, GitHub compared feature with `main`
  `d71c6d47e532b25fb816a8bb1f96c41e5275a165`:
  ahead 824 / behind 0. After source changes, ahead 833 / behind 0.
- Only this feature branch was modified; no rebase, force push or main
  mutation. Do not overwrite parallel agents' NEREID/PHYSIS/Prisma/
  SpectraFX/TerraWeave work. Refresh refs before further edits.
- Scope: genre-independent model/whole-asset LOD/socket composition,
  legacy RTS adapter migration, and retryable canvas upload state.
  No new simulation authority, AI/physics, or animation update clock.

## Code changes with exact commits

1. `d321eed4e4341016058bf0b626b47e556cd73833`
   **NEW** `engine/view/ModelComposition.h`: generic C++ helpers
   `ModelLod`, `selectModelLodLevel` with bounded distance
   hysteresis, `compatibleModelPose`, `ModelAttachment`,
   and `composeAttachment` (index/named node). All GPU models,
   materials, attachments and poses are borrowed host assets.
   Attachments use the host's model-space animated node matrices
   or bind pose if a valid pose is absent. Invalid sockets fail,
   never silently attach at world origin.
2. `4ace7e7090a94877ba1d443efbfabf81e3f60473`
   `engine/view/View.h`: `view::Object` can optionally set
   `animationModel` independently of its visible ModelDraw,
   `lods`, `lodHysteresis` and child `attachments`.
   One animation source is synchronized with MotionWeave per
   sourceId/generation, regardless of selected visual LOD.
   Selection uses Renderer3D camera distance and per-View/source
   hysteresis memory, reset on source generation changes and
   pruned for absent IDs each `View::update`. Missing streamed
   LOD Models fall back to an available closer model. A new visual
   Model cannot reuse stale static caches, material overrides,
   node assignments, baked samples or GPU geometry policy from
   another asset. CPU pose/skinning matrices are only reused when
   exact skeleton/rest/skin contracts match.
   Attachments follow the BASE animation source matrices even if
   that skeleton is not present in the far visual LOD; a stable
   sourceId/generation/attachment-index draw ID is supplied when
   the child doesn't specify its own. Child draws do NOT advance a
   separate animation clock.
3. `1fb03e65804db3572d80101b1bedcf3b4b2360b0`
   `modules/rts/view/RtsViewDraw.cpp`: existing RTS weapon/cargo/
   banner socket visuals now use the same genre-neutral
   `view::composeAttachment` helper instead of manually
   multiplying frame transforms. RTS-owned filtering by weapon,
   carry and state stays in the RTS host. The source's
   `st.drawn` transform (NOT selected LOD fix) and existing
   model-space node matrices are passed through. Attachments
   retain their previous transient renderer identity.
4. `9b6fc18fa7486e2f922af1fc20df3c58db022cd4` and
   `084ec5abec4c45ac2de469f2c0eb7ff95a51e412`
   `engine/render/RenderTexture.h/.cpp`: `upload` now
   returns bool instead of void (legacy callers may ignore it).
   A failed source staging allocation or GPU blit returns false,
   allowing the presentation host to retry the same CPU bytes.
   Temporary staging textures still retire through the generic
   CanvasGpuRetirement fence queue introduced in Stage 120.
5. `cab96173cab581590a5f059171e5ec1e5e846fd1`
   `modules/rts/view/RtsPaintLayers.h`: each paint layer tracks
   `uploadRetry` so an uncommitted partial texture update is
   never mistaken for completed GPU content.
6. `e25f06e58bd2fec685be2fbe72a1e2dafeb72e40`
   `modules/rts/view/RtsPaintLayers.cpp`:
   - categorical raster/palette upload retains dirty flags when
     GPU staging or blit fails;
   - a failed initial coastline target creation does not
     acknowledge the sea mask hash;
   - territory scalar layers retry the full latest CPU image
     after a failed partial upload even if the next frame
     has no changed pixels;
   - a manual `copyFrom` command is not cleared when blit
     failed, so later strokes are not incorrectly applied first.
7. `80a468f7f4a928c9ce381dc9767a8528ce3ffb56`
   `engine/view/View.h`: Stage 121 properties appended AFTER
   the original `view::Object` fields, preserving the old
   positional aggregate source initialization order (not binary ABI).
8. `8f817f7540df0cf55c5136a29a40464c580a2b5a`
   `engine/view/ModelComposition.h`: morph-target indices are
   mesh-local, so source/visual Models with morph targets cannot
   blindly share Pose across distinct whole-model LOD assets.
   Such LODs render with their own bind pose unless the host
   supplies explicit remapping. The base graph clock continues.

## Public C++ example

```cpp
aiverse::view::Object actor;
actor.id = stableEntityId;
actor.generation = spawnGeneration;
actor.draw.model = &baseModel;            // base visual Model
actor.draw.world = worldTransform;
actor.graph = compiledGraph;
actor.animationModel = &baseModel;       // always the Graph model
actor.lods = {{&reducedModel, 30.f}, {&impostorModel, 100.f}};
actor.attachments.push_back({
    .draw = weaponModelDraw,
    .socket = -1,
    .socketName = "RightHand",
    .local = weaponGripTransform,
    .visible = true
});
view.update(std::span<const aiverse::view::Object>(&actor,1),dt);
view.submitOne(renderer,actor);
```

- `update` is called exactly **once** per presentation frame.
  Calling `submitOne` multiple times does not advance the graph.
  For an independently animated attachment, use a distinct
  `view::Object` with its own stable ID and graph.
- The passed Model/Graph/Material pointers must survive both the
  worker join and synchronous draw submission. LOD resources may
  be streamed/published between frames, never during workers.
- `compatibleModelPose` is deliberately strict: node order,
  names, parents, skin links, bind T/R/S and inverse bind
  matrices must match. Morphed separate LOD meshes need a
  dedicated channel correspondence API. Incompatible render
  assets never consume stale joint palettes or morph weights.
- `view::ModelAttachment::socketName` is resolved on the base
  animation rig per submission (not on a changing LOD).
  For crowds, prefer cached integer socket IDs in stable models.

## Actual validation performed

- Connected GitHub branch and PR lookup, comparison with main,
  targeted source inspection, commit writes with current blob SHA,
  and source readback. Confirmed the new API and RTS attachment
  call path exist in the branch.
- **NOT RUN:** local CMake configure/build/link, CTest, CI, real
  Metal/GL/Vulkan draw, FPS/RTS/editor visual parity, correctness
  of compiled bone/inverse bind data in real LOD assets,
  ASan/TSan/UBSan, benchmark or GPU failure injection.
  The source implementation remains **UNVERIFIED for compilation
  and runtime**. PR #22 MUST remain Draft.

## Known limits / future work

1. Whole-model LOD is opt-in and distance based; Renderer3D's
   existing per-mesh projected-pixel LOD remains independent.
   Some legacy/host-specific complex RTS appearances still
   choose their own LOD and will require further migration.
2. Compatible whole-model CPU animation reuse does not retarget
   rigs with changed bones or different bind poses. Such distant
   Models render in bind pose instead of silently corrupting skinning.
   Explicit hierarchy mapping/retargeting is future work.
3. The model source pointer is borrowed, not pinned by the new
   packet. Host hot reload must keep old resources resident until
   presentation worker barrier and draw completion.
4. The upload retry currently retains CPU source intent for
   categorical/territory/manual copies, but the `flush` method
   is still void and Lua does not get detailed GPU failure codes.
5. This stage does not claim all LOD/attachment authoring paths
   were converted, only the generic API and RTS socket pass.
6. No observed source-level compiler/link validation has been
   run. PR #22 remains Draft and must not be merged.

## Exact next Stage 122

- Refresh current `main` and all active feature branches, compare
  ancestry and avoid overwriting other teams' code.
- Integrate canonical View's source-rig/visual-LOD packet into
  remaining legacy RTS/FPS host paths where possible without
  changing visual parity, and add an explicit compatible
  retarget/morph LOD map for authored lower-detail skeletons.
- Make standalone canvas upload/retry and `flush` failure
  diagnostics explicit while preserving GPU async retirement.
- Finish feature migration first, then run full native CMake
  build/CTest, GPU backend editor/RTS/FPS visual parity and
  sanitizers/performance checks. Fix all warnings/errors.
  Only after that review Draft PR #22 for merge.
- Append **separate Stage 122 handoff**, update `AGENTS.md`
  with exact code/validation status.
