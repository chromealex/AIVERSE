# MotionWeave — transactional visual scene publication

**Stage 173 source API.** This is genre-neutral cosmetic ECS, not simulation-owned scene state. The implementation lives in `engine/view/MotionWeaveScenePublication.h` and uses existing `SceneAttachmentHost`, `EcsAttachmentScene` and `AnimatedSceneAttachmentSources`.

## Why a separate visual scene World

The canonical `SceneLoadCoordinator` lives in peer branch `feature/scene-runtime-multiworld`, not this animation branch. Its `SpawnPrefab(World&,Entity,const SceneObjectAsset&,string&)` receives the AUTHORITATIVE scene World. It must never create persistent MotionWeave graph clocks, borrowed CPU Model pointers, GPU meshes or cosmetic animation poses inside simulation state.

`MotionWeaveScenePublication` instead builds an independent `World` and `SceneAttachmentHost` per logical scene ID. It receives published immutable parent/child Model/Graph appearance snapshots from the native host's SAME ModelResourceCache. Animated child sources are evaluated by exactly one genre-neutral MotionWeave bridge; static socket attachments never evaluate their own Graph.

## Public API

```cpp
#include "engine/view/MotionWeaveScenePublication.h"

aiverse::view::MotionWeaveScenePublication visuals;
using Publication=aiverse::view::MotionWeaveScenePublication;
Publication::Object object;
object.objectKey="42"; // from SceneObjectAsset::id, NOT Entity::index
object.layout=authoredAttachmentLayout; // pointer-free data
object.parent=publishedParentAppearance; // shared immutable publication
object.resolveStaticChild=resolveStaticFromSameCache;
object.animatedChildren.emplace("headset",publishedIndependentGraphChild);

// Owner barrier, with prepublished CPU+GPU assets held live by host.
Publication::Statistics stats;
std::string error;
const std::vector<Publication::Object> objects{object};
if(visuals.stageScene(sceneId,objects,provider.resources(),stats,&error)) {
    // ONLY if the corresponding SceneLoadCoordinator::commit(ticket)
    // succeeded on the authoritative owner:
    visuals.commitStaged(sceneId);
} else {
    // Source/Graph/rig/socket/attachment error; previous visual scene lives.
}
// On a failed authoritative commit: visuals.discardStaged(sceneId).
// On a successful scene unload: visuals.unload(sceneId);
// After authoritative rollback/loadState: visuals.resetAfterRollback();
```

The example assumes host-provided variables and is a workflow sketch, not a standalone compile unit. `stageScene` retains the new generation in a private pending World and `commitStaged` publishes it after successful authoritative commit. An uncommitted candidate is NEVER rendered. `replaceScene` is a convenience staging+commit call only for self-contained visual hosts (such as authoring previews); do not use it as a substitute for the authoritative coordinator transaction.

For each committed scene, run `visuals.sync(sceneId,cache,dt,stats,error)` once at the presentation owner frame boundary. Then use `prepareParent`, fill its precomputed parent `draw.pose` and `draw.nodeWorld` from the one parent animation owner, and call `prepareChild` for every independently animated child before submitting them to a render-only `View`. Store the move-only parent and child `Prepared` packets through `Renderer3D::render`. Call `View::beginPreparedFrame()` at the appropriate frame boundary. The visual host allocates globally unique, never-reused draw IDs so independent child graph hosts cannot collide in a shared View; these IDs are separate from authoritative Entity handles and Model registry asset IDs.

## Proposed host integration with the peer SceneLoadCoordinator

The following is the contract for the agent merging/reconciling the scene branch. **It has NOT been wired into a running GameModule in Stage 173.**

1. During `SceneLoadCoordinator::request/poll/ready`, parse and validate the `SceneAsset` and publish required appearance assets in a provider that remains CPU/GPU-live independently of temporary scene resource leases. Use its `SceneObjectAsset::id` as `std::to_string(id)`; never use the authoritative Entity's `index` as a stable visual key.
2. When its `prepared(ticket)` manifest is available, assemble a COMPLETE `Publication::Object` array with one Model source, pointer-free attachment layout, static appearance resolver and validated independent Graph snapshots per visual object. Call `visuals.stageScene(ticket.scene.value, objects,cache,stats,error)` BEFORE the authoritative commit. If staging fails, cancel/decline activation, retaining old visual and authoritative scene.
3. Call the coordinator's native `commit(ticket)`. On false, call `discardStaged` and leave previous visuals intact. On true, call `commitStaged(ticket.scene.value)` immediately at the SAME structural owner barrier.
4. For Replace, obtain the complete ACTIVE `SceneState::scenes` ID set after successful authoritative activation and call `unloadAbsent(activeIds)`. Do NOT infer retained scenes from the new request alone: persistent scenes may survive Replace. Duplicate/zero active IDs are rejected before deleting any visuals.
5. On `SceneLoadCoordinator::unload(sceneId)`, stop preparing any more draw packets, remove the visual generation with `unload(sceneId)`, and only then permit old CPU Model/Graph owner retirement. After `Simulation::loadState/rollback`, call `resetAfterRollback()` before `SceneLoadCoordinator::afterRollback()` / requeueing residency and republish new generation IDs.
6. **Important GPU/asset lifetime gap:** the actual scene coordinator currently releases scene resource refcounts inside its `commit/unload`. A native host must add an independent appearance/provider residency pin or a release-defer hook that spans visual stage, authoritative commit, visual unload and GPU fence completion. Otherwise the old Model may be freed before the visual bridge can detach and GPU commands complete. `MotionWeaveScenePublication` does NOT own ModelAssetProvider, does not implement GPU fences, and cannot safely replace that host lifetime mechanism.

## Failure modes and guarantees

- Duplicate/oversize object keys, missing parents, invalid socket Layout, incomplete or extraneous independent Graph snapshots, duplicate child IDs, bad Graph/Model publication identity and stale cache models fail before publishing the candidate.
- A staging failure preserves previous active scene, child Graph clocks and presentation Worlds. On successful replacement, old Graph players detach before child attachment entities; old Model/Graph owners remain host obligations.
- A compatible old child Graph's portable named playback can be queued for the new child if its rig matches. Any incompatible rig/state/clip safely restarts. The entire scene's compiled pose/physics/transient Graph caches are not serialized.
- One independent Graph writer per child; a static attachment compiles into the parent packet only and never ticks separately. Staged candidates perform a one-time dt=0 pose evaluation for validation; the host should not confuse this with a second LIVE graph writer.
- `saveScene(sceneId)` returns pointer-free authored visual layout JSON and object IDs, not authoritative SceneState, serialized Model pointers, source Graph clocks or GPU fences.
- The bridge deliberately holds no authoritative ECS World pointer and imports no SceneLoadCoordinator header. It is a reconciled adapter API prepared for integration once the peer branches are merged/reviewed.
- New `MotionWeaveScenePublication` source has NOT been natively compiled; transactional behavior, true CPU/GPU lifetime and multi-scene rendering require final native Mac tests.

## Acceptance plan

Create C++ fixtures for two committed scenes, duplicate stable IDs, disabled/static/independent children, graph import failure at object N after N-1 staged, failed coordinator commit/discard, successful swap, persistent-scene Replace, multi-scene unique draw IDs, unload, rollback, compatible/incompatible Graph clock continuation and model resource lease retirement. Then run CMake/CTest, real Studio/ModelView or game scene, Metal/Vulkan/GLES and sanitizer/GPU fault/VRAM suites in the agreed final acceptance campaign.

See [Stage 173 handoff](agent-handoffs/animation-stage173-scene-publication-2026-11-20.md).


## Stage 174 — real GPU-owned residency (dedicated scene provider)

The Stage 173 presentation-only visual bridge remains a valid low-level adapter, but it does not itself own GPU Models. For production native scene hosts with **one dedicated ModelAssetProvider per scene**, use `engine/view/MotionWeaveSceneOwnedHost.h` instead: it wraps the two-phase visual publication and strongly retains the actual provider through a separate GPU token queue in `engine/render/SceneModelProviderRetirement.h`.

This avoids treating appearance snapshots or CPU ModelReadLeases as GPU fences. It also stops the provider's asynchronous CPU reader before retiring the owner and validates `RenderDeviceLifetime` epochs. If GPU completion cannot be proven, the provider is quarantined and status is reported as a failure, not success.

See [scene GPU owner integration guide](motionweave-scene-gpu-owners.md) and [Stage 174 handoff](agent-handoffs/animation-stage174-scene-provider-gpu-residency-2026-11-20.md) for sequencing, limitations and diagnostic APIs. **Not yet fully nonblocking:** the current provider shutdown still waits for device idle. Shared-provider leases and the actual peer SceneLoadCoordinator GameModule integration remain open.
