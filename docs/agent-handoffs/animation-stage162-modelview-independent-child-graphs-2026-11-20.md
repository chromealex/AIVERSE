# MotionWeave Stage 162 — native ModelView independent child AnimationGraph integration

Date: 2026-11-20
Branch: `feature/universal-ecs-animation-pipeline`
Baseline: Stage 161 `a2f7b405a65c850a05084df918b3f547840b5904`
Draft PR: https://github.com/chromealex/AIVERSE/pull/22 — do not merge without real native validation.

## Implemented (source-only)

- `apps/modelview/main.cpp` uses the existing genre-neutral `engine/view/SceneAttachmentHost.h` and `engine/view/AnimatedSceneAttachmentSources.h` APIs, mirroring the Studio owner split without importing RTS/FPS/gameplay code.
- Added a staged `NativeAttachmentGeneration` with its own presentation `World`, `SceneAttachmentHost`, exactly one `AnimatedSceneAttachmentSources` bridge, and a separately published immutable child Graph appearance per actor and stable authored child ID.
- Every actor is a distinct presentation scene object (`scene=1, object=actor-N`). Parent Model's native `Animator` remains its sole parent pose writer. Every animated child has its own MotionWeave graph source; static child sockets exclude those animated children.
- Before acquiring parent ModelReadLeases, the staging provider imports all enabled static/animated child Models. For each actor/child, publish an independent Graph via `AppearanceAssetRegistry::Request` (`animationGraph`, `graphReader=readFile`, nonzero `sourceId`), and validate graph Model pointer identity. A bad Graph/socket/rig/import rejects the new generation without replacing the old visible generation.
- `rebuildAttachments` uses the existing staged `reimportModelFiles` transaction. The old provider retains Animators, appearance registry AND presentation/Graph generation until the device-safe retirement barrier. Graph owners and ECS attachments are cleared before old GPU resource shutdown. The native file watcher also observes enabled child Graph asset files.
- During each visible frame, parent Animators evaluate once; independent child Graph bridge `sync` runs once on the render/Device-owner thread (dt=0 if paused/seeking). The parent uses a precomputed pose, while the child uses `AnimatedSceneAttachmentSources::prepare` and submits a Graphless packet via a separate `View`, avoiding duplicate evaluation.
- Move-only parent and animated-child `Prepared` read leases remain alive until AFTER synchronous `Renderer3D::render` (previously parent's attachment read lease was released immediately after `submitOne`).
- ModelView Attachments UI can now author the independent animation toggle and Graph path, preserving shared JSON codec and stable child ID. Invalid edits are staged/rejected rather than replacing published assets.
- On failed shutdown GPU completion, live attachment generation and registries are quarantined along with GPU owners instead of destructing borrowed Model/Graph references.

## Changed commits

- `8bcf590caac3eed38104a9a1c768b0b88259397e`: lifetime of parent prepared packets through render.
- `1c68e79161481543478870560b281cf299249bef`: staged per-actor native Graph ownership/ECS host, reimport generation.
- `98e61b23d8e2fde7377c71bc47ccb33e6be2d3f8`: per-frame Graph sync, rendered independent child poses and pinning.
- `1cbfa6908ccdccd208163926c608202839ba8574`: import-order safety and UI Graph authoring.
- `7d59d2dc3f049debb80f0e2c9453972aca9cdd7a`: staging errors, CPU-lease contract comment.
- Stage 162 part 1 predecessor `f980e245d9efcc09ec37345fb89581f58f23dcf3`: watch Graph paths.
- Only runtime source file changed in this phase: `apps/modelview/main.cpp`.

## Actual verification

Actual: GitHub-connected source reads of `apps/modelview/main.cpp`, `apps/editor/windows/AttachmentAuthoringWindow.cpp`, `engine/view/{AnimatedSceneAttachmentSources,SceneAttachmentHost,EcsAttachmentScene,ImportedAttachmentAuthoring}.h`; SHA-guarded source commits and readback inspection. **NOT RUN:** native C++ compiler/link, CMake, CTest, running ModelView, Graph playback pixels, native hot-reload continuity, CI, Metal/Vulkan/GLES, ASan/TSan, fault injection or crowd benchmark. Do NOT label Stage 162 tested, green, complete or release-ready.

## Known gaps / correctness risks

- No native compile has proved header APIs or template types. Build the `aiverse_modelview` target FIRST at the final coordinated Mac validation; fix compiler failures before any success claim.
- Reload currently creates fresh graph players; Graph timing/parameter continuity across old/new child Graph generations has NOT been implemented. Reimport retains parent clip phase only.
- Original parent model Animator playback is not replaced by MotionWeave. The one-writer rule remains one Animator for parent + independent child Graph players (no double-writer).
- File watcher tracks observed graph/model paths but cannot watch non-filesystem virtual assets.
- Reimport restages the entire Model/provider generation, not an incremental Graph-only replacement, and currently uses an advisory 8-old-provider retention limit with blocking device idle on cleanup; real token-complete per-resource shutdown remains unimplemented.
- Source and render identities are per host. Child source world is separate from parent view; source ID collision between separate views is avoided by the dedicated render-only View, but renderer draw-ID semantics need native verification.
- The shared graph compiler and default parameter pose need real imported assets; test one animated child, repeated parent instances, multiple sockets, missing graph, hot-reimport and pause.
- Standalone ModelView remains ImGui; no cross-branch UI or SceneLoadCoordinator merge was attempted.

## Status/readiness

| Domain | Status | Next |
|---|---|---|
| Universal ECS MotionWeave + Graph | Incremental code integrated | native regression |
| Studio animated child Graph import/render | Stage 161 code integrated | native screenshots |
| ModelView animated child Graph + ECS per-instance source | Stage 162 source implemented | compile, skinning/pixel check |
| ModelView authored child Graph file edit and file watcher | Source implemented | real disk edit/reload |
| ModelView native atomic replacement of parent + child generation | Source implemented | error injection and lifetime verification |
| Child graph phase continuity on hot reimport | NOT IMPLEMENTED | compatible Graph phase/parameters |
| Full LOD/morph retarget, crowd budgets | PARTIAL | complex rigs, perf |
| SceneLoadCoordinator native prefab hook | NOT IMPLEMENTED | peer branch review |
| Full GPU provider token retirement | NOT IMPLEMENTED | texture/rollback/fence owners |
| Full CMake/CTest/backend/sanitizer/perf | NOT RUN | user-requested one-pass Mac acceptance |

## Next Stage 163

Implement child Graph phase/parameter continuity across compatible staged reimports; avoid reusing stale Graph player or Entity handle. Then continue genre-independent complex rig retarget/morph/LOD, scene lifecycle reviewed adapter and complete GPU token retirement. Maintain one handoff per implementation stage and explicit test-status honesty.
