# AIVERSE MotionWeave / Universal View — Stage 152 handoff

Date: 2026-11-19
Branch: `feature/universal-ecs-animation-pipeline`
PR: https://github.com/chromealex/AIVERSE/pull/22 — OPEN / DRAFT at beginning of stage
Previous: [Stage 151](animation-stage151-transactional-presentation-authoring-2026-11-19.md)

## User requirements and scope

**MotionWeave must be independent of genre.** Generic source identity, authored AnimationGraph parameters, immutable Model publication epochs, Model LOD retarget plans, socket bindings and single-owner presentation evaluation live in common libraries. Game and editor hosts remain adapters only; never import FPS, RTS, network state, weapon/game actions or gameplay entity components into reusable animation/view code. Continue all functions first, then the user wants ONE comprehensive native Mac test pass. After EACH implementation stage show the user a **readiness table** for the complete animation subsystem and keep an equivalent checkpoint in this handoff.

Stage 151 compiled an atomic authored LOD+socket binding bundle but had no native packet builder for attaching that bundle to a published Asset, and the reference FPS renderer assembled its appearance packet manually, despite already publishing an immutable `AppearanceAssetRegistry::Asset`. Editor's Modeling/Graph preview windows have their own `AnimationGraphInstance` and no AppearanceAssetRegistry pipeline: do not create a second ECS player or claim editor adoption.

## Exact code commits

1. `a55c4398b53f724958d4864656bda87f271b94d5` — `engine/view/AppearanceAssetRegistry.h`: appended optional `VisualLodRequest::matchByName=false`. When an explicit authored map is absent, the importer can explicitly opt into `makeNamedModelPoseRetarget(source,visual)` and compile it at publication. No index guessing, no implicit morph mapping. Missing/duplicate names, invalid skeleton hierarchies, mismatched/zero epochs or unbindable complex rigs still fail closed via the existing `compileVisualLod`/retarget validator. Other requests remain strict.
2. `b1ffef2006f2899ac8e30b0a9e60adb43ee5a0f0` — `engine/view/AppearanceAssetRegistry.h`: added `Asset::packetWithBindings(world, attachments, compiledBindings, Object&, error)`. Transactionally builds an appearance packet using the published Asset source, copies caller-authored child attachments, optionally applies the complete Stage 151 `CompiledPresentationBindings`, and only on success replaces the output packet. Invalid Model, binding mismatch or allocation failure preserve the caller's last-good output. This method **does not call View::update or run the graph**.
3. `a34001b7b7b3c381fa4884ab4102dd6e5ed9c232` — `apps/fps/client.cpp`: native reference consumer uses the generic `characterAppearance->packetWithBindings(world, {}, nullptr, packet)` to derive published Model/material/node overrides/variant/geometry/flags/tint/epoch in the existing remote-character render path, with a Model identity check and legacy packet fallback. It adds host-owned world team/visual pose/node matrices/baked sample/persistent draw ID, and overrides only the actor's logical source ID/life. It explicitly calls `packet.graph.reset()` BEFORE placing a pre-evaluated packet in `ScenePacketBatch` so generic `View` cannot create a second animation player; the native host's independent `characterPresentation` World and MotionWeave scheduler remain the sole pose/clock owners.

## Public API

```cpp
using Asset = aiverse::view::AppearanceAssetRegistry::Asset;
Asset::VisualLodRequest authored{
    visualAsset.get(), 25.f, {}, true // opt-in match by node NAME
};

Asset::CompiledPresentationBindings bindings;
std::string err;
std::vector<Asset::VisualLodRequest> lods{authored};
std::vector<Asset::SocketBindingRequest> sockets{{0, "Hand.R", -1}};
if(!sourceAsset->compilePresentationBindings(lods,sockets,bindings,&err))
    /* retain old published bindings */;

aiverse::view::Object candidate;
std::vector<aiverse::view::ModelAttachment> attachments = /* host authored */;
if(sourceAsset->packetWithBindings(
       worldMatrix, attachments, &bindings, candidate, &err)) {
    candidate.id=stableInstanceId;
    candidate.generation=logicalGeneration;
    // For View-owned animation: keep candidate.graph and use View::update once.
    // For an EXTERNAL, already evaluated pose: assign pose/nodeWorld, then
    // candidate.graph.reset(), call View::beginPreparedFrame + submitPackets.
}
```

The new `Asset::packetWithBindings` is optional; unbound `Asset::packet`, manually constructed `View::Object` and legacy uncompiled attachments remain source-compatible. `matchByName` was appended at the END of the prior request aggregate, preserving existing three-argument initialization. Complex rigs need explicit `shared_ptr<const ModelPoseRetarget>`, not guessed indices. This is native C++ API, not new authored JSON schema, serialization, network state or GUI.

## Safety, ownership and limitations

- The published Asset's material pointers, Model, compiled LODs and socket bindings are borrowed from the host and must remain CPU/GPU-safe until synchronous renderer submission. No internal GPU fence or ModelReadScope ownership is implied.
- `Asset::packetWithBindings` uses local packet construction and `CompiledPresentationBindings::apply` (also transactional). It leaves `out` untouched on false or caught exception; the existing compiled bundle is not republished or mutated.
- The reference FPS client currently has **one** published character Model and no authored child assets/whole-model visual LOD in this renderer path. It exercises the common packet **without** a bundle (`nullptr`) rather than inventing extra models or duplicating the character animation player. Future assets may pass actual compiled LOD/socket bundles through the same public API when imported and retained correctly.
- The editor's ModelingWindow/AnimationEditor preview still directly owns its own `render::AnimationGraphInstance` and GPU preview Model. It is a legitimate source-neutral preview owner; it is NOT yet integrated with `AppearanceAssetRegistry::Asset::packetWithBindings`. Porting editor full asset authoring/view packet submission requires a separate warranted stage with CPU/GPU ownership review, not a silent change to animation clock ownership.
- The FPS host remains responsible for its network/actor semantics and chooses source IDs/appearance. No engine animation code knows it is FPS. The precomputed packet has `graph==nullptr` and still carries source rig/epoch solely for render/LOD retarget identity.

## Actual checks vs NOT RUN

**ACTUAL:** connected GitHub inspection of Stage 151 handoff; `AppearanceAssetRegistry.h`, `AppearancePacket.h`, shared `ModelPoseRetarget.h` and `ModelComposition.h`; real `apps/fps/client.cpp` character import/pub and precomputed render packet; `apps/editor/animation/AnimationEditor.h`, `apps/editor/windows/ModelingWindow.cpp`, and `AssetsWindow.cpp`. Three SHA-guarded GitHub source commits were written. No files in main or other agents' branches were modified.

**NOT RUN:** native CMake configure/compile/link, any CTest, current-head CI green, Metal/GLES/Vulkan visual rendering, FPS/editor/RTS parity, source reimport/LOD/attachment failure injection, sanitizers, high-crowd profile, final `bash tools/animation/validate-final.sh`. The above are source implementations, not tested proof.

## Progress table after Stage 152

| Subsystem | Status (source only) | Remaining work |
| --- | --- | --- |
| MotionWeave genre-independent ECS/Graph | Implemented incrementally | Native compile, concurrency/visual regression tests |
| Graph hot reload / named playback carryover | Implemented incrementally | Full fault injection and clock parity |
| Logical source IDs / Model epochs | Implemented incrementally | Runtime across host reimports |
| Whole-model Animation LOD selection | Core implemented | Full visual parity and crowd benchmarks |
| Retarget/compiled plans | Core implemented, name-matching authoring added | Complex rigs and import presets |
| Compiled attachment sockets | Core implemented | Complete attach/detach/prefab authoring |
| Atomic authored LOD + socket bundle | Stage 151 implemented | Real configured child/LOD source integration |
| Published native appearance packet | Stage 152 integrated in native example | Source-dependent LOD/attachment bundles |
| Editor animation authoring/preview | Partial | Editor authoring integration and UX |
| FPS/RTS/native compatibility | Partial | Specialized legacy migration and parity |
| Metal/Vulkan/GLES visuals | NOT VERIFIED | Actual backend runtime and GPU lifetime |
| Comprehensive CMake/CTest/sanitizers | NOT RUN | Single final Mac pass after feature freeze |

These labels mean present in code and GitHub, NOT independently compiled or production-ready.

## Next Stage 153

1. Add source-neutral authored prefab/model attachment assembly using **real imported child Models** with explicit owner-held snapshot/texture leases, and support hot reload of those bindings. Avoid inventing dummy LODs/children solely for demo. Alternatively extend the editor authoring workflow to build named LOD/socket bundles when real source/visual Models and GPU lifetime are available.
2. Continue universal retargeting of difficult rigs, animation graph authoring, roots/morphs, LOD+attachments and editor integration without a second ECS writer or genre modules in core.
3. At final feature freeze test opt-in named mapping, exact/invalid skeletons, atomic output rollback, existing published native appearance parity, graph-not-null precomputed refusal/double writer prevention, streaming source/visual lifetimes, FPS/editor scene pixels, Metal/Vulkan/GLES plus memory/performance and sanitizers. No testing outcomes are claimed now.
4. Preserve branch `feature/universal-ecs-animation-pipeline`, keep PR #22 Draft. Main/base refs have diverged in prior stages; use a reviewed disposable integration worktree only when the full features are ready. Do not auto-rebase, merge or force push concurrent branches.
5. Produce a separate Stage 153 handoff, append chronological journals, and a user-visible readiness table for every stage.
