# AIVERSE MotionWeave / Universal View — Stage 153 handoff

Date: 2026-11-19
Branch: `feature/universal-ecs-animation-pipeline`
PR: https://github.com/chromealex/AIVERSE/pull/22 — OPEN / DRAFT at source readback
Previous: [Stage 152](animation-stage152-native-appearance-packet-integration-2026-11-19.md)

## Goal and architecture

The user's binding requirement: **MotionWeave, reusable animation components and aiverse::View are completely genre-independent**. A game/editor/native host supplies authored file paths, stable source IDs, immutable published models, named sockets, graph parameters and presentation cues. No RTS/FPS/RPG/gameplay/net semantics may enter the animation core. Each animated source has exactly **one** presentation graph/pose owner.

Stage 152 exposed a transactional generic `Asset::packetWithBindings`, but actual imported child Models were still passed as bare borrowed `ModelAttachment::draw.model` and compiler snapshots did not keep old CPU Models alive. Stage 153 introduces an opt-in **real child Model provider/cache/appearance authoring path** which uses the existing native `ModelAssetProvider` and `ModelResourceCache::acquireModelRead` owner-thread read leases.

This adds no fictitious child Models and does not fabricate asset paths or create a second animation evaluator. The stage is a reusable library capability, **not** a completed editor UI or FPS-specific child authoring integration. Native C++ compile, CMake/CTest, Metal/GLES/Vulkan, sanitizer and performance tests are STILL **NOT RUN** according to the user's feature-first, one full test pass after closure instruction.

## Exact implementation commits

1. `b4a63dfcd4c6ee8e621461c594daa83f139c1093` — new `engine/view/ImportedAttachmentAuthoring.h`:
   - Generic `ChildRequest`, immutable source/child `Compiled` layout, and move-only `Prepared` output.
   - `compile(sourceSnapshot,cache,children,out,error)` validates that the source and **real imported child Models** reside in the same live `ModelResourceCache`, using short-lived read leases while resolving immutable named/indexed `CompiledModelSocket` bindings. Child published `Asset` snapshots are held strongly for MATERIAL metadata only; raw Model/GPU lifetimes remain host-owned.
   - `prepare(compiled,cache,world,optionalBindings,out,...)` obtains fresh short-lived owner-thread `ModelReadLease`s for parent, every actual imported child, and every visual LOD Model in optional Stage 151 bindings before constructing a renderable `View::Object`. Child `Asset::packet` supplies its real imported model, material/geometry/tint/variant fields and child-relative world matrix; sockets use Stage 150 compiled bindings. Optional LOD/socket bundles and source publication epoch are checked by `Asset::packetWithBindings`.
   - `Prepared` retains the exact live CPU Model leases and source/child appearance snapshots until after the caller's synchronous `View::submitOne` / `submitPackets`. Failed missing child, stale cache residency or revision mismatch leaves previous output untouched.
2. `fd475ccd4823394e7fd202d032cac8a46f0cba45` — same new header:
   - `importChild(provider,device,appearanceRegistry,key,modelSource,request,out,textureLoader,error)` loads a REAL file via `ModelAssetProvider::load` and its existing GPU/texture cache, obtains a live ModelReadLease, publishes a static child AppearanceAssetRegistry snapshot with source dependency, then swaps the output snapshot only on success. Rejects attempts to sneak a second animated Graph into a child attachment. The actual Model stays owned by the provider, not by the snapshot or imported bundle.
3. `d62b9482bcc6b360625f5a56fcc88f43464fb437` — same header:
   - Adds explicit `AnimationOwner::{ViewEvaluates,HostPrecomputed}` selection to `prepare`. `HostPrecomputed` clears `next.object.graph` before packet submission, so an already evaluated host presentation Pose cannot be scheduled again in generic View. `ViewEvaluates` rejects Graph sources without a stable nonzero ID.

## Public API and intended native workflow

```cpp
#include "engine/view/ImportedAttachmentAuthoring.h"
using Imported = aiverse::view::ImportedAttachmentAuthoring;
using Registry = aiverse::view::AppearanceAssetRegistry;

// The parent must ALREADY be loaded in the same ModelAssetProvider cache
// and published into the AppearanceAssetRegistry by the host.
auto source = registry.find("avatar");
Registry::Snapshot child;
Registry::Request request; // optional authored child material overrides
std::string error;
if(!Imported::importChild(provider,device,registry,
       "avatar/lantern","models/lantern.glb",
       request,child,{},&error)) {
    // Preserve previous published assets; log error, skip new configuration.
}

std::vector<Imported::ChildRequest> children;
Imported::ChildRequest lantern;
lantern.appearance=child;
lantern.socketName="Hand.R";
children.push_back(lantern);
Imported::Compiled compiled;
if(Imported::compile(source,provider.resources(),children,compiled,&error)) {
    Imported::Prepared current;
    if(Imported::prepare(compiled,provider.resources(),
           actorWorld,nullptr,current,
           Imported::AnimationOwner::HostPrecomputed,&error)) {
        // Add the pose already computed by the host MotionWeave owner:
        current.object.draw.pose=&existingPose;
        current.object.draw.nodeWorld=&existingNodeWorld;
        // current.object.graph is null; NO second evaluator.
        view.submitOne(renderer,current.object);
        // Destruction of current releases all owner-thread CPU read leases.
    }
}
```

For View-owned animation, choose `AnimationOwner::ViewEvaluates`, assign a durable source ID + generation, run `View::update` **exactly once** before submitting the prepared Object, then release the `Prepared` leases after draw. For a Graph-bearing animated CHILD, create a *separate* View Object with its own source ID and independent intended single-writer evaluation; this static attachment helper deliberately rejects `child->graph!=nullptr`.

## Resource lifetime and transactional rules

- **Ownership:** only `ModelAssetProvider` / `ModelResourceCache` owns imported CPU Models and GPU resources. `AppearanceAssetRegistry::Snapshot` strongly holds material and Graph metadata but **does not** extend Model or texture GPU lifetime. `Compiled` holds parent and child snapshots plus immutable socket plans but does not hold persistent read leases; it must be recompiled after Model publication/reimport.
- **Compilation:** source and every child must be cache-resident in the SAME resource owner; `cache.acquireModelRead(Model*)` refuses missing/nonresident/foreign-thread access. A missing named socket, zero publication epoch or animated child source rejects compilation without mutating the previous Compiled output.
- **Per-frame preparation:** `Prepared` is move-only and retains one owner-thread lease per pinned Model (parent, optional visual LODs, children), plus source/child snapshots to keep the Material override pointers in `ModelDraw` valid. Packet construction occurs only AFTER acquiring leases, and `out` is replaced only when the entire packet builds successfully. Real ModelReadLease destructors must run on the owner thread. GPU completion is **NOT** implied by the lease; renderer/device Model retirement fences remain separate.
- **Hot reload:** host must detach prior View/animation sources and retire GPU submissions with existing barrier/token procedures before invalidating provider models or re-publishing the source; a failed new child import or bad socket preserves caller's prior snapshot/configuration. Compiled child packs must be replaced on new Model publication. Pointer equality alone is not enough if a host violates its monotonically changing Model publication epoch contract.
- **Performance:** `acquireModelRead(Model*)` currently validates membership in ModelResourceCache; the repeated per-model leases and packet allocations may be expensive for large crowds. This is correctness-first source code and requires actual benchmark; optimize batch authoring/scope only after measurement. Do not maintain long-lived ModelReadLease in the compiled asset.

## Actual verification and NOT RUN

**ACTUAL:** connected GitHub source/API inspection of Stage 152 handoff, ModelResourceCache, ModelReadLease, ModelAssetProvider, ModelComposition, AppearanceAssetRegistry, reference FPS packet and editor preview owners. Three source commits were written and latest new header was read back to confirm provider import, cache read lease acquisition for parent/child/LOD, compiled sockets, transactional output, and explicit single animation owner mode. PR #22 was OPEN, Draft, unmerged at Stage 153 code HEAD `d62b9482bcc6b360625f5a56fcc88f43464fb437`. No changes to other agents' branches or main.

**NOT RUN:** native CMake/C++ compiler/link, unit CTest, current-head passing CI, Metal/GLES/Vulkan scene draw, editor/FPS/RTS visual parity, GPU fence tests, Thread/Address Sanitizer, cache read-lease concurrency fault injection, memory/crowd benchmarks, final `bash tools/animation/validate-final.sh`. Source inspection alone is NOT proof that the C++ compiles or runs.

## Status table — Stage 153

| System | Source status | Still missing / unverified |
|---|---|---|
| Genre-neutral MotionWeave ECS + Graph | Implemented incrementally | Final native/worker/regression tests |
| Graph hot reload and portable named playback | Implemented incrementally | Clock/reimport fault injection |
| Stable source ID and Model epochs | Implemented incrementally | Runtime asset race/reuse validation |
| Animation LOD/hysteresis + retarget plans | Core implemented | Complex rig parity, high-crowd performance |
| Compiled sockets | Core implemented | Full editor attach/detach authoring and UX |
| Atomic LOD + socket appearance bindings | Source implemented | Actual imported LOD source consumers |
| Native imported static child Model path | Stage 153 source implemented | Host/editor adoption and real model rendering |
| Cache read-lease scoped child presentation | Stage 153 source implemented | Compiler, lease failure injection, perf |
| FPS native appearance packet | Source connected | Actual child-model configurable assets |
| Animation Graph/Modeling editor | Partial | Unified asset/import authoring and previews |
| Metal / Vulkan / GLES visual runtime | NOT VERIFIED | Real GPU and fence validation |
| CMake / CTest / sanitizers / benchmarks | NOT RUN | One comprehensive final Mac pass |

All “implemented” statuses above mean GitHub source exists, NOT native-tested or production ready.

## Next Stage 154

1. Integrate the new generic `ImportedAttachmentAuthoring` into a REAL native/editor asset workflow that owns model files/cache/scene UI, rather than declaring the library helper full integration. Keep actual graph owners independent and do not inject genre semantics into core.
2. Continue universal attach/detach/source publication UX, retarget mapping for complex imported rigs/morphs, authored LOD child ownership and GPU/CPU retirement coordination.
3. After actual functional closure: full compiler/CTest, backend Metal/Vulkan/GLES, real asset/socket/reimport parity, read-lease lifetime, worker sanitizers and high-crowd benchmarks; fix failures before changing PR from Draft. Keep tests deferred per current user instruction.
4. Keep each stage's exact code SHAs, source paths, test status and standalone handoff; show the user a readable progress table after every stage.
