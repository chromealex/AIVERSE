# AIVERSE MotionWeave / Universal View — Stage 151 handoff

Date: 2026-11-19
Branch: `feature/universal-ecs-animation-pipeline`
PR: https://github.com/chromealex/AIVERSE/pull/22 — OPEN / DRAFT
Previous: [Stage 150](animation-stage150-complete-lod-signatures-compiled-sockets-2026-11-19.md)

## Goal and strict architecture

**Animation is entirely independent of genre.** This stage continues reusable authored presentation publication, not RTS/FPS/RPG-specific features. The universal AppearanceAssetRegistry already had isolated `Asset::compileVisualLod`; Stage 150 added generic `compileModelSocket` bindings. A native host still needed a single way to prepare ALL authored whole-model LOD retarget plans and attachment socket bindings against the SAME published source Model revision, and only expose a coherent successful configuration to `View::Object`.

Stage 151 adds an optional **all-or-nothing CPU authoring transaction**. It introduces no animation update, worker, gameplay ECS writer, network state, GPU ownership, file format, animation graph schema, or genre-module dependency.

The user requested **finish features first and perform one comprehensive native Mac test pass at feature freeze**. Consequently no compiler, CMake, CTest, GPU/sanitizer or performance tests were run; source readback is not runtime verification. Do not claim the entire animation system is finished or merge PR #22.

## Exact source commits

1. `6498458f833841785c12921de68af513fbbbef51` — `engine/view/AppearanceAssetRegistry.h`: add nested `Asset::VisualLodRequest`, `Asset::SocketBindingRequest`, `Asset::CompiledPresentationBindings`, `Asset::compilePresentationBindings(...)`. The previously published `Asset::compileVisualLod` remains unchanged.
2. `4be3927571b62bc3d8ca90f4bf11d209f78b2354` — same header: `CompiledPresentationBindings::apply(Object&)` validates solely numeric Model pointer identity, publication and authored socket selector without dereferencing a retired Model; when committing a full new authored configuration, it clears earlier compiled socket bindings from ALL attachments before installing this revision's specific ones.

## C++ API and example usage

```cpp
using Asset = aiverse::view::AppearanceAssetRegistry::Asset;
using Bindings = Asset::CompiledPresentationBindings;

auto source = appearanceRegistry.find("character");  // shared snapshots
auto distant = appearanceRegistry.find("character_lod1");
if(!source || !distant) return;

auto mapping = std::make_shared<aiverse::view::ModelPoseRetarget>(
    aiverse::view::makeNamedModelPoseRetarget(*source->model,*distant->model));

std::vector<Asset::VisualLodRequest> levels = {
    {distant.get(), 25.f, mapping}
};
std::vector<Asset::SocketBindingRequest> sockets = {
    {0, "Hand.R", -1}
};

Bindings candidate;
std::string error;
if(source->compilePresentationBindings(levels,sockets,candidate,&error)) {
    aiverse::view::Object draw;
    if(source->packet(worldTransform,draw)) {
        draw.attachments = authoredAttachments; // index 0 has matching Hand.R
        (void)candidate.apply(draw);  // false leaves draw untouched
        // Host keeps CPU source + visual Models / snapshot materials readable,
        // then calls its normal View::update / View::submit paths.
    }
}
```

- `VisualLodRequest` identifies a published immutable visual `Asset`, strictly increasing positive finite distance, and a shared read-only authored `ModelPoseRetarget`. Each level is validated with existing `Asset::compileVisualLod` and produces a compiled immutable `CompiledModelPoseRetarget` bound to the source and visual publication identities.
- `SocketBindingRequest` identifies a unique attachment index and authored socket name/index. It is compiled with the Stage 150 generic `compileModelSocket` on the same source Model and nonzero publication generation. An unknown bone, invalid socket, repeated index, null mapping, missing model epoch or incompatible LOD fails the whole transaction.
- `CompiledPresentationBindings` retains source Model pointer **for equality only**, source publication generation, complete compiled LOD list, and immutable socket bindings. It does NOT own Model/Graph GPU memory or animation clocks. The caller keeps published Appearance snapshots and Model lifetimes valid.
- `Asset::compilePresentationBindings` builds a local candidate and assigns `out` only after all levels and sockets succeed. An error or exception returns false, with diagnostics in optional `error`, leaving the previously published `out` intact. It does not register/replace global appearance sources or mutate a View.
- `CompiledPresentationBindings::apply(Object&)` checks `animationSourceModel(Object)` and its `animationModelGeneration` exactly match the compiled source identity before any changes. Every socket index must fit `Object::attachments` and its original authored name/index must match. The implementation checks compiled metadata fields without dereferencing its saved Model pointer. It constructs a local copy, replaces its complete `lods` list, clears all prior compiled socket caches, installs this transaction's bindings, and finally moves the candidate into `Object`. A rejected object leaves the previous configuration untouched; normal allocation exceptions likewise leave it untouched until commit.
- This API works with the existing `Asset::packet(world, Object&)` helper and regular `View::update` / `View::submitOne`, without extra evaluation. `VisualLodRequest` / `SocketBindingRequest` are host-only authoring descriptions, not genre semantics.

## Backward compatibility and caveats

- No existing `AnimationSourceFrame`, `View::Object`, `ModelAttachment`, `ModelLod`, `AppearanceAssetRegistry::Request`, `AppearanceAssetRegistry::Asset` aggregate field order was changed. The new types are nested helpers and all previously supported methods retain their signatures.
- The compiled configuration is explicitly opt-in. Hosts that do not use it can still pass direct ModelLod and legacy uncompiled ModelAttachment records. Any source publication change requires recompilation, and a visual Model revision also requires regenerating the affected LOD plan.
- The helper runs on the host's asset/presentation owner before old Models are retired, with worker barriers managed by that host. It does not allocate GPU resources, prove device fences, own read leases or prevent external hosts violating immutable Model + monotonic epoch contracts.
- `apply` resets all prior compiled socket bindings on attachments when publishing a *complete* new authored configuration; an attachment not present in `SocketBindingRequest` uses the existing legacy runtime name/index resolution. A host requiring fail-closed behavior for all attachments must include every required attachment in the bindings transaction.
- Batch compilation retains O(number of LODs + sockets) shared CPU metadata. ModelPoseRetarget validation may traverse hierarchies and allocates compiled plans at publication, not in per-frame actor rendering. Crowd timings remain unmeasured.

## Actually performed and NOT RUN

**ACTUAL:** used connected GitHub to read the Stage 150 handoff and source of `AppearanceAssetRegistry.h`, `AppearancePacket.h`, `AppearanceAuthoringService.h`, `ModelComposition.h`, `ModelPoseRetarget.h`. Confirmed `AppearanceAssetRegistry::Asset::packet` provides a Model publication epoch and `Asset::compileVisualLod` already produces the compiled retarget plan. Wrote two SHA-guarded source commits and read back the new API. No local Mac worktree was available.

**NOT RUN:** native CMake configure, C++ compilation/link, CTest, current-head passing CI, Metal/GLES/Vulkan renderer parity, editor/FPS/RTS interactive scenes, asset reimport stress, fault injection, sanitizers or benchmarks. These are not implied by connected source reads.

## Remaining work / Stage 152

1. Audit actual native project and editor import/publish callers for this new optional batch API. Adopt it where source and visual snapshot lifetimes are guaranteed; do not graft game action logic into MotionWeave.
2. Expand universal authored animation graph authoring, retargeting of arbitrary imported skeletons, complete attach/detach/socket workflows and runtime LOD parity. No claim of feature completion before real graph/pose/render tests.
3. Queue final regression tests (run once after feature freeze): valid compiled batch; invalid second LOD keeps prior bundle; duplicate socket index keeps prior bundle; invalid socket name/index; wrong Model generation; packet apply with mismatched attachment selector; clearing old socket caches; source/visual hot reload; no double writer, Model read lease and GPU fence lifecycle; high-crowd memory/performance.
4. Coordinate with concurrent `main`/renderer/texture/model agents in a disposable integration worktree after feature closure. Do not automatically rebase, merge, force-push or modify their branches; Draft PR #22 stays Draft.
5. On each subsequent stage, commit separate implementation and handoff. After ALL functions are complete, run one full native Mac validation via `bash tools/animation/validate-final.sh`, fix failures and only then assess readiness.


## Readiness matrix at Stage 151 (source implementation vs verified product)

| Subsystem | Code status | Outstanding work |
| --- | --- | --- |
| Genre-neutral MotionWeave ECS/Graph | Implemented incrementally | Native compile, runtime and concurrency validation |
| Graph hot reload / portable named playback | Implemented incrementally | Reimport fault injection, restore parity tests |
| Stable source ID / published Model epoch | Implemented incrementally | Cross-host integration validation |
| Whole-model animation LOD | Implemented, including complete revision checks | End-to-end visual/retarget parity and performance |
| Generic retarget / compiled plans | Core API exists | Importer presets and complex rigs/parity |
| Compiled attachment sockets | Core API exists | Full authored attach/detach workflows and parity |
| Atomic authored LOD + socket transaction | Stage 151 source complete | Native compile/regressions and caller adoption |
| FPS/RTS/editor integration | Partial | Rich animation parity and removal of legacy paths |
| Metal/Vulkan/GLES presentation | Not verified by this stage | Real GPU, screenshots and fence tests |
| Complete native validation / integration | NOT RUN | One coordinated final Mac pass and repairs |

“Implemented” means committed source, not confirmed runtime correctness. The user requested a **user-visible status table after every future stage**; the next agent must include one in the reply in addition to maintaining this separate handoff.
