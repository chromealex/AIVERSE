# AIVERSE MotionWeave — Stage 162 (part 1): ModelView animated Graph file watch

Date: 2026-11-20
Branch: `feature/universal-ecs-animation-pipeline`
Parent: `a2f7b405a65c850a05084df918b3f547840b5904` (Stage 161)
Source commit: `f980e245d9efcc09ec37345fb89581f58f23dcf3`
Draft PR: https://github.com/chromealex/AIVERSE/pull/22 (do not merge before feature closure)

## Change made

In `apps/modelview/main.cpp::Viewer::pollModelSourceChanges`, add the external `animationGraph` path to the watched files for each enabled, independently animated attachment. Existing source model files are still watched. The watch remains opt-in (`--watch-model-files`), checks file timestamps at its pre-existing interval, and schedules `reimportRequested` at the Device-owner frame boundary rather than publishing on a watcher thread. Removed/disabled graph paths leave the watcher map through existing cleanup.

This change is a prerequisite for native child Graph hot-reimport parity, **not** its completion. At present, `buildAttachmentLayout` still intentionally rejects enabled `animated=true` children with `independent animated child preview requires Studio`. Therefore native ModelView child Graph import, evaluation, visual rendering and hot reload are **NOT IMPLEMENTED**. Do not claim the graph watcher delivers playable children yet.

## Architecture to complete Stage 162

1. Extend the staged native resource generation (`RetiredNativeScene`) with published independently animated child `AppearanceAssetRegistry::Snapshot` instances, a dedicated genre-neutral presentation `World`, `SceneAttachmentHost` and `AnimatedSceneAttachmentSources`. Respect destruction order: clear Graph bridge/scene ECS before releasing their Model/Graph/CPU owners; old GPU generations remain retained until proven idle.
2. In `buildAttachmentLayout`, validate shared `AttachmentLayoutCodec` JSON first. Import static children through `ImportedAttachmentAuthoring`; import each enabled animated child Model and its independent Graph using `AppearanceAssetRegistry::Request` (nonzero `sourceId`, `animationGraph=Json::string(path)`, `graphReader=readFile`). DO NOT feed animated children into the parent static `Imported::compile`; it must receive static entries only.
3. Create a **unique persistent presentation parent Entity and scene/object key per ModelView actor**; do not reuse an asset registry key as an instance ID. Activate child ECS entities through `SceneAttachmentHost`; stage all new actors/Graph publications/scene children before committing the generation to the live viewer.
4. At exactly ONE Device-owner pre-render boundary per frame, gather the complete set of independent child inputs and call `AnimatedSceneAttachmentSources::sync` ONCE with dt=0 when paused. The original parent `Animator` remains the only source of the parent's pose; no second Graph writer should evaluate it.
5. After computing each parent's `ModelDraw` and parent node matrices, use `AnimatedSceneAttachmentSources::prepare`, submit its graphless packet to a SEPARATE render-only `::aiverse::View`; retain every move-only `Prepared` (including parent+child `ModelReadLease`) through synchronous `Renderer3D::render`.
6. Treat any malformed child Graph, missing named parent socket, bad epoch or GPU import as rejected staged generation; retain last-good parent/child render assets. For layout edits also stage the scene/Graph host without invalidating the live one. Never destroy GPU-owned ModelAssetProvider without valid device completion proof.
7. Add repeatable source/Graph edit and multi-instance cases to final test plan. Preserve per-instance source identity, hot-import graph restart, duplicate draw prevention and renderer backend parity.

## Tests and risk

Actually performed: remote GitHub source audit of Stage 161 handoff, ModelView and Studio ownership paths; SHA-guarded one-file GitHub commit for opt-in graph source tracking.

**NOT RUN:** native C++ compile/link, CMake, CTest, real ModelView/Studio, Graph hot reload, screenshots, Metal/Vulkan/GLES, sanitizers, GPU lifetime fault tests and performance. This stage is a SOURCE-ONLY PARTIAL IMPLEMENTATION; user requested one coordinated full Mac test run once all functions close.

## Readiness

| Area | Status |
|---|---|
| Independent child AnimationGraph, Studio | Stage 161 source integrated, native tests pending |
| ModelView Graph path timestamp observation | Source implemented in this commit, untested |
| ModelView animated child Graph import/evaluation/render | NOT IMPLEMENTED |
| Stage 162 ModelView atomic staged Graph reimport | NOT IMPLEMENTED |
| Cross-host child LOD/morph and large crowds | PARTIAL / NOT VERIFIED |
| Complete token-based GPU provider shutdown | NOT IMPLEMENTED |
| Full native CMake/CTest/backend/sanitizer/perf pass | NOT RUN |

Next action: finish the **ModelView real animated child owner/render integration** above before declaring Stage 162 complete. Maintain a new handoff after each implementation stage; do not merge or force-push peer branches.
