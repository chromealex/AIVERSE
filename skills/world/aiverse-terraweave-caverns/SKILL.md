---
name: aiverse-terraweave-caverns
description: Generate deterministic genre-independent underground chambers, capsule tunnels, volume occupancy and smooth CPU cave meshes via world-space density and marching tetrahedra.
---

# TerraWeave Caverns — agent skill (stage 2)

Read `engine/world/TerraWeaveCaverns.h`, `engine/world/TerraWeaveCavernsMesh.h` and `docs/agent-handoffs/caverns-stage2-2026-11-23.md` before editing.

## API
- Namespace `aiverse::terraweave::caverns`.
- `Settings` stores a seed, cell size, tunnel/chamber sizes and probabilities, min/max Y. Positions and geometry parameters use **integer centimetres**.
- `isVoid(settings, PointCm)`: original binary analytic occupancy for block-world carving.
- `caveFieldCm(settings, PointCm)`: signed, approximate smooth density, **positive inside cave** and negative in rock. It is not exact Euclidean SDF.
- `generateChunk(settings, ChunkKey3, resolution, spacingCm)`: original X-fastest binary cell-center occupancy.
- `generateCaveMesh(settings, ChunkKey3, resolution, spacingCm)`: independent CPU surface geometry `CaveMesh` with triangle-list indices and chunk-local **metre** vertices/normals; place at world origin `key * resolution * spacingCm / 100` metres.
- Generated triangles are not welded; same-spacing neighbors sample the same world lattice, but different LODs require a seam transition system.

## Ownership and restrictions
- No dependencies on RTS/FPS or a renderer. CPU jobs create mesh; RHI uploads in a different layer; physics/navmesh should consume a durable mesh or bake.
- Do not duplicate/replace TerraWeave heightfield, VoxelGen or VoxelWorld. Opt-in adapters only.
- No persistent mesh cache or edits yet. Seed/settings + explicit generator version must be saved; **do not claim compatibility** after changing hash, surface extraction, or coordinate rounding.
- Field and mesh generation are compute-heavy; avoid updating per frame. Implement caching/budgets in a separate integration layer.
- Do not claim mesh is watertight or crack-free with arbitrary LOD differences without seam tests. No cave entrances, decorations, flora, cave lakes or collision in this stage.
- After every stage create a dated handoff describing exact changed files, APIs, actual tests, constraints, branch/commit and next action.

## Validation
Standalone tests:
`c++ -std=c++20 -O1 -I. tests/terraweave_caverns_mesh_test.cpp -o /tmp/cave_mesh_test && /tmp/cave_mesh_test`.
Run in checked-out repository, plus engine CI and negative-key/neighbor seam tests before merge.

## Stage 3: surface entrances (2026-11-23)
Read `engine/world/TerraWeaveCavernsEntrances.h` and
`docs/agent-handoffs/caverns-stage3-2026-11-23.md`.

- `EntranceSettings`: deterministic global X/Z lattice, radius, min/max search depth, search step and chance permille.
- `generateEntrances(caves, entrySettings, SurfaceRectCm, heightAtCm, excluded)`: template with callbacks for **authoritative surface height** and optional authored exclusion at entrance center; returns only candidates whose vertical ray finds an existing cave field greater than zero. Bounds are half-open. To cover a chunk and neighbor gradient samples, generate entrances from a larger surface rectangle than the target chunk.
- `Entrance`: stable identifier, signed-cm position of shaft, sampled terrain Y, verified existing cave Y, radius.
- `entranceFieldCm(entrance, point)`, `combinedFieldCm(caves, entries, point)`, `surfaceHoleAt(entries,x,z)`: union cave/shaft geometry and surface-hole footprint query.
- `generateCaveMeshWithEntrances(caves, entrances, chunkKey, resolution, spacingCm)`: meshes a combined field using `generateCaveMeshSampled`, while the old `generateCaveMesh` remains unchanged.
- To use TerraWeave: pass a lambda calling `aiverse::terraweave::heightAtCm(terrainSettings,x,z)`. Do **not** duplicate the height generator or mutate the terrain inside this pure library.
- **A surface hole is not yet applied to terrain meshes.** The owning TerraWeave surface adapter must clip/remove triangles within the footprint, regenerate collision/navmesh and invalidate appropriate chunks; never assume `surfaceHoleAt` modifies the heightfield automatically.
- A found cave connection is a pointwise density hit, not a proved traversable opening for a character; collision, minimum clearance and connectivity proof remain integration tasks.
- Persist cave, terrain and entrance seeds/config versions and authored overrides. Never treat generated entrances as mutable global singletons.
- Run standalone tests `tests/terraweave_caverns_entrances_test.cpp` in a real checkout before claiming verified functionality.

## Stage 4: cutting native TerraWeave terrain triangles
- Source: `engine/world/TerraWeaveCavernsSurfaceCut.h`. Handoff: `docs/agent-handoffs/caverns-stage4-2026-11-23.md`.
- `cutTerrainEntrances(const terraweave::TerrainPatch&, const std::vector<Entrance>&, sides=16, maxTriangles=200000)` accepts **authoritative** TerraWeave TerrainPatch (not camera LOD) and produces `CutSurface` with world-centimetre double XYZ triangles and dirty-region bounds.
- The cutter subtracts a regular polygon aperture per shaft from existing `TerrainPatch::collision` triangles, preserving winding and interpolating Y on each ORIGINAL terrain triangle plane. It uses a globally ordered id list and a strict triangle budget.
- For integration, **do not pass resulting variable-count triangle lists** to existing `TerrainCollisionPublisher::publish` or `makeEarthworksTerrainModel`: both expect fixed heightfield topology. Adapt the native publisher's topology contract so the same clipped triangle soup feeds both render::Model and SpatialWorld. Do not publish one without the other.
- `dirtyRegions` are merely planning metadata. They do not rebuild collision/navmesh/vegetation until the owner-thread publish barrier actually commits matching revisions. No new collision or navmesh owner has been implemented.
- Near aperture is an inscribed regular polygon; shaft field remains round. Ensure matching shaft aperture/transition rim in future stage to avoid polygonal collision lips.
- Missing: actual published runtime adapter, complete render+collision transaction, navmesh invalidation delivery, cave portal rim mesh, terrain material/UV seam, CTest registration, live GPU proof.

## Stage 5: native shared render/collision resident
- Read `engine/render/TerraWeaveCavernsResident.h` and `docs/agent-handoffs/caverns-stage5-2026-11-23.md`.
- `buildCutNativePayload(cut, cellSizeMetres, materialSource, uvScale)` converts **one** `CutSurface` triangle list to `CutNativePayload`: collision triangles in `Vec3/Fixed` and the matching `render::Model`; reject invalid geometry. `Model` uses same borrowed-texture policy as existing dynamic terrain.
- `CutTerrainResident::publish(device, spatial, std::move(payload), firstColliderId, cellSizeMetres)`: owner-thread opt-in publication. It uploads a new GPU model and adds its new collision range before removing old collision/model. **Caller MUST reserve a fresh, nonoverlapping global collider ID range** on each republish, and hide the replaced original terrain tile in BOTH render and collision owners. Cannot reuse the existing range on a republish.
- `CutTerrainResident::submit(renderer, origin)` follows existing Renderer3D model submission. `shutdown(device,spatial)` removes owned collision and releases GPU model before device destruction.
- `takeDirty()` yields candidate dirty rectangles **only after a successful publish**. At the host barrier, use these to invalidate existing native NavMesh, vegetation and Horizon derived state. This stage does not call native NavMesh APIs or guarantee navigable walk paths.
- No direct Box3D bridge yet. SpatialWorld triangle publications are real, but Box3D owners need a separate authoritative geometry replacement strategy.
- GPU model lifetime and publication failure paths need real Metal/GL/Vulkan and collider-ID exhaustion tests before main merge. The unmodified ordinary `TerrainPatchResident` remains owner of normal (unclipped) tiles.

## Stage 6 — Box3D, surface NavMesh and STRATA
Read `engine/world/TerraWeaveCavernsBox3D.h`, `engine/world/TerraWeaveCavernsNav.h`, `engine/render/TerraWeaveCavernsStrata.h`, and `docs/agent-handoffs/caverns-stage6-2026-11-23.md`.

- `makeCavernsBox3DBody(payload,tileSizeMetres,userData)`: converts the very same authoritative `CutNativePayload::collision` triangles into a deduplicated Box3D static mesh in tile-local metres. `CavernsBox3DPublisher::publish(scene,payload,tileSizeMetres,userData)` replaces one pinned tile body, creating before destroying the old; returns false on stale revision or physics allocation failure. Call between `physics::Scene::step` on scene owner thread, never duplicate original TerraWeave body.
- `surfaceEntranceBlocked(EarthworksNavConfig, vector<Entrance>, previousBlocked)`: immutable captured blocker for the existing `EarthworksNavSource`; retains NEREID/authored blockers and disallows surface walk spans inside holes. `refreshCavernsSurfaceNav(mesh,source,dirty,err)` forwards half-open cm dirty rectangles to existing `refreshEarthworksNav`. NO multi-level underground nav generation yet; a vertical shaft is not walkable just because its surface cell was removed.
- `planCaveStrataLease(key,generatorVersion,sourceRevision,renderRevision,strataEnabled,isStatic,topologyValidated,hasCommittedGeometry)` returns conservative presentation routing metadata. STRATA owns mesh cluster hierarchy / residency / LOD; TerraWeave Caverns owns generated source mesh and revision. `StrataEligible` does NOT mean Strata has accepted/submitted geometry: GeometryScene and Renderer3D still decide that, with proof of exactly-once rendering and fallback. `caveStrataLeaseCurrent` rejects stale cave/entrance/LOD candidates.
- Never route collision or NavMesh through STRATA's approximate visual LOD mesh. STRATA may simplify static cave walls and decorations, but authoritative collision remains sourced from stable immutable cave geometry revisions.
- GPU / Box3D / NavMesh / STRATA per-tile integration into a live scene is **not certified** until host barrier sequencing, source lease expiry, removal of original tile bodies, and hardware proof. Keep Renderer3D fallback.
- For stage-6 tests see `tests/terraweave_caverns_stage6_test.cpp` and `tests/terraweave_caverns_box3d_test.cpp`; tests have not been run yet.

## Stage 7 — bounded streaming, multi-span cave nav and shaft walls (2026-11-23)

Mandatory files: `engine/world/TerraWeaveCavernsStreaming.h`, `engine/world/TerraWeaveCavernsMultiNav.h`, `engine/world/TerraWeaveCavernsShaftMesh.h`, `docs/agent-handoffs/caverns-stage7-2026-11-23.md`.

### Streaming
- `CaveStreamPlanner(StreamSettings)` accepts `setCenter(ChunkKey3)`, `setSourceRevision(revision)`, `setVisualLod(lod)`, then `pump()` emits bounded `Build` and `Retire` requests, nearest keys first with stable tie breaks. `accept(request,revision)` rejects stale or retired worker results. Do not use `accept` to imply GPU/model/Box3D publication succeeded; mark ready only after a separate atomic host publication or extend planner with explicit ready/commit states.
- `StreamRequest::lod` is visual-only; cave collision, Box3D, and native NavMesh remain independent from STRATA selection.
- Caller must not use uncommitted or stale mesh assets after a revision update. The host owns worker scheduling, GPU resource retirement and physics barriers, not the planner.

### Native multi-level navigation
- `CaveNavSource(Settings,CaveNavConfig,vector<Entrance>,terrainHeight)` extends the existing genre-independent `nav::Source`. `spans` samples vertical cave floor candidates that have sufficient head clearance and are below sampled terrain; `link` allows adjacent columns by max step/slope. `caveNavMeshConfig` returns matching `nav::Config`.
- This is a coarse raster walkability hypothesis; height sampling can miss narrow ledges, agent radius and shaft traversability are NOT guaranteed. No off-mesh ladder/climb links. Test real paths and floors before enabling gameplay routing.

### Shaft transition
- `makeEntranceShaftSurface(Entrance,heightAt,sides,verticalSections)` produces an explicit cylindrical mesh wall between terrain height around entrance and cave target depth. Output vertices are **world centimetres as double**, triangles are indexed. Use the same polygon side count as `cutTerrainEntrances` and use an immutable terrain snapshot when supplying heightAt.
- The cutter clips against the original terrain triangle plane. Height source lookup at the rim may NOT perfectly match piecewise interpolation on a deformed tile; sample the actual authoritative terrain triangle surface in production or reconcile lip vertices to avoid cracks.
- Generated walls are only geometry. Host must convert to the shared `render::Model` and collision mesh and publish at the same owner-thread revision barrier.

### STRATA
- STRATA is presentation-side clustered geometry/LOD, not collision/navmesh. The existing GeometryScene must explicitly acquire live immutable chunk geometry, verify its revisions, manage residency leases and exclusive render routing. `planCaveStrataLease` is metadata only.
- Do not claim active STRATA streaming merely because this planner emits mesh requests. Continue ordinary Renderer3D until parity/ownership tests pass.

### Verification
- Stage 7 adds `tests/terraweave_caverns_streaming_test.cpp` and `tests/terraweave_caverns_multinav_test.cpp`. These have not been compiled or run in this turn. Do not mark them PASS without real build logs.
- After EVERY implementation stage append an exact handoff with changed files, commits, API, tests actually run, limitations and next actions; update this skill too.

## Stage 8 — safe publish gating and collider leases

Read `engine/world/TerraWeaveCavernsPublication.h`, `engine/world/TerraWeaveCavernsColliderLeases.h`, and `docs/agent-handoffs/caverns-stage8-2026-11-23.md`.

- `CavePublicationCoordinator(maxPending)`: `begin(key,sourceRevision,renderRevision,requiredMask)` issues a new unique versioned ticket; `prepared(ticket, CavePublishPart)` acknowledges staged Renderer / SpatialWorld / Box3D / Navigation and optional STRATA; `commit(ticket, ownerBarrierSucceeded)` marks the ticket committed only when EVERY required stage is ready and the host confirms barrier success. `abort` cancels pending work, `retire` discards the key, and `readyForGameplay(key,revision)` refuses stale/incomplete tiles.
- The coordinator is **not a multi-system rollback implementation**. Do not call `prepared` for resources that have already replaced visible scene state; all steps must be prepare-only until a host-controlled owner barrier makes every subsystem visible. A failed commit must preserve old render/collision/physics/navigation and release staged candidates. Current stage-5/6 resident publishers perform their own immediate swaps: THEY ARE NOT YET WIRED into a single atomic host transaction. Do not claim actual atomic publication or playable caverns.
- `CaveColliderLeasePool(first,count)` uses a dedicated globally reserved collider-ID namespace and returns `ColliderLease {first,count,token}`. Different stage revisions require fresh nonoverlapping ranges. Only release a lease after all associated SpatialWorld colliders have been removed. Tokens prevent a stale release from stealing a newly reused range.
- STRATA stage-6 eligibility is not an acquisition handshake. Actual STRATA source adoption must be performed by GeometryScene and acknowledged only when residency, safety, and exactly-once draw routing are verified. Until then use Renderer3D fallback and omit STRATA from requiredMask.
- Never allocate IDs from a pool overlapping gameplay/terrain owners; pool cannot detect such external overlap itself.
- Test source: `tests/terraweave_caverns_publication_test.cpp`. Added only; actual checkout build / CTest not run.

## Stage 9 — STRATA/Renderer3D exclusive presentation lease

Read `engine/render/TerraWeaveCavernsPresentationLeases.h`,
`tests/terraweave_caverns_presentation_test.cpp`, and
`docs/agent-handoffs/caverns-stage9-2026-11-23.md`.

- `CavePresentationLeases` is a per-chunk host-side versioned presentation state machine. `begin(chunk,sourceRevision,renderRevision)` invalidates stale STRATA candidates and returns a generation token. `confirmFallback(ticket)` MUST only be called after the new Renderer3D model has been uploaded and made available by the host. `confirmStrataPrepared(ticket)` MUST only be called after the existing GeometryScene has actually acquired the source and verified all geometry/material/GPU leases.
- `activateStrata(ticket, exclusiveDrawProof)` is allowed only after STRATA is prepared, Renderer3D fallback is available, and the host proves exclusive routing in **all** relevant render passes. `route(chunk)` is a decision for host draw submission; calling it does not submit or suppress any draws. `deactivateStrata` reverts to fallback before STRATA source expiry.
- `requestRetirement` records pending teardown but does **not** hide a live chunk. `finishRetirement(ticket,resourcesReleased)` is valid only after the host observed GPU fences, GeometryScene lease release and collision/navmesh/Box3D retirement; stale tickets cannot retire new versions.
- This is NOT live GeometryScene insertion, STRATA cooking, GPU culling or draw submission. The current source is a protocol only. No false claims of working STRATA streaming or atomic cross-system rollback.
- IMPORTANT: `begin` on an existing STRATA-owned presentation requires the host to arrange a safe fallback and release old STRATA GPU residency before changing visibility; a route value alone cannot enforce GPU synchronization.
- Keep collision/Box3D/navmesh independent of visual STRATA LOD. Do not remove original terrain source until the new source is committed, and do not double-submit the same geometry through both routes.
- After completing any stage update this skill and a dated handoff; record tests actually executed, not simply authored.

## Stage 10 — real GeometryScene prepared-model entry point

Read `engine/render/TerraWeaveCavernsGeometryScene.h` and `docs/agent-handoffs/caverns-stage10-2026-11-23.md`.
- `makeCaveGeometryModel(CaveMesh)` converts CPU triangle mesh to an actual rigid, unuploaded `render::Model` with a default rock material. Coordinates are chunk-local **metres**.
- `caveGeometrySourceKey(key,generatorVersion,revision)` creates a versioned model key; `caveGeometrySceneManifest(key,generatorVersion,revision,resolution,spacingCm,strataEnabled)` makes the actual `GeometryScene` JSON schema with one instance placed at chunk origin in metres.
- `CaveGeometrySceneResident::publish(device, mesh, generatorVersion, revision, resolution, spacingCm, strataEnabled)` creates a staged `render::GeometryScene`, sets its Strata staging/portable flags, and invokes the **existing** `GeometryScene::loadPrepared` with an owned unuploaded `Model`. `draw(renderer)` submits through the actual GeometryScene owner; `shutdown(device)` releases it.
- Calling GeometryScene with Strata enabled does **not guarantee** it chooses STRATA or achieves visual parity. Query GeometryScene status/decisions; keep safe fallback. Do not draw an equivalent `CutTerrainResident` model concurrently with this scene.
- This is an opt-in proof-of-integration for ONE cave chunk per GeometryScene; DO NOT launch hundreds of these per frame. Production must batch scene sources and add incremental updates with bounded GPU lifetime and staging budgets.
- Current `publish` calls `GeometryScene::shutdown` for previous source after stage. This requires GPU idle, is not a nonblocking retirement strategy, and does not create a cross-system render/Box3D/SpatialWorld/NavMesh atomic transaction. Never claim complete gameplay-ready cave streaming.
- Check real engine compilation and the new `tests/terraweave_caverns_geometry_scene_test.cpp`, then run Metal/GLES/Vulkan raster parity before enabling by default.

## Stage 11 — batched GeometryScene ingestion (2026-11-23)

Read `engine/render/TerraWeaveCavernsBatchGeometry.h`, `tests/terraweave_caverns_batch_geometry_test.cpp` and `docs/agent-handoffs/caverns-stage11-2026-11-23.md`.

- `CaveBatchChunk` owns one `CaveMesh` plus generator version, source revision, mesh resolution and spacing. `CaveBatchSpec` defines max chunk count (<=1024), max vertex count (<=16 million), and STRATA opt-in.
- `prepareCaveBatch(vector<CaveBatchChunk>, CaveBatchSpec)` sorts by signed XYZ chunk key to make source and instance order independent of worker completion order, rejects duplicate keys and invalid inputs, enforces a global vertex budget, skips entirely empty chunks, and returns one `Json` manifest with a map of unuploaded `render::Model` objects. GeometryScene source keys embed chunk position/version/revision. World translations are in metres; source vertices stay chunk-local metres.
- `CaveBatchGeometryResident::publish(device,chunks,batchRevision,options)` stages ONE actual `render::GeometryScene` via `loadPrepared` and swaps the previous batch only after staged load succeeds. `draw(renderer)` invokes GeometryScene's own draw path, including its STRATA selection/fallback, not an extra Renderer3D draw. `shutdown(device)` performs normal GeometryScene shutdown.
- **Important:** this rebuilds the entire batch even for one changed chunk. `GeometryScene::shutdown` of an older batch still requires a device idle/lifetime barrier. This is not incremental streaming or frame-time-safe live update. Integrate bounded scheduling and GPU-fenced deferred retirement next; do not promise zero hitches.
- Do not double-submit the same batch through `CaveGeometrySceneResident`, `CutTerrainResident` and `CaveBatchGeometryResident`. Keep physical mesh/Box3D/NavMesh independent of STRATA visual LOD. Source revisions are part of the manifest identity but do not themselves constitute a live update notification.
- No LOD seam repair is implemented by batching. Preserve identical world-space field sampling and add explicit multi-resolution seam transitions and GPU verification before production.
- Run the stage-11 regression test in a full checkout with correct GeometryScene/Model/Json linking; test source exists but has not been executed in this stage.

## Stage 12 — shard-local rebuild planning and same-LOD seam diagnostics

Read `engine/world/TerraWeaveCavernsShards.h`,
`engine/world/TerraWeaveCavernsSeamAudit.h` and
`docs/agent-handoffs/caverns-stage12-2026-11-23.md`.

- `CaveShardRegistry(CaveShardConfig)` partitions global signed cave chunk keys into 1–4 chunks-per-axis groups using mathematical floor division (correct for negatives). `upsert(chunk,sourceRevision)` and `erase(chunk)` invalidate only their shard. `pump(maxCount)` emits bounded `CaveShardBuild` requests with a unique generation, stable sorted chunk keys, and an empty key list for retire. `acknowledge(request)` only after successful geometry publication; `fail(request)` releases an unsuccessful stage for retry. Old/replaced worker generations cannot acknowledge a new one.
- A shard corresponds to one `CaveBatchGeometryResident` owner. The host builds the request's chunk keys from an immutable source snapshot and stages that ONE GeometryScene replacement. Unchanged shards retain their current scenes. These APIs **do not yet** directly drive `gfx::Device` or GPU-fenced resource retirement. This is not fine-grained STRATA GPU incremental source replacement; each affected shard is still rebuilt wholesale.
- `auditCaveSameLodSeam(left,right,resolution,spacingCm,axis)` compares shared world-space mesh boundary edges after quantization to integer centimetres. The result reports unmatched edges; only supported for SAME spacing/resolution, strictly adjacent signed keys. `matched()` means border edges coincide, not that topology is watertight or that normals/materials match.
- In particular, **different LOD seam bridging is not implemented**. No automatic stitching, Transvoxel or geometric morphing here. Do not mark mixed-resolution neighbors crack-free.
- Tests: `tests/terraweave_caverns_shard_seam_test.cpp`. Source authored, not yet executed in a complete AIVERSE checkout. Ensure that each regression is compiled/executed before claiming PASS.

## Stage 13 — mesh welding and conservative mixed-LOD avoidance

Read `engine/world/TerraWeaveCavernsLodWeld.h`, `tests/terraweave_caverns_lod_weld_test.cpp`, and `docs/agent-handoffs/caverns-stage13-2026-11-23.md`.

- `weldCaveMesh(mesh)` shares duplicate vertex positions rounded to integer **local centimetres**, reindexes triangles, removes index-degenerate triangles, and averages input vertex normals. It is a deterministic visual mesh simplification; it does NOT alter authoritative collision/physics/navmesh, and it is not an edge topology repair algorithm. Verify sharp normal/material seams before enabling for authored meshes. Source triangles with zero area but distinct indices can still remain.
- `planCaveSeamSafeLod(vector<CaveLodEntry>)` is a conservative **LOD policy** for currently loaded positive/negative chunks: all face-connected active chunks are promoted to the finest common spacing, keeping the same physical chunk span. This avoids mixed-resolution boundaries inside connected groups, at the cost of potentially over-refining long connected regions. It does NOT create Transvoxel/skirt/stitch geometry, nor does it prove geometric seam continuity. Each output `resolution` remains <=64.
- Run `auditCaveSameLodSeam` after actual mesh generation on each shared face. If audit reports unmatched edges, do not mark scene ready for crack-free display; defer/correct the source. Do not confuse matching integer-cm edge coordinates with matching normals, UVs or cross-chunk STRATA cluster safety.
- STRATA must consume the welded **presentation-only** result as an immutable versioned source. The physics/navmesh owner must continue to use authoritative original geometry. STRATA selection must never independently lower the resolution across an uncoupled boundary unless its own source-wide seam certification proves safe.
- Test source added; no actual full-engine native build or GPU/STRATA seam acceptance executed this stage. Report tests NOT RUN until there are logs. Update this skill and a separate handoff after each following stage.

## Stage 14 — selective mixed-LOD contour stitching

Read `engine/world/TerraWeaveCavernsLodTransition.h`,
`tests/terraweave_caverns_lod_transition_test.cpp`, and
`docs/agent-handoffs/caverns-stage14-2026-11-23.md`.

- `stitchCaveCollinearBoundary(coarse,coarseResolution,coarseSpacingCm,fine,fineResolution,fineSpacingCm,axis)` accepts adjacent +X/+Y/+Z chunk meshes of equal physical span, with fine spacing <= coarse spacing. It operates on **presentation meshes only**. It audits the shared world-coordinate boundary, and subdivides coarse boundary triangles where fine contour segments partition a *collinear* coarse edge. Returns `CaveTransitionResult{coarseStitched,splitTriangles,supported}`.
- `supported=true` certifies the resulting chunk-face edge set matches the fine source's corresponding edge set at integer-centimetre precision. It is NOT proof of normal continuity, physical watertightness, or STRATA cluster LOD stability in every render pass.
- `supported=false` is a NORMAL result for independently sampled curved/branched cave contours, overlapping boundary features, incompatible or ambiguous topology. Do not display a cracked transition. Fall back to `planCaveSeamSafeLod` from stage 13, regenerate both neighbors at a common spacing, and rerun the same-resolution seam audit.
- This is **not general Transvoxel**. Do not claim arbitrary mixed LODs are seamless. Geometry is only changed on the coarse visual source; Box3D / SpatialWorld / native NavMesh remain unchanged. Do not stitch across different world spans, source revisions or incompatible density snapshots.
- Host must publish updated meshes to GeometryScene/STRATA with a new render revision; preserve Renderer3D fallback while STRATA residency updates.
- C++ tests for all three positive face axes and divergent fallback are authored, NOT executed in the full AIVERSE checkout yet.

## Stage 15 — six-direction LOD face policy and revision leases

Read `engine/world/TerraWeaveCavernsLodPolicy.h`, `tests/terraweave_caverns_lod_policy_test.cpp`, and `docs/agent-handoffs/caverns-stage15-2026-11-23.md`.

- `resolveCaveFaceLod(CaveFaceSpec)` validates two adjacent chunks in **either argument order** and on any of six cardinal directions. It verifies equal physical chunk span and audits the common world-coordinate face.
- `CaveFaceDecision::ExactMatch`: the actual boundary-edge sets match at integer-centimetre precision. `Subdivided`: stage-14 collinear coarse-low/fine-high edge subdivision succeeded and was re-audited. `RegenerateAtCommonLod`: unsafe or unsupported transition; do NOT render the unmatched pair. Rebuild both chunks at returned `commonResolution/commonSpacingCm` and run the stage-12 same-LOD seam audit before exposure.
- `CaveFaceLease` and `caveFaceLeaseCurrent` provide externally supplied revision checks. The host MUST certify each face against the actual immutable meshes and invalidate certification after either side changes; mismatched revisions may not enter STRATA visibility.
- This policy is NOT general Transvoxel, not real incremental GPU streaming, not a physics or navigation modification, and not proof of seam-normal continuity or absence of internal nonmanifold triangles.
- Source changes must not bypass `CavePublicationCoordinator` and presentation lease gates. Any fallback regeneration should update visual revisions and STRATA source leases, leaving original collision data authoritative.
- Stage-15 test source covers X/Y/Z planes, negative coordinates, both argument orientations, divergent contour fallback, and revision guards. No compiler/CTest or GPU verification of this stage has been run yet.

## Stage 16 — revision-safe regeneration / seam visibility gate

Read `engine/world/TerraWeaveCavernsSeamGate.h`,
`tests/terraweave_caverns_seam_gate_test.cpp`, and
`docs/agent-handoffs/caverns-stage16-2026-11-23.md`.

- `CaveSeamPublicationGate(maxChunks)` tracks immutable `CaveCertifiedChunk` values: mesh, resolution, spacingCm, sourceRevision, renderRevision. Call `upsert` when an actual completed geometry revision arrives, `erase` at unload. Each update invalidates all neighboring face certificates.
- `analyze()` processes loaded +X/+Y/+Z neighbors (six directions covered via both ends), calls `resolveCaveFaceLod`, caches only **exact matching edge** certificates, and queues current `CaveRebuildRequest` for coarse neighbors that require the finest common lattice. A supported stage-14 subdivision still needs a changed mesh to be **published**, so stage 16 conservatively regenerates coarse meshes instead of claiming a stitch already resident.
- `requests(budget)` emits deterministic, bounded work. `accept(request,rebuilt)` rejects stale serial, wrong mesh key, wrong source revision or LOD mismatches. After acceptance call `analyze()` AGAIN: accepting one job is never enough to certify a face. `canPublish(key,renderRevision)` rejects a stale revision, any pending rebuild or any uncertified loaded neighbor.
- `CaveFaceLease` is valid only for the actual current render revisions of both chunks. The host must gate `CaveBatchGeometryResident` / STRATA source adoption using `canPublish` for all involved chunks, stage a cohesive shard or neighbor group, then commit at its real GPU/physics owner barrier. The gate does not directly perform mesh generation, STRATA insertion, RHI commits or Box3D/NavMesh rollback.
- When same-resolution generated borders still differ, the gate **fails closed**: no unsafe geometry is published, and it cannot automatically repair inconsistent density/mesh topology. Diagnose origin, field, quantization, or meshing errors instead of retrying infinitely.
- The gate supports faces only between currently loaded chunks. Neighbor streaming/unload must trigger re-analysis; no claims about unseen future neighbors.
- The Stage 16 regression exists but has NOT been compiled or run as part of this work. Add the appropriate native CMake/CTest target and confirm real runtime behavior before main merge. Continue skill and dated handoff updates after every stage.

## Stage 17 — asynchronous seam regeneration CPU workers

Read `engine/world/TerraWeaveCavernsAsyncRebuild.h`, `tests/terraweave_caverns_async_rebuild_test.cpp`, and `docs/agent-handoffs/caverns-stage17-2026-11-23.md`.

- `CaveAsyncRegenerator(maxWorkers)` is a bounded C++20 `std::async` CPU worker dispatcher. Call `CaveSeamPublicationGate::analyze()` before initial dispatch. `pump(gate, immutableSettingsSnapshot, budget)` consumes `gate.requests()`, launches at most `maxWorkers` work items, captures settings and request by value, and suppresses duplicate jobs for one chunk. `poll(gate)` consumes completed futures on the scene owner thread; successful results go through `gate.accept` and `gate.analyze` before any presentation. `drain()` waits for current workers before world teardown.
- Worker code invokes `generateCaveMesh(settings,key,resolution,spacing)` only. It must NEVER touch ECS, `gfx::Device`, `GeometryScene`, STRATA, Box3D, SpatialWorld, NavMesh or live settings.
- The host must associate one immutable **world snapshot / generator version** with every sourceRevision and must increment that revision if settings, terrain entrances, authored edits or density fields change. `CaveRebuildRequest` carries sourceRevision but the dispatcher does not independently validate its relation to the passed Settings snapshot.
- `CaveRebuildPumpStats` exposes launched/completed/accepted/stale/failed/inFlight counters. On worker exception the gate remains not-ready; no unverified geometry becomes visible. Caller handles diagnostics, retry policy and backpressure.
- The stage-17 gate rebuilds only procedural base cave mesh, NOT combined entrance field, hand-authored edits, geological stamps, or material payloads. Do not use its output in worlds with those modifications until a versioned field-sampler callback is integrated.
- `CaveSeamPublicationGate::canPublish` must be checked before proposing any stage-11/12 GeometryScene shard swap. The host still needs to stage and atomically commit matching render + collision + Box3D + NavMesh. This dispatcher is NOT such a transaction.
- `std::async` is a portability baseline rather than AIVERSE's final shared job-system integration. Later connect to the engine executor without increasing simultaneous job budgets and without blocking frame-critical teardown.
- Stage-17 test authored; full checkout compiler, CTest and hardware integration not yet run. Update skill and handoff every subsequent stage.

## Stage 18 — versioned SDF authoring and immutable async volume snapshots

Read `engine/world/TerraWeaveCavernsVolumeSnapshot.h`, `engine/world/TerraWeaveCavernsSnapshotWorkers.h`, `tests/terraweave_caverns_snapshot_workers_test.cpp` and `docs/agent-handoffs/caverns-stage18-2026-11-23.md`.

- `CaveVolumeSnapshot { generatorVersion, sourceRevision, Settings, entrances, stamps }` is the **immutable authored world generation source**. Before handing it to workers, invoke `canonicalizeCaveSnapshot` on the owner thread; it validates settings, max 16,384 stamps and 4,096 entrances, stable stamp/entrance IDs, positive radii, and sorts by ID.
- `CaveSphereStamp` supports `Carve` (union positive void density) and `Fill` (subtraction; fill wins even where carves overlap). Both are deterministic integer-centimetre operations. `caveSnapshotFieldCm` is the unified base-cave + entrance + author-stamp field, and `generateCaveSnapshotMesh` uses it through the existing marching-tetrahedra field callback. This is an approximate signed density field, **not a true exact Euclidean SDF**.
- Persist ALL authored input parameters, stable stamp IDs, entrance IDs, generator version and sourceRevision in the game save. Stage 18 only defines memory data and sampling; binary/JSON persistence, undo/redo, collaboration and editor UI are NOT implemented.
- `CaveSnapshotRegenerator(maxWorkers)` takes `shared_ptr<const CaveVolumeSnapshot>` plus `CaveSeamPublicationGate`; only launches jobs whose `CaveRebuildRequest.sourceRevision` matches snapshot revision. Workers use captured immutable snapshot, never mutable ECS or GPU. Poll completed meshes on owner thread; they are accepted through gate and re-audited before any scene publication. This is `std::async` bounded workers, not yet the existing PHYSIS/animation pool.
- Every adjacent chunk must use the SAME canonical snapshot covering the influence halo of every entrance/stamp. Use a whole-world/global catalogue or deterministic region queries with stable ordering and sufficient overlap. Never clip author stamp lists at chunk boundaries without halo.
- The **old Stage17 `CaveAsyncRegenerator` handles base caves only**. Use this stage-18 worker when generated entrances and authored stamps are enabled. Revisions must be bumped whenever ANY field input changes; reject stale results and regenerate dependent neighbors.
- Rendering / STRATA / Box3D / NavMesh remain separate publication owners. No worker performs RHI, scene updates or collision mutation. Add CMake tests and test on real hardware before claiming full integration.

## Stage 19 — native Terrain editor SDF brush, MapDoc save and Undo/Redo (2026-11-23)

Read `engine/world/TerraWeaveCavernsArchive.h`, `apps/editor/core/TerraWeaveCavernsOps.h`, `apps/editor/windows/TerrainWindow.cpp`, `tests/terraweave_caverns_editor_test.cpp` and `docs/agent-handoffs/caverns-stage19-2026-11-23.md`.

- Map persistence contract: `MapDoc::extra["terraweaveCaverns"]` (serialized through the normal MapDoc route), schema `version=1`, `generatorVersion`, `sourceRevision`, full cave `settings`, `entrances`, and ordered `stamps`. Use `caveSnapshotToJson(snapshot)` and `caveSnapshotFromJson(json)`; both canonicalize/validate the SDF authoring data. Persist deterministic input data, never rendered mesh or GPU buffers. This is a map-format addition; runtime loader still needs an explicit bridge.
- `registerTerraWeaveCavernsOps(ws)` is invoked by existing `registerTerraWeaveOps(ws)`. Shared native-editor/MCP/HTML5 operation: `Workspace::call("terraweave_caverns_paint", args)`. `mode=carve|fill` requires integer `xCm,yCm,zCm` and optional `radiusCm` (default 400); `mode=erase` requires stable `stampId`; `mode=get` returns `snapshot`. Responses include `stampCount,revision` and the created/removed `stampId` where applicable.
- All mutation validation occurs before `map.beginEdit()` and `map.touched()`. `withinStroke=true` means the GUI already opened an undo stroke; otherwise each operation creates one undo step. Invalid edits leave the document unchanged. Stamp IDs monotonically exceed the current source revision or any resident stamp ID to avoid routine ID reuse after removal.
- The native `TerrainWindow` Caverns panel offers Carve/Fill, adjustable radius, depth offset below the scene ground-projection plane and mouse dabs. Shift reverses the action. The "stamp at camera center" action allows setting a 3D position from the camera target with the same depth offset.
- This currently uses the existing `Editor::groundAt` hit on the scene plane for X/Z; Y is a **configured depth**, NOT an actual 3D raycast into cave wall triangles. There is no live SDF preview, hover gizmo or cave-wall selection yet. Do not describe this as full 3D ray-picking.
- Map changes emit `Change::Map` and update sourceRevision, but no runtime chunk scheduler or STRATA/Box3D/NavMesh resident is directly connected. The host must consume saved state, canonicalize it, invalidate affected chunk+halo, and rebuild all derived geometry and physics at a real owner barrier. Stage18 snapshot worker can consume these saved inputs after the host parses them.
- Tests authored in `tests/terraweave_caverns_editor_test.cpp`; full checkout compilation, CTest, GUI mouse tests and save/replay runtime integration are still NOT RUN in this turn.

## Stage 20 — cave surface SDF raycast and 3D editor brush target

Read `engine/world/TerraWeaveCavernsRaycast.h`, `tests/terraweave_caverns_raycast_test.cpp`, `apps/editor/windows/TerrainWindow.cpp`, and `docs/agent-handoffs/caverns-stage20-2026-11-23.md`.

- `CaveRayCm{origin,dxQ20,dyQ20,dzQ20}` defines a signed centimetre origin and normalized Q20 ray direction. `raycastCaveSurface(snapshot,ray,maxDistanceCm,stepCm)` searches for a density sign change and bisection-refines to integer-centimetre distance. `CaveRayHit` yields signed world cm position and whether the ray enters void. This is bounded CPU sign-change sampling, not a triangle-accurate GPU depth raycast. Features thinner than stepCm may be missed; reduce step / validate sample density when precision matters.
- TerrainWindow Caverns brush uses `Camera::screenRay` to sample the actual saved authored volume, allowing walls/ceilings where the volume is represented by SDF. It does not need `Editor::groundAt`. The cursor UI reports a world XYZ hit when available and refuses drawing without a hit. It is NOT yet a drawn 3D sphere gizmo and does not raycast STRATA's final tessellation.
- `dirtyBoundsForStamp(stamp,haloCm)` returns conservative half-open 3D world-cm AABB (100cm default gradient halo). `dirtyCaveChunks(bounds,resolution,spacingCm,maxChunks)` converts it to globally anchored signed XYZ keys with an explicit capacity guard. Editing op `terraweave_caverns_paint` returns `dirtyBounds` for create/erase as metadata, but the editor/runtime has NOT yet subscribed to it to rebuild live GeometryScene/STRATA/Box3D/NavMesh residents.
- Editor viewport uses existing MapDoc `terraweaveCaverns` snapshot through `caveSnapshotFromJson`, so changes are persisted as before. For frequent hover, cache canonical parsed snapshots by `sourceRevision`; current implementation parses every hover and is a correctness-first prototype.
- This raycast sees the canonical signed field, NOT an already-authored polygon mesh's exact visual triangles. A thin or unexpectedly nonmonotone field can produce sampling errors. Avoid misrepresenting it as exact geometry picking.
- Stage20 tests are source only; no verified full native editor build or GPU demo. Continue skill and handoff for each stage.
