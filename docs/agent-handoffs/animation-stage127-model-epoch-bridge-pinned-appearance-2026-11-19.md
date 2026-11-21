# AIVERSE MotionWeave / Universal View — Stage 127 handoff

Date: 2026-11-19
Branch: `feature/universal-ecs-animation-pipeline`
PR: https://github.com/chromealex/AIVERSE/pull/22 (Open / Draft)
Previous: [Stage 126](animation-stage126-published-lod-hot-reload-2026-11-19.md)

## Starting point and repository/parallel-agent contract

- Continued Stage 126 HEAD `548f75f0ce824db6f468f6577da95387d388ea0b`.
- At start: `main` `d71c6d47e532b25fb816a8bb1f96c41e5275a165`,
  feature ahead 927 / behind **0**. After source changes,
  feature ahead **931** / behind **0**, code HEAD
  `db98d31e4d1e0fa21538794fb8efbf253fe15224`.
- No force push, rebase or changes to main. GitHub PR #22 stays
  **Draft**, and PR base is `feature/universal-entity-inspector-fps`
  (not main). Before the next stage compare latest main, PR base
  and related parallel branches; other agents own NEREID, PHYSIS,
  TerraWeave, Prisma, SpectraFX, batching and texture/RHI.
- Scope is ONLY the genre-independent animation source bridge
  / View presentation model-epoch checks and native
  CPU appearance packet lifetime. No gameplay/authoritative
  ECS changes, simulation state, GPU owner or additional
  AnimationGraph update was added.

## Completed code and exact source commits

1. `c6939197ecd8dfdb930c2e1265aa9f35cd083f91` —
   `libraries/include/aiverselib/animation/MotionWeaveSourceBridge.h`:
   - Appended optional `modelPublicationGeneration` to both
     `AnimationSourceFrame` and the
     `AnimationSourceLinkComponent` stored in the presentation
     World. The new field is after the legacy aggregate fields
     to preserve existing C++ native source initialization.
     Zero remains compatible with older non-hot-reload hosts.
   - `MotionWeaveSourceBridge::sync` now treats a changed Model
     *publication epoch* as an asset replacement even when a C++
     allocator reuses the exact same Model pointer. A new/changed
     graph or Model epoch performs a player restart and invalidates
     old montage/notify state; an epoch-change must NOT transfer
     old playback pose/clip phase to an unrelated rig.
   - Prevalidates the complete incoming source list before
     mutating World: if an existing source has changed NONZERO
     modelPublicationGeneration but both compiled AnimationGraph
     pointer and Model pointer are the same, reject with a
     diagnostic invalid_argument. An in-place re-import MUST
     recompile its AnimationGraph before using the changed Model.
     Without this check a graph compiled for old node/clip data
     could silently use a new rig at the same C++ address.
   - Adds `AnimationBridgeStats::assetEpochRestarts`
     (counts actual asset-generation-triggered restarts).
   - Does NOT turn the Model revision into a gameplay generation
     and does not change the host-owned logical source identity.
2. `15e3129d30d41c145267b14e156b57bd4214606c` —
   `engine/view/View.h`:
   - `View::update` forwards
     `Object::animationModelGeneration` through the new
     `AnimationSourceFrame::modelPublicationGeneration`
     to the presentation ECS bridge.
   - `submitOne` accepts a previously evaluated MotionWeave
     pose only when both source *logical* generation and the
     last synchronized Model *asset publication* generation
     match the current Object, along with the already existing
     Model/Graph pointer and pose layout checks. A stale pose
     is not handed to Renderer3D after a Model reimport.
   - Graph-free native/FPS precomputed poses still come from
     the host's own independent presentation World. The host
     remains responsible for giving correct precomputed data.
3. `d4d0c1747b2831fdbb53a8263fde4ec0da26d353` —
   `engine/view/AppearanceAssetRegistry.h`:
   - New `view::PinnedAppearanceBatch` holds a vector of
     `AppearanceAssetRegistry::Snapshot` and corresponding
     `view::Object` packets. It retains the immutable
     appearance material arrays and graph shared pointers
     through an entire `View::update` + synchronous
     `View::submit` transaction, even when the live registry
     hot reloads and publishes a replacement key.
   - `reserve(count)`, `add(snapshot,world)`,
     `objects()`, `edit(index)`, `size/empty`,
     `clear()`. A failed add leaves the existing batch
     unchanged (including rollback if the Object vector
     insertion throws); null snapshots return false.
     Do not clear or mutate packets until the worker join
     and synchronous View submit are finished.
   - The snapshot does **NOT** own raw Model pointers or
     GPU buffers/textures. The host MUST separately retain
     its loaded Model and GPU epoch through the draw and
     resource retirement fences. This is CPU appearance
     lifetime pinning, not automatic full Model ownership.
4. `db98d31e4d1e0fa21538794fb8efbf253fe15224` —
   `AppearanceAssetRegistry.h`: added
   `PinnedAppearanceBatch::span()`, an explicit
   `std::span<const view::Object>` for native
   `View::update(span,dt)` and `View::submit(span)`
   without constructing a second Object vector.

## Native usage and timing contract

```cpp
aiverse::view::PinnedAppearanceBatch batch;
auto hero = appearanceRegistry.find("hero");
auto environment = appearanceRegistry.find("door");
batch.reserve(2);
batch.add(hero, heroWorld);
batch.add(environment, doorWorld);
// Add optional LODs/attachments to batch.edit(index) before the frame.
if(!batch.empty()) {
    view.update(batch.span(), dt);  // only ONE MotionWeave update
    view.submit(renderer, batch.span());
}
// After View update / worker join and synchronous renderer consumption:
batch.clear();
```

- The batch pins appearance/graph/material CPU snapshots only.
  A newly published registry Asset does not replace or destroy
  material/graph content used by a pending previous snapshot.
  The raw Model and its GPU resources are borrowed and must stay
  alive independently (e.g. host ModelResourceCache + GPU fence).
- `AnimationSourceFrame` sourceId + logical generation are NOT
  Model asset versions. `Object::animationModelGeneration`
  is the separate nonzero host `ModelPublicationEpoch` issued
  on asset reimport; zero retains legacy mode for hosts without
  Model epoch integration. Do not change model revisions secretly
  without new compiled graph and deferred resource retirement.
- If Model epoch changes by reimport but the same compiled graph
  pointer is submitted, `sync` throws before changing the World.
  Native hosts should stage BOTH Model and Graph as a transaction
  and only publish after source compilation succeeds.
- MotionWeave graph-only hot reload where Model epoch stays
  constant may still migrate named clip/state playback from Stage
  119/126. Changed Model epoch never migrates source playback.
- Do not call both `View::update` and an FPS/native host
  precomputed-pose evaluator for the same source. Use the
  Stage 122 `beginPreparedFrame` path for independently
  precomputed FPS/native characters.
- The original View::Object field order is unchanged, preserving
  old aggregate source initialization. `AnimationSourceFrame`
  new field is appended rather than inserted earlier.

## Actually performed verification — source-only

- Connected GitHub latest PR/head/main compare and Stage 126 handoff.
- Inspected `MotionWeaveSourceBridge.h`,
  `EcsAnimationSystems.h`, `View.h`,
  `AppearanceAssetRegistry.h`, `AppearancePacket.h` and
  precomputed FPS character adapter; read back modified
  source and checked new members/actions exist.
- Targeted GitHub commits with current blob SHA; no branch
  force push or unrelated code changed.
- **NOT RUN:** native checkout, CMake configure, C++ build/link,
  CTest, CI, Metal/GL/Vulkan real rendering, FPS/RTS/editor
  screenshots, ASan/TSan/UBSan, perf benchmarks, threaded
  ownership fault injection or hot reload simulation tests.
  Code remains **UNVERIFIED for compilation, runtime and
  visual correctness**. PR #22 stays Draft.

## Caveats and Stage 128 exact next work

1. Source bridge rejects an in-place Model replacement when the
   Graph pointer remains the same but both known asset epochs are
   different. Hosts must stage/recompile and publish a new graph
   with the updated Model. This is an intentional safe failure
   instead of silently running with stale node layouts.
2. `PinnedAppearanceBatch` does not own GPU/Model resources.
   The host must retain Models through animation worker joins
   and draw; a pinned Asset shared_ptr cannot delay a separate
   manual `releaseModel` call.
3. Zero Model revision remains a compatibility opt-out: old
   callers cannot get asset-epoch protection until upgraded.
   Incrementally migrate remaining native/editor/RTS hosts
   with explicit ModelPublicationEpoch and bounded cache
   lifetime without genre checks inside common View.
4. Batch snapshot pins do not substitute for the existing
   `AppearanceAssetRegistry::compileVisualLod` and generation
   matching. On actual Model reimport build new immutable
   visual LOD plans for the new source and visual revisions.
5. Consider native authoring/resource diagnostics for pending
   Model retirements and stale graph resolution, and retain
   source clocks across compatible graph-only reload.
6. After all agreed features, run native CMake/compiler,
   CTest, GPU render parity, editor and FPS/RTS hot reload
   tests, sanitizers, warnings fixes and crowd benchmarks.
   Do not merge PR #22 without real results.
7. Stage 128 must refresh `main`, PR base and relevant peer
   branches; produce a separate Stage 128 handoff and update
   `AGENTS.md` after every completed stage. No force pushes.
