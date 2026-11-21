# AIVERSE MotionWeave / Universal View — Stage 126 handoff

Date: 2026-11-19
Branch: `feature/universal-ecs-animation-pipeline`
PR: https://github.com/chromealex/AIVERSE/pull/22 (Open, Draft; base `feature/universal-entity-inspector-fps`)
Previous: [Stage 125](animation-stage125-scratch-budget-model-epochs-2026-11-19.md)

## Branch, scope and concurrent agent safety

- Continued Stage 125 HEAD `2ae54740331d7ed6ba8d8e01e6c39e753391852f`.
- Initial GitHub compare vs `main` `d71c6d47e532b25fb816a8bb1f96c41e5275a165`: **ahead 913, behind 0**.
- After Stage 126 source commits: HEAD `daac085051d86294859cdb6ea6786a3301b0362a`, **ahead 918, behind 0**.
- No main modification, rebase, merge, force push or unrelated domain change. `main` was already an ancestor of the feature branch. Refresh latest main and all relevant peer branches before the next stage; NEREID, PHYSIS, TerraWeave, Prisma, SpectraFX and batching have independent owners.
- Owns genre-independent native `AppearanceAssetRegistry` publication rules and FPS animation presentation adapter. No gameplay ECS writer, physics authority, GPU lifetime, game-specific retarget system or additional graph tick was introduced.

## Source changes, exact GitHub commit SHAs

1. `4cca72e77034fc52ba085dc965d0fa15eebcd45a` — `engine/view/AppearanceAssetRegistry.h`
   - Previously every successful appearance publication called `ModelPublicationEpoch::next()`, even for graph-only or material-only edits. This unnecessarily invalidated already compiled source/visual Model LOD retarget plans.
   - `publish` and `publishPrepared` now preserve `modelPublicationGeneration` when the SAME key uses the SAME Model pointer, unchanged host `modelRevision`, unchanged model source path and no pending Model-source invalidation.
   - Added separate `dirtyModel_` state: `invalidateSources` marks a changed model source differently from a changed graph source. An in-place Model reimport may reuse a C++ pointer but the invalidation still issues a **new nonzero asset epoch**.
   - `dirtyModel_` clears only after a successful publication; it is preserved if compilation fails, as are the previous good appearance snapshot and epoch. `erase/clear` remove dirty flags. A new key always gets a new global Model epoch. All graph, material, texture and GPU lifetimes remain host-owned.
2. `d88687cbe89eb7707a4e3c36ade1cecfc99b13e7` — `apps/fps/client.cpp`
   - FPS remote precomputed character `view::Object` packets now forward the published `characterAppearance->modelPublicationGeneration`, in addition to `animationModel` and actor-specific `generation`.
   - This is separate from the network player life and optional GPU-baked crowd animation. It enables future native visual LOD `compiledRetarget` matching without adding another MotionWeave update.
3. `b17010fc4b791408dc60191b842bffad37ce40ef` — `apps/fps/client.cpp`
   - On graph-file hot reload of an unchanged `characterModel`, the FPS host now snapshots a healthy previous graph instance's named state and clip phases using `AnimationGraphInstance::snapshotPlayback()` **before** restarting its one presentation-ECS player.
   - The existing Stage 119 `AnimationPlayerComponent::pendingPlayback` is consumed once when `EcsAnimationSystems` resets the newly compiled graph, with `restorePlayback()` name/clip matching and authored-default fallback if incompatible.
   - Old pending snapshots are cleared before recapture. Old notifies/montage players remain cleared; root motion, IK, contacts, runtime native callbacks and authoritative gameplay state are NOT migrated. No duplicate animation tick.
4. `b8863e53db2c94e4cc77a8fd53c81740f62e3e35` — `engine/view/AppearanceAssetRegistry.h`
   - `Asset::compileVisualLod(const Asset& visual,float minDistance,const ModelPoseRetarget& authored,ModelLod& out,std::string* error)` is the new native C++ asset-publication handoff API.
   - Takes two immutable, already-published appearance snapshots, requires both borrowed Models and their nonzero publication generations, finite positive distance, and compiles the existing Stage 123 generation-safe `CompiledModelPoseRetarget`.
   - Output `out` is replaced **only after** compilation succeeds; on failure the host's last-good LOD remains unchanged. The optional message uses existing `describeModelPoseRetarget` authoring diagnostics. The helper pins only the compiled plan, NOT Model/GPU lifetime.
   - The host builds `view::Object::lods` with these returned ModelLod packets, and `Asset::packet` already supplies the corresponding source `animationModelGeneration`.
5. `daac085051d86294859cdb6ea6786a3301b0362a` — `engine/view/AppearanceAssetRegistry.h`
   - `publishPrepared` no longer incorrectly preserves an old graph-generation ID when the same Graph/Model pointer is supplied but host `modelRevision`, graph invalidation or model invalidation changed. The new generation signals a compatible or incompatible graph reload to the existing presentation bridge, while `ModelPublicationEpoch` can still remain stable if only the graph changed.

## Public C++ contract and example

```cpp
using aiverse::view::AppearanceAssetRegistry;

AppearanceAssetRegistry registry;
// Model owners load/keep the CPU+GPU resources alive across publication.
// Publish "hero" and "heroLod" using a registry Request each.
auto base = registry.find("hero");
auto visual = registry.find("heroLod");
aiverse::view::ModelLod lod;
std::string error;
if(base && visual) {
    auto authored=aiverse::view::makeNamedModelPoseRetarget(
        *base->model,*visual->model);
    if(base->compileVisualLod(*visual,40.f,authored,lod,&error)) {
    aiverse::view::Object packet;
    if(base->packet(actorTransform,packet)) {
        packet.lods.push_back(lod);
        view.update(std::span<const aiverse::view::Object>(&packet,1),dt);
        view.submitOne(renderer,packet);
    }
    }
}
```

**Important:** Retrieve/check `base` and `visual` as nonnull before dereferencing to construct the authored map. An actual host must retain the snapshots and their borrowed Model owners through worker join and synchronous draw. The registry does NOT own the Models, GPU Device, resource retirement or model streaming.

- On material-only changes, registry appearance `version` changes while `modelPublicationGeneration` remains stable. A compiled LOD map still requires exact source/visual Model pointer and epoch on every draw. Graph-only edits may change **graph** generation but MUST NOT change the Model epoch.
- On model pointer replacement, changed `Request::modelRevision`, or `invalidateSources({modelPath})`, issue a new Model epoch and recompile all dependent source/visual LOD plans. If a host mutates a Model in place WITHOUT calling invalidation or updating `modelRevision`, no registry can detect that; this remains a host contract violation.
- `AppearanceAssetRegistry::publishPrepared` sees a new graph generation after invalidated graph/model input even if an old compiled `shared_ptr` is passed; material-only updates reuse graph generation when allowed.
- FPS character presentation remains a **single precomputed owner**. A graph reload transfers only compatible authored playback clocks; it does not transfer contact/notify/montage state or gameplay authority.
- Compiled LOD maps are still explicitly opt-in. The renderer's own mesh LOD and RTS legacy authoring/fallback remain independent.

## Actual checks and unverified behavior

ACTUALLY performed:
- Connected GitHub PR/head, latest `main` comparison and Stage 125 handoff reads.
- Inspected `AppearanceAssetRegistry::publish`, `publishPrepared`, `invalidateSources`, `Asset::packet`; FPS character graph reload and prepared packet path; `AnimationPlayerComponent::pendingPlayback` consumption in `EcsAnimationSystems.h`.
- Wrote five targeted source commits using latest blob SHA and performed current-branch source readback.
- No other agent branches were changed.

**NOT RUN:** local Git clone, CMake configure, C++ compile/link, CTest, CI, metal/vulkan/gl GPU execution, actual FPS/RTS/editor animation parity screenshots, source/hot-reload fault injection, sanitizers or benchmarks. Source-only completion; C++ compilation/runtime and absence of warnings remain **unverified**. PR #22 MUST remain Draft.

## Known limits and precise Stage 127 handoff

1. `Asset::compileVisualLod` is a native API, NOT an automatic LOD streamer. Native hosts must pass the published source/visual snapshots and must retain their underlying borrowed Models across the render/worker fences. The API does not add GPU allocations or a second animation graph.
2. Registry publication epochs are scoped to the appearance key and host `modelRevision`; distinct published keys referencing the same Model pointer may have independent epochs, by design. A native asset replacement may preserve its pointer: host MUST change `modelRevision` or call `invalidateSources` on the model source.
3. Graph-only FPS hot reload migration is limited to the existing named state and normalized clip phases; changed names/loop definitions restart. Existing FPS montage state and native callbacks reset on reload.
4. Later stages may add an opt-in native LOD profile authoring UI and stream-source map, with explicit immutable snapshots and asset version diagnostics. Keep genre logic in the host.
5. Finish remaining feature work as requested, then run full native CMake, CTest, compiler warnings, Metal/Vulkan/GL visuals, FPS/RTS/editor hot reload parity, sanitizer/failure injection and benchmarks. Do not claim tests passed until actual evidence exists. PR #22 remains Draft.
6. Next Stage 127 must start with current main and peer branch ancestry, then write a separate detailed handoff and update `AGENTS.md` with changed files, actual tests, limitations, HEAD and next step. Never force push or overwrite parallel teams' changes.

## Stage 126 native AppearanceSpec follow-on commits

Two source commits were made after the initial handoff was written:

- `1aa17f6d12bdbc3c8e306ac23c057d18609d3440` —
  `engine/view/AppearancePacket.h`: appended optional
  `AppearanceSpec::modelPublicationGeneration` after the
  original aggregate fields. Native hosts that do not use
  `AppearanceAssetRegistry` may supply an explicit generation.
  `buildAppearancePacket` forwards it to the resulting
  `view::Object::animationModelGeneration`; zero remains a
  backwards-compatible opt-out for old noncompiled LOD hosts.
- `6487a3f61a1e694145b28a5745061aa00b5de8f3` —
  `engine/view/AppearanceAssetRegistry.h`: registry snapshot
  packets now set that same `AppearanceSpec` property before
  calling the generic builder, avoiding a separate downstream
  assignment. Registry and no-registry native consumers share
  one source-level contract.

These remain GitHub code-only edits. Neither native C++ compilation
nor CTest nor GPU validation was performed.
