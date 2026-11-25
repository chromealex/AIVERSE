# TerraWeave Caverns — Stage 34 GAME runtime host handoff (2026-11-23)

## Repository and commit provenance

Repository: `chromealex/AIVERSE`.
Branch: `feature/terraweave-caverns-core-20261123`, NOT merged into main.

Stage34 GitHub commits:
- `7fa336036817e4d8570fc6562159ffecfbaf5e62` — `engine/world/TerraWeaveCavernsEditRuntime.h`: generic read-only `certifiedCandidates()` with editor alias `visualPreviewCandidates()`. No dirty ACK from either.
- `86c7b9ee36def77f79e1e0adda031d50a38c1c9c` — new `engine/world/TerraWeaveCavernsGameplayEditHost.h`: first complete authored SDF → CPU worker → GPU/physics/nav *preparation* coordinator.
- `3d3260d2982132d96acc60c9f0583a7761856912` — normalize saved MapDoc revision to monotonically increasing game runtime epoch so Undo/Redo yields new jobs.
- `3f34585d29819bcdb8c88762cb1f6f73d88d4b1b` — convenience `advance()` owner-frame hook; worker drain on final GPU-idle shutdown.
- `a92810a15b73f9c12833d1cfea5ec2e85f2c5c20` — explicit mutable physics stage access through external commit callback.
- `99a6521bca5b42baccc43646955d17a599997f08` — updated `skills/world/aiverse-terraweave-caverns/SKILL.md`.
- This file is a subsequent commit; inspect branch HEAD before making further changes.

## Feature implementation

**New:** `CaveGameplayEditHost`, a genre-independent world-owned C++ coordinator in `engine/world/TerraWeaveCavernsGameplayEditHost.h`. It depends on existing Stage22 `CaveEditRuntimeBridge`, Stage27 native cave volume collision, Stage28 `CaveVolumePhysicsStage`, Stage29 authored NavMesh stage and Stage30 per-shard real GeometryScene GPU staging. This does not introduce a separate simulation path or game-genre-specific module.

- `edited(CaveDirtyBounds,shared_ptr<const CaveVolumeSnapshot>)`: canonicalizes and defensively freezes the new authoring snapshot, assigns a strictly monotonically increasing runtime source revision independently of MapDoc (important for Undo), aborts outstanding unpublished resources, then enqueues affected chunks for async generation.
- `consumeAuthoringOperation(Json,shared_ptr<const CaveVolumeSnapshot>)`: interprets the existing `terraweave_caverns_paint` return contract `{ok,revision,dirtyBounds}`, verifies revision and forwards to `edited`. The owning game host must call it upon its own authoring operation; there is no implicit global Workspace subscription.
- `pumpCpu(budget)`: bounded Stage22 worker progress; `advance(gfx::Device&,const world::SpatialWorld&,CaveNavConfig,TerrainHeight,ColliderIdAllocator,visualRevision,budget,CaveBatchSpec)`: a single outside-render-pass owner-frame hook that advances workers then stages the complete source revision once ready.
- `stageReady(...)`: reads Stage22 `certifiedCandidates()` (not an editor-only API), ensures one matching sourceRevision, prepares each chunk's authoritative unsimplified 3D triangles for both Box3D and SpatialWorld (including zero-geometry retirements) using `CaveVolumePhysicsStage`, prepares native nav::Mesh via a shadow `CaveAuthoredNavStage` from the exact same SDF snapshot, then GPU-stages the complete affected GeometryScene shards with `CaveShardGpuHost::stagePreparedEdits`. It uses an externally provided **globally coordinated non-overlapping collider ID allocator**. On partial preparation failure, it cancels staged CPU nav/collider state and unpublished GPU sources.
- `commitAtOwnerBarrier(commitOwners)`: does NOT silently publish. It refuses incomplete/revision-mismatched staging, calls `worker_.publishReady` with exact matching chunk identity, serial and render revisions, and invokes ONE explicit user-supplied exclusive owner callback through `geometry_.activateGroup`. The callback receives `CaveGameplayCommitView` with the staged meshes, visual shards, mutable per-chunk physics stage owners, native authored navigation stage, the frozen snapshot and visual revision. **Only after a true host-confirmed commit** does Stage22 acknowledge dirty entries. Rejection leaves the gameplay dirty queue pending and retains previous active visual sources.
- `cancelPending`, `registerCommittedMesh`, `draw`, `collectAfterGpuIdle`, `shutdownAfterGpuIdle`, `stats` and read-only stage accessors complete the public host lifecycle. `shutdownAfterGpuIdle` waits for outstanding CPU workers and requests explicit GPU-idle cleanup.

### Critical safety and integration status

1. **This stage implements automatic cross-subsystem resource PREPARATION, not a proven all-or-none gameplay commit.** Stage26 Box3D `Scene::create` immediately inserts an active body. A host callback that commits Box3D/SpatialWorld/NavMesh sequentially without native rollback may leave an inconsistent world after partial failure. A callback which returns true without actually committing resources is unsafe and prohibited. Stage35 must build a verified host-exclusive game-tick barrier and rollback policy or inactive physics staging.
2. `CaveGameplayEditHost` is a reusable C++ owner that must be instantiated and wired by a game/world lifecycle implementation. It has NOT been inserted into the existing `GameCore`/RTS/FPS/native client loop in this stage; do not describe it as automatically running in a launched game until done.
3. Existing per-shard GeometryScene GPU upload can be expensive and resource retirement still requires real GPU idle when STRATA sidecars are live. The Stage32 per-frame pacer presently applies to editor preview, not necessarily the game host; the game host must respect graphics owner frame budgets.
4. Any field change that touches wide areas must provide complete conservative `CaveDirtyBounds`; Undo needs union of affected old/new SDF stamps. Unchanged loaded neighbors must be seeded via `registerCommittedMesh` and validated by Stage16 seam gate.
5. The new `registerCommittedMesh` registers CPU/catalog and seam metadata; it does NOT itself upload the initial GeometryScene. Actual runtime bootstrapping and source ownership must be handled by the game host without double drawing.
6. Stage34 `commitAtOwnerBarrier` can still encounter native owner failures after an external callback reports success; the Stage30 GPU/catalog functions assume exclusive owner and should be moved toward an infallible final swap with a true rollback barrier in Stage35.

## Tests actually run

- In accordance with the user's **features first, tests later** direction, Stage34 adds no new test source and performs **no native C++ compiler, CMake/CTest, GPU/STRATA, Box3D or NavMesh runtime test**. Source was inspected through the GitHub connector and all file updates were acknowledged with concrete commit SHAs.
- Previous Stage33 four isolated CPU smoke checks are NOT evidence of Stage34 native compatibility. Do not say Stage34 code builds or works end-to-end yet.

## Next feature, Stage35

1. Connect this host to a lifecycle owner within the native game/runtime (not the editor-only preview), establishing world load/unload, persistent GPU scene draws, and authoring-event routing.
2. Implement a genuinely exclusive tick/render owner publication transaction: stage stable Box3D bodies or an alternative shadow physics world with rollback, globally reserved SpatialWorld IDs, staged native nav pointer and GPU shard owners, then atomically switch reader-visible epoch. No intermediate query/simulation must see a mixed epoch.
3. Add recovery when a prospective source was rejected, native physics resource capacity fails, or a newer authoring epoch invalidates staged candidates. Never acknowledge the Stage22 dirty queue until full commit.
4. Continue per-feature skill + dated handoff after Stage35. Defer the comprehensive full-branch compile/test suite until functional completion, per user instruction, but do not claim release readiness before real QA.
