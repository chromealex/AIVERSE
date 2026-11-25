# TerraWeave Caverns — Stage 35 handoff (2026-11-24)

## Branch and commits

Repository: `chromealex/AIVERSE`, working branch `feature/terraweave-caverns-core-20261123`; **not merged to main**.

Actual Stage35 commits:
- `7d3a11c14e60407864d2b95553b2db8df19d1759`: `engine/world/TerraWeaveCavernsSpatialShadow.h`, split reversible fresh-ID collider installation from final old-range removal.
- `ac42ffacf5ec273dd2b36e086f85ff904bd611ee`: `engine/world/TerraWeaveCavernsBox3DShadow.h`, split new-body creation and rollback from final old-body destruction.
- `010fe381a8b2a7c9f6656ae4964b332683e56c2f`: `engine/world/TerraWeaveCavernsGameplayPhysicsBarrier.h` first owner publication coordination.
- `60d31713f218a5f50dffc8545877fdb9f04ed2d4`: correct rollback state checks and require recovery on refusal.
- `c74eda4e58d41cae208b019cf6aa4d72eb0c334a`: expose SpatialWorld candidate installation status.
- `48fedb48618e063c60d779106da5be90ff74609c`: expose Box3D candidate installation status.
- `ac893277cd46e98bd4640c66d7b5efbdad553edd`: new `engine/world/TerraWeaveCavernsRuntimeFrame.h`, genre-independent game frame lifecycle integration hooks.
- `bb654298dfd84b56afb1a589e3003fa9c88d7f54`: external recovery trigger for late GPU failure.
- `14a6743870b1233b8fc350f9054b2c2a3f6f4109`: runtime halts further normal updates/draw if GPU activation fails after physical commit.
- `67eed6422bfab20f5c1a33d0e4b9570d8634362e`: mandatory agent skill update `skills/world/aiverse-terraweave-caverns/SKILL.md`.
- This document is the next commit; inspect HEAD for final tip.

## Feature implementation

**Problem:** Prior Stage34 host could prepare immutable SDF, GPU GeometryScene shards, per-chunk SpatialWorld+Box3D geometry and a native shadow NavMesh, but had no coordinated native owner publication. In particular, creating a Box3D body immediately inserts an active shape into the world. Independent sequential calls could fail halfway and expose a mixed visual/physics/nav revision.

**Stage35 resource installation split:**

`CaveSpatialShadow` now has `installCandidate(world,revision)` that inserts fresh dedicated collider IDs while the previous ID range remains; failed insert rolls back newly inserted IDs, without disturbing old. `revertCandidate` removes newly installed candidate IDs if another owner rejects the batch. `finalizeInstalled` removes old IDs and adopts new.

`CaveBox3DShadow` now has `installCandidate(scene,revision)` that creates a candidate Box3D body but keeps the prior body alive; `revertCandidate` destroys the candidate if another owner rejects. `finalizeInstalled` retires previous body and adopts candidate. An empty/filled chunk stages no new body, so finalization removes old body. In **all** cases Box3D candidates are live from creation, requiring genuine global simulation/query exclusion before any candidate installation begins.

**Coordinated owner barrier:**

New `CaveGameplayPhysicsBarrier::apply(commitView,boxScene,spatial,exclusiveProof)` preflights each chunk's sourceRevision and prepared shadow owner, plus native staged authored NavMesh. Under a caller-owned exclusive barrier it installs all fallible new SpatialWorld/Box3D candidates, rolls them back in reverse order on installation failure, then finalizes old Box3D and SpatialWorld ownership, and finally swaps the staged native NavMesh pointer. `CavePhysicsCommitStatus` exposes current phase and a sticky `RecoveryRequired` state on irreversible errors.

**Frame adapter:** New `CaveGameplayRuntimeFrame` supplies `onEditedVolume`, `onAuthoringOperation`, `frameOutsidePass` to drive Stage34 CPU/GPU/physics/nav staging, `publishWithExclusiveWorld` to run the stage35 barrier and Stage34 `commitAtOwnerBarrier`/Stage30 GeometryScene publication while all readers are paused, `drawCommitted`, `collectAfterGpuIdle` and `shutdownAfterGpuIdle`. This deliberately lives OUTSIDE the deterministic ECS/Simulation tick and requires explicit attachment to the renderer/physics owner lifecycle. No implementation of specific game genre is required.

**Late failure handling:** If the native physics resources were finalized but `CaveShardGpuHost::activateGroup` or later catalog change fails, `CaveGameplayRuntimeFrame` calls `physicsBarrier_.requireRecovery()` rather than letting future scene frames run with incompatible revisions.

## Important correctness limits

1. This is an **exclusive, coordinated publication protocol**, NOT a fully rollbackable cross-subsystem atomic transaction. `Scene::destroy` can fail in final retirement, and already removed body handles cannot be reinserted with identical identity. If finalization partially succeeds, recovery from authoritative world snapshot is required while the world remains paused. Do not mark all-owner atomicity done.
2. The `ExclusiveProof` callback must be backed by an actual held world/tick/render-reader exclusion, retained across `publishWithExclusiveWorld`. An unconditional `[] {return true;}` is not meaningful proof and must not be used in live gameplay.
3. There are brief simultaneous old and candidate Box3D bodies during installation; no physics step or query may interleave. The owner must also pause SpatialWorld queries and NavMesh readers until GPU scene activation and release of the exclusive barrier.
4. This adds a **reusable runtime hook**, but doesn't automatically instantiate it in an actual `GameCore`, native game client or dedicated-server loop. Putting GPU device calls inside `GameCore::setup` would violate deterministic simulation ownership; attach at a real renderer/world host frame boundary instead.
5. Stage34 `CaveGameplayEditHost::advance` may have GPU upload cost; real per-frame budget and device-lifetime backpressure still require additional work. The stage32 GPU preview pacing covers only editor visual-only mode.
6. Box3D, SpatialWorld, NavMesh and GeometryScene still need final native integration, streaming loaded-region policies and actual revision-controlled recovery. No duplicate cave draw path may be installed.
7. RecoveryRequired currently exposes status and blocks future `frameOutsidePass`, `onEditedVolume` and `drawCommitted`. An actual world reload / physics scene rebuild recovery operation is still pending.

## Tests actually performed

- User requested **features first, tests later**. Therefore no new test source or compiler/CTest/GPU execution was performed for Stage35. The GitHub connector confirmed source and skill commits, but this is NOT build or runtime QA.
- No claim of fully atomic rollback, Box3D native correctness, or editor/client render functionality is made. The earlier Stage33 isolated four-CPU-smoke pass does not prove Stage35 compilation.

## Next features (Stage36)

1. Attach `CaveGameplayRuntimeFrame` to the actual renderer/scene host lifecycle with an owned physics::Scene and SpatialWorld, and provide a true exclusive tick/query barrier for `publishWithExclusiveWorld` outside GPU command recording.
2. Implement loaded-region streaming: identify desired chunk sets, pin outstanding edited revisions and keep GPU/physics/nav residents retained until no owner or worker lease remains. Prevent rendering or collision of stale/unloaded tiles.
3. Support aborting staged work when the player moves beyond streaming radius or when project/world changes, including empty tile retirements, without losing Undo/Redo or authoring persistent SDF.
4. Implement recovery from `RecoveryRequired` by closing readers, reconstructing native physics/nav from one authoritative revision, then safely restoring presentation ownership. This is a real feature, not merely clearing a flag.
5. Continue updating `skills/world/aiverse-terraweave-caverns/SKILL.md` and a separate dated handoff after each stage. Hold comprehensive compilation/testing until feature completion per user instruction, without declaring release-ready.
