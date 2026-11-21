# AIVERSE MotionWeave / Universal View — Stage 122 handoff

Date: 2026-11-19
Branch: `feature/universal-ecs-animation-pipeline`
PR: https://github.com/chromealex/AIVERSE/pull/22 (Open/Draft)
Previous: [Stage 121](animation-stage121-universal-lod-attachments-2026-11-19.md)

## Starting point / git graph

- Started from Stage 121 HEAD `bf3afef7d3920b150091a162fd9c918618999363`.
- Compared to `main` `d71c6d47e532b25fb816a8bb1f96c41e5275a165`.
  At start feature was ahead 841 and behind 0; after the Stage 122
  **functional** commits it was ahead 859 / behind 0.
- All changes were written to the same feature branch with GitHub
  blob-SHA guards. No forced update, main merge, main mutation or branch
  reset. Main was already an ancestor, so no additional merge was needed.
- PR #22 remains Draft, and its PR base is the older
  `feature/universal-entity-inspector-fps` branch; inspect PR base and
  current main before any eventual merge.

## Implemented source code with exact commits

1. `0406b1083bc9781dc3823132ac941f82952c8178` —
   NEW `engine/view/ModelPoseRetarget.h`: genre-free visual LOD
   mapping `view::ModelPoseRetarget`, `validModelPoseRetarget`,
   `retargetModelPose`. An author specifies a target-indexed
   `sourceNodeForVisual` array. Every target node receives a source
   node index or -1 (keep target bind pose). Optional target-indexed
   arrays of source morph indices, -1 for unmapped channels, preserve
   mesh-local morph semantics.
2. `f9134a9516962271ef822e234c2e00067659c0f9` and
   `c471674fbb432869543c64dc62e48755208dbc45` —
   retarget validation accepts importer model node arrays in any
   order, rejects cycles, invalid indices, repeated source mappings,
   incompatible mapped source ancestry, out-of-range morph channels,
   invalid translationScale; and does not promise noexcept while
   allocating validation scratch.
3. `331ce844a7c0f93bb39f059ccec29093c7040fdb` —
   `engine/view/ModelComposition.h`: optional
   `ModelLod::retarget` shared immutable map, alongside the existing
   model and minDistance fields (so prior C++ aggregate initializers
   with two values stay source compatible).
4. `b3555d9c27d6e6609e89ad5cf8e4c5264d716da6` —
   `engine/view/View.h`: `submitOne` supports a remapped visual
   Pose and matrices for both MotionWeave-owned graph updates and
   independently precomputed FPS/native presentation poses. A visual
   pose is never directly shared across mismatched Model skeletons:
   strict exact layout is zero-copy; valid explicit map derives a new
   visual-local pose from **source animated delta relative to source
   bind** applied to the target bind pose; invalid or missing map
   renders target bind pose. Source rig and its animated socket
   matrices remain unchanged. All retarget scratch is consumed
   synchronously by `Renderer3D::drawModel`.
5. `6138c0bd4728d56f643a1e9ed0b6c9e8803565e2` —
   `modules/rts/view/RtsView.h`: optional validated
   `ModelChoice::poseRetarget` cache.
6. `b8e623ebc481d901502e1efe06a5ae2112c6b9e1` —
   `modules/rts/view/RtsLook.cpp`: compile name-derived
   source-to-visual mapping **once on LOD import**, accepting it
   only if the generic validator approves. This avoids per-actor
   remap construction; visual pose evaluation still happens per draw.
7. `a90b462e9627d8494624e644a2d335768eac3a9f` —
   `modules/rts/view/RtsViewDraw.cpp`: reuse the same explicit
   retarget algorithm for validated LODs, feeding its resulting
   Pose and matrices to `View::submitModel`. Keep the historical
   legacy world-matrix fallback for older LODs whose maps were not
   validated to avoid a silent appearance rewrite without visual
   testing; this fallback still needs follow-up safety evaluation.
8. `efa67a4f2d22015a7acec9377e4e1caf283b295b` —
   `apps/fps/client.cpp`: precomputed FPS character packets
   identify `animationModel` explicitly. The client continues to
   evaluate FPS characterPresentation only once; a bare
   `ScenePacketBatch` remains draw-only.
9. `bacd07f280c1be104af4fff4829f96e79594feb2`,
   `a7f2ae33d65a61cc209f36a086c0951fda28b52b`,
   `5b85b9cb510a27bba935cc34639f982a643bdf6c` —
   `View::beginPreparedFrame` lets precomputed FPS/native
   streams manage LOD hysteresis and stale IDs WITHOUT calling
   `View::update` (which would double-update their graphs).
   FPS calls this once before assembling its remote character
   draw packets. Stage 121 Model/attachment packet field order
   remains unchanged.
10. `e5e38a04f85ea53692d8761e86dfb113a568aff0` and
    `d4ddbe9b6d5de99bf5ff7e4b25389fb871d1066d` —
    `RenderTexture::flushChecked()` returns false for failed
    scratch vertex-buffer allocation, retaining brush commands.
    The old `void flush()` is a source-compatible wrapper.
    `RenderTexture::SubmissionStats` counts failed brush,
    blit and upload-texture allocations.
11. `521fdbb0c789e76e228ffe128dc11b42f3995e1c` —
    `RtsPaintLayers.cpp`: no surface draw is acknowledged
    after a failed canvas `flushChecked`; queued brushes are
    retried and source ordering stays intact.
12. `336b7f7059534bd666edd7330d1fd26a9589a908`,
    `b7fea4dedc4f0e6921bc9e4222dcd512413beb4f`,
    `763ce256efdd81a55fd2e2d75059c1111139cd8b` —
    collect paint canvas submission diagnostics through the
    RtsPaintLayers -> RtsView -> Editor Stats chain. These
    counters cover currently live canvas owners; removed owner
    counters are not archived as a telemetry history.

## API contract and example

```cpp
auto map=std::make_shared<aiverse::view::ModelPoseRetarget>();
map->sourceNodeForVisual = {0, 4, 12}; // target node index -> source
// Optional: keep unmapped morphs at visual bind weights.
map->sourceMorphForVisual.resize(visualLod.nodes.size());
for(auto& channels:map->sourceMorphForVisual)channels.fill(-1);
// Example: copy source channel 1 into visual node 2 channel 0:
map->sourceMorphForVisual[2][0] = 1;
if(aiverse::view::validModelPoseRetarget(baseRig,visualLod,*map)) {
    actor.lods.push_back({&visualLod,40.f,map});
}
```

- `View::update(objects,dt)` owns one MotionWeave presentation graph
  update per frame. Never invoke it additionally for a source that
  an FPS/native host already updated. For precomputed poses use
  `beginPreparedFrame()` once each host render frame followed by
  `submitPackets()`.
- The explicit remap never modifies source animation, simulation,
  game physics or CPU/GPU model resources. Incompatible visual rig:
  show target bind pose, **not** unsafe source palette.
- Remapping uses target bind + source local translation/rotation/
  scale delta, optionally scaling local translations. Non-mapped
  target nodes remain in the target bind pose; mapped target node
  ancestry must agree with source ancestry.
- Morph mapping is opt-in and target-indexed, all entries default
  to **-1** when a map is explicitly allocated; do not rely on
  default-initialized integer arrays (which contain zero).
- Renderer3D already performs its native per-mesh LOD separately.
  The explicit whole-asset map is used only if that visual LOD
  actually differs from the source model.
- Host-owned Models, Graphs, maps, materials and Pose source
  pointers MUST stay alive until the joined presentation worker
  barrier and synchronous `submitModel` completion.

## Checks actually performed / limitations

- Checked current GitHub branch/PR/main ancestry, inspected native
  model, graph, View and adapter implementations; uploaded and
  re-read source edits with current file SHAs. Reconfirmed that
  renderer `drawModel` samples Pose during synchronous submission.
- **NOT RUN:** local checkout, CMake configure, C++ compiler/link,
  CTest, GitHub CI, Metal/Vulkan/GL device execution, real FPS/RTS
  screenshots, ASan/TSan/UBSan, memory or performance benchmarks.
  Stage 122 code is SOURCE-ONLY, unverified for native
  compilation/runtime. Keep PR #22 DRAFT.
- An implicit name-based RTS mapping is a convenience import path,
  not guaranteed retarget parity. Its historical fallback remains.
- This is delta-pose mapping, not automatic high-quality body
  proportion retargeting, twist/IK solver adaptation or mesh weight
  reauthoring; those need separately authored retarget profiles.
- `validModelPoseRetarget` currently does hierarchy checks and
  allocates on every generic View remapped submission. For large
  crowds, cache validated immutable mappings or create a compiled
  remap plan with generation signatures and measure performance
  before shipping. No performance improvement is claimed.
- Badly authored Model hierarchies, malformed morph indices and
  device loss still need native fault injection; no runtime claim.

## Exact Stage 123 and validation plan

1. Refresh `main` / animation branch and compare peers; no
   force push, no main modification, no overwrite of other agents.
2. Build a generation-safe compiled retarget mapping cache to avoid
   per-draw validation allocations for large crowds, with explicit
   asset versioning. Continue migrating remaining RTS legacy
   appearance LOD paths while retaining visual parity and source
   graph authority.
3. Define safe explicit retarget mapping for additional limb/twist
   rigs and morph authoring profiles, with binding diagnostics.
4. Keep `RenderTexture` submission errors inspectable in native
   hosts and the Editor, and evaluate the remaining standalone
   PostFx RTT ownership with its responsible domain agent.
5. FEATURES FIRST per user. Then run full native CMake, CTest,
   sanitizers, GPU parity + benchmarks; repair all errors/warnings
   and only then review PR #22 for merge. Append Stage 123 handoff
   and update `AGENTS.md`.
