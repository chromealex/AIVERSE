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

## Stage 21 — wireframe 3D brush preview, cached picking and dirty revision queue

Read `apps/editor/Editor.h`, `apps/editor/Editor.cpp`, `apps/editor/windows/TerrainWindow.cpp`, `engine/world/TerraWeaveCavernsDirtyQueue.h`, `tests/terraweave_caverns_dirty_queue_test.cpp`, and `docs/agent-handoffs/caverns-stage21-2026-11-23.md`.

- `Editor::cavernsBrushPreview` carries UI-thread visual-only brush centre XYZ in metres, radius in metres, and Carve/Fill color. `Editor::drawEditOverlays3D` now draws THREE 48-segment orthogonal circles using existing `r3d_.line` to visualize the sphere in 3D. No new renderer/RHI/shader and no ECS changes.
- The Caverns tool in `TerrainWindow` refreshes the preview based on existing `sceneCam.screenRay` and SDF hit; hides it if scene/tool is inactive or ray misses. On-hover `CaveVolumeSnapshot` is cached against the complete `MapDoc::extra["terraweaveCaverns"]` JSON value, so Undo/Redo and edits reparse it but unchanged hovers do not. Comparing entire JSON is still O(authoring state size); later improve with document identity/revision key plus undo generation.
- `CaveDirtyQueue` receives affected `CaveDirtyBounds` from an authored stamp with `dirtyBoundsForStamp`, maps to 3D chunk keys via `dirtyCaveChunks`, coalesces repeated edits and uses monotonically changing serials. `pending(budget)` provides stable bounded requests; `acknowledge(CaveDirtyChunk)` refuses old sourceRevision/serial so a completed stale job cannot clear more recent edits.
- This is a reusable **notification queue**, not a live listener. The editor `terraweave_caverns_paint` currently returns `dirtyBounds`; a runtime host must explicitly feed them into a `CaveDirtyQueue`, map sourceRevision to a canonical immutable snapshot, ask Stage18 workers to rebuild, certify Stage16 seams and publish to GeometryScene/STRATA, SpatialWorld/Box3D and NavMesh on the owner-thread barrier.
- Brush preview is a 3D wireframe sphere, not depth-tested cave material/transparent voxel visualization. SDF ray sampling may miss very thin features; it is not exact mesh triangle-picking.
- Stage21 code/tests have NOT been compiled/linked or run in full AIVERSE and no GPU screenshot was made. Run native CMake/CTest and mouse-hover/brush visual acceptance before main merge.

## Stage 22 — runtime bridge for authored dirty events and owner-owned publication

Read `engine/world/TerraWeaveCavernsEditRuntime.h`, `tests/terraweave_caverns_edit_runtime_test.cpp` and `docs/agent-handoffs/caverns-stage22-2026-11-23.md`.

- `CaveEditRuntimeBridge(resolution,spacingCm,maxQueued,maxWorkers)` bridges signed 3D dirty chunks to immutable, bounded `std::async` CPU mesh regeneration and subsequent Stage16 `CaveSeamPublicationGate` certification. It does **not** directly own or touch `gfx::Device`, STRATA, Box3D or NavMesh. All public calls are **scene-owner-thread only**; workers read `shared_ptr<const CaveVolumeSnapshot>`.
- `invalidate(bounds,snapshot)` requires a current canonical snapshot and rejects regressions in source revision. Consecutive edits supersede all uncommitted dirty requests to the newer snapshot, including previously dirty chunks outside the latest brush bounds, so stale async jobs cannot permanently block publication. Undo/Redo must supply the correct resulting saved snapshot and invalidate BOTH previous and new affected regions or an explicit larger region.
- `consumeEditorResult(operationResult,snapshot)` is an explicit adapter for the `Workspace::call("terraweave_caverns_paint",...)` response. It verifies `ok`, existence of `dirtyBounds`, and matching `revision`, then invokes `invalidate`. The editor must forward this result from the owner host; there is **no global event subscription automatically installed** by defining this class.
- `tick(maxJobs)` polls completed CPU futures, refuses stale serials/revisions, and schedules only bounded, immutable snapshot mesh jobs. `CaveEditTick` tracks dispatched, finished, stale, failed, pending, ready, blocked. Completed CPU jobs go into `CavePreparedEdit` and remain dirty until a confirmed owner commit.
- `registerResident(CaveCertifiedChunk)` provides previously resident neighboring visual meshes for seam certification. `publishReady(hostCommit)` checks ALL pending dirty chunks are prepared, no worker remains, stages a copied prospective seam gate, rejects unmatched borders, then calls the host callback on the owner thread with the prepared candidates. It only acknowledges dirty markers and adopts the prospective gate after callback returns `true`. A callback returning `false` or throwing retains pending work. The host must stage/publish **the entire accepted set together**, including any unchanged neighbors whose visibility must be coordinated; the bridge cannot make GPU/Box3D/NavMesh visibility atomic.
- Every callback must be all-or-none and must report true only after actual safe owner commit. **Existing Stage11 CaveBatchGeometryResident::publish is NOT a GPU-fenced incremental source insertion**, and its `GeometryScene::shutdown` path can stall; do not call it per brush dab without owner-side GPU idle/lifetime proof.
- Mixed-LOD or mismatched equal-LOD seams fail closed. The bridge does not currently convert the Stage16 `gate.requests()` fallback into new seam worker jobs; use Stage18 `CaveSnapshotRegenerator` for LOD fallback with the same exact versioned world snapshot before reattempting publication.
- Stage22 test source checks superseding an in-flight edit, host refusal retaining dirty state, and an accepted host commit clearing it. No complete C++20 compile, CTest, GPU/STRATA, or Box3D/NavMesh end-to-end test has been run for this stage.

## Stage 23 — Editor-owned map-change observation and GeometryScene CPU preparation

Read `apps/editor/Editor.h`, `apps/editor/Editor.cpp`, `apps/editor/windows/TerrainWindow.cpp`, `engine/render/TerraWeaveCavernsEditPresentation.h`, `tests/terraweave_caverns_edit_presentation_test.cpp` and `docs/agent-handoffs/caverns-stage23-2026-11-23.md`.

- `Editor::updateCavernsAuthoring()` runs after window updates in the native editor owner loop. An internal `CavernsEditHost` observes the persisted `MapDoc::extra["terraweaveCaverns"]` JSON for changes, including native Undo/Redo and editor/MCP commands, and keeps one `CaveEditRuntimeBridge` per project. It computes a conservative union of all old and new stamp bounds and entrance shaft bounds. When any volume changes it captures the newly canonicalized `CaveVolumeSnapshot` and sends it to `bridge.invalidate`. This is broad correctness-first invalidation, not an efficient spatial diff.
- The MapDoc saved `sourceRevision` can decrease after Undo; the editor host assigns a **separate monotonic in-memory epoch** to each observed snapshot before dispatching workers. Do not persist that runtime epoch as the saved revision. It prevents async jobs from an undone state claiming to represent the new state.
- Changing generator settings requires a full-world invalidation. This stage **fails closed with an explicit diagnostic** instead of pretending a stamp-AABB update covers the procedural field. A dedicated loaded-world reload must handle that case. Similarly, world transition / project reset destroys the old CPU bridge, waiting for outstanding tasks.
- The TerrainWindow Caverns panel now exposes `Editor::cavernsBuildStats`: CPU pending/prepared/inFlight and diagnostic, and explicitly says the GPU, STRATA and collision residents are NOT yet being updated. Do not mark brush updates visible merely because CPU preparation finished.
- `prepareCaveEditPresentation(candidates,generatorVersion,CaveBatchSpec)` converts a verified owner-commit candidate list into actual Stage11 `CaveBatchPrepared` CPU render models and one GeometryScene JSON manifest. Rejects source-revision mismatch and duplicate keys. This is a staging API for a future real graphics owner; it does NOT invoke `GeometryScene::loadPrepared`, GPU upload, physical collider replacement, NavMesh or fence retirement.
- A real future host must call `bridge.publishReady` on the editor/graphics owner thread, perform **all-owner** staged graphics/physics/nav publication with verified GPU lifetimes, and return true only after the actual commit. It must never automatically return true after CPU model preparation.
- Source-only test `tests/terraweave_caverns_edit_presentation_test.cpp` checks deterministic negative signed chunk ordering, staging model counts, STRATA manifest flag, rejects stale/malformed versions and duplicates. Not yet compiled in the complete AIVERSE checkout.
- Full native editor CMake/CTest, Studio web, GPU and Box3D/NavMesh QA remain outstanding, and no main merge is approved. Update a dated handoff and this skill for each stage.

## Stage 24 — staged GeometryScene/STRATA GPU ownership and safe retirement

Read `engine/render/TerraWeaveCavernsGpuSwap.h`,
`engine/render/TerraWeaveCavernsSwapPolicy.h`,
`tests/terraweave_caverns_swap_policy_test.cpp` and
`docs/agent-handoffs/caverns-stage24-2026-11-23.md`.

- `CaveGeometrySwapOwner` (graphics-owner thread only) owns at most eight `GeometryScene` GPU resource containers split into active, staged and retired. `stage(gfx::Device&,CaveBatchPrepared,renderRevision,strataEnabled,vertexBudget)` calls the REAL `GeometryScene::loadPrepared`; old active geometry remains visible during upload. A partial-stage exception retains the failed GPU owner for later safe teardown.
- `activate(hostCommit)` switches the draw owner **only when the caller proves** full graphics/collision/navmesh compatibility and successful owner-barrier visibility. An unsuccessful callback leaves the old active draw. `draw(renderer)` submits **one** active GeometryScene, using its existing STRATA/fallback logic. Never also render those chunks through the older per-chunk path.
- `cancelStage()` moves a discarded GPU source to a retained retirement list. `collectAfterGpuIdle(device)` requires real `device.waitForResourceIdle()` success and subsequently `GeometryScene::shutdown` for each retired owner. This is intentionally conservative; **not yet nonblocking GPU-fenced retirement**. `shutdownAfterGpuIdle(device)` is required before owner destruction. Do not drop a failed pending owner merely because a CPU frame ended.
- `caveCanStageScene(activeRevision,stagedRevision,proposedRevision,liveOwnerCount,maxOwners)` and `caveCanActivateScene(activeRevision,stagedRevision)` are pure testable policy guards. They reject stale source revisions and retain bounded GPU owner count. The same policy is called by the real swap owner.
- This is **real GPU staging API code** but the native Editor has NOT yet been connected to `CaveGeometrySwapOwner`. Stage23 editor still prepares CPU meshes only, without a physics owner commit. Do NOT mark `visualCommitConnected` true or acknowledge a dirty region until real Render/Box3D/SpatialWorld/NavMesh ownership barriers succeed.
- On resource exhaustion, failed idle barriers, or incompatible mixed-LOD seam certification, fail closed. STRATA still decides whether its GPU cluster route is supported; requesting STRATA does not prove it selected.
- **Verification:** a local C++20 `-Wall -Wextra -Werror` compile + execution of a faithful copy of `tests/terraweave_caverns_swap_policy_test.cpp` passed (console: `caverns stage24 swap policy PASS`). This validates ONLY the pure policy; full `GeometryScene` build, GPU upload, STRATA selection, editor, Box3D/NavMesh and GPU-idle behavior remain NOT RUN.

## Stage 25 — graphics/physics/navigation owner-barrier adapter (2026-11-23)

Read `engine/world/TerraWeaveCavernsOwnerBarrier.h`,
`engine/render/TerraWeaveCavernsEditedSceneTransaction.h`,
`tests/terraweave_caverns_owner_barrier_test.cpp`, and
`docs/agent-handoffs/caverns-stage25-2026-11-23.md`.

- `CaveOwnerBarrier` is **pure bookkeeping** for a single versioned `CaveOwnerBatchTicket{serial,sourceRevision,renderRevision,chunkCount}`. `begin` invalidates old generations, `prepared(ticket,owner)` acknowledges exactly one of Geometry/Spatial/Box3D/Navigation, and `ready` is true only after all FOUR owner bits are marked. Unknown and composite owner flags are rejected. `confirm(ticket,externalAtomicCommit)` is successful only when the real host already performed an all-owner changeover; false aborts the ticket. This coordinator is not a rollback mechanism.
- `CaveEditedSceneTransaction::prepareVisual(device,candidates,generatorVersion,batchRevision,strataEnabled,options)` uses `prepareCaveEditPresentation` and real Stage24 `CaveGeometrySwapOwner::stage` to GPU-upload an unpublished candidate `GeometryScene`, then opens a batch ticket and marks the Geometry owner prepared. `prepared(ticket,part)` accepts ONLY Spatial, Box3D and Navigation, based on actual host staging performed outside this helper.
- `commit(ticket,externalAtomicCommit)` requires all owners ready; only after callback confirms its external changeover does it `activate` the prepared GeometryScene. `draw(renderer)` routes through ONE active scene (including actual STRATA selection/fallback), never both a cave resident and the batch draw. `cancel`, `collectAfterGpuIdle` and `shutdownAfterGpuIdle` keep explicit stage/retirement semantics.
- **Important:** This is a host integration API, not proof of real atomic cross-physics rendering: external ownership still needs a two-phase, rollback-safe prepare/commit/release implementation. Stage25 does not create, replace or revert Box3D bodies, SpatialWorld triangles or native NavMesh, and does not automatically wire the editor Scene render path. Do not fake acknowledgment by blindly marking these owners ready.
- Existing `CavernsBox3DPublisher::publish` swaps a body immediately, so it is NOT safe to call it as the staging callback until refactored. Existing GeometryScene/STRATA final retirement still needs idle proof and can stall. Do not promise nonblocking GPU fence retirement.
- **Actual tests:** isolated self-contained C++20 policy test source compiled and executed with `-Wall -Wextra -Werror` and printed `caverns stage25 owner barrier PASS`. Composite-mask rejection was added after that local run and remains unverified in the complete checkout. Native GeometryScene/Editor/Box3D/NavMesh and CMake/CTest were NOT RUN.

## Stage 26 — prepare / commit / abort shadow owners

Read `engine/world/TerraWeaveCavernsShadowOwners.h`,
`engine/world/TerraWeaveCavernsSpatialShadow.h`,
`engine/world/TerraWeaveCavernsBox3DShadow.h`,
`tests/terraweave_caverns_shadow_owners_test.cpp` and
`docs/agent-handoffs/caverns-stage26-2026-11-23.md`.

- `prepareCaveCollision(CutNativePayload,tileMetres,userData)` validates a **TerraWeave entrance-cut surface** triangle list and creates matching CPU `world::Triangle` and `physics::BodyDesc` representations. It does NOT generate native collision for all underground cave volume surfaces; do not substitute its surface-cut payload for full cave-wall collision.
- `CaveNavShadow` can `prepare(nav::Source,nav::Config,revision,error)` by building an isolated `nav::Mesh` while the old active nav mesh remains untouched. `commit(revision)` swaps the staged mesh into the active pointer and `abort(revision)` discards an unpublished staged mesh. Call commit only with no concurrent readers of the previous nav mesh. An actual cave navigation `nav::Source` must sample the same immutable versioned authored density; Stage7 `CaveNavSource` still samples base+entrances and is NOT yet full authored-stamp aware.
- `CaveSpatialShadow` stages disjoint collider IDs/triangles without mutating `world::SpatialWorld`. At the exclusive owner barrier, `commit(world,revision)` revalidates IDs, inserts all new triangles, rolls them back on insertion failure, and only after successful insert removes the previous collider range. `abort(revision)` does not affect live colliders. ID reservations must be globally coordinated across other publishers, since a local preparation check alone does not reserve IDs.
- `CaveBox3DShadow` stages a validated `physics::BodyDesc` **without creating a live body**. `commit(physics::Scene,revision)` at a between-step owner barrier creates a new body and destroys the previous one. If creation fails it keeps old state, but a short overlap between new and old active bodies occurs within commit; this is **not** a true invisible-body preparation or globally reversible atomic transaction. There is no native Box3D shadow world or deferred activation API in this change.
- Crucial: Stage25 `CaveOwnerBarrier` remains a preparation **protocol**, not a true cross-owner atomic swap. NEVER mark SpatialWorld/Box3D/NavMesh prepared merely because the CPU payload exists if the host expects physical resource staging and immediate visibility parity. All commits must be serialized between physics steps and render passes; arbitrary order with potential failure is not equivalent to rollback.
- Stage26 test source covers native `nav::Mesh` shadow preparation/commit/abort, SpatialWorld collision staging/commit/abort and Box3D descriptor staging/abort. No actual Box3D Scene commit, full engine compile or CTest execution occurred. Tests are NOT RUN.
- Next: implement the full underground volumetric collision payload; authorize a game-wide reader-pause and prepare/commit policy, or extend the physics API to accept inactive bodies so the whole set can be swapped without transient overlapping collision.

## Stage 27 — authoritative full cave-wall collision + authored SDF NavMesh source (2026-11-23)

Read `engine/world/TerraWeaveCavernsVolumeCollision.h`, `engine/world/TerraWeaveCavernsAuthoredNav.h`, `engine/world/TerraWeaveCavernsMultiNav.h`, `tests/terraweave_caverns_volume_collision_nav_test.cpp` and `docs/agent-handoffs/caverns-stage27-2026-11-23.md`.

- `prepareCaveVolumeCollision(CaveMesh,resolution,spacingCm,sourceRevision,userData,limits)` uses ONE cave surface's original indexed triangles to prepare both absolute-world-centimetre `world::Triangle` collision and a local-metre `physics::BodyDesc` static mesh. It supports arbitrary triangle orientation — walls, floor, ceilings, overhangs — and preserves the mesh's supplied winding. The Box3D body origin is the signed 3D chunk origin, not the TerraWeave 2D cut-terrain origin. Input validated for indices, nonfinite/out-of-chunk coordinates, triangle degeneracy via `SpatialWorld::validTriangle`, Box3D mesh budget and bounded physics world coordinates.
- `prepareCaveAuthoritativeTile(snapshot,key,resolution,spacingCm,userData,limits)` generates the visual-authoritative surface with `generateCaveSnapshotMesh` and builds the collision payload from **exactly that mesh**, so edited Carve/Fill and entrances affect both. Do NOT pass separately simplified STRATA LOD source to physics. `CaveAuthoritativeTile` returns a CPU mesh and CPU collision staging; it doesn't touch a GPU device, a native Box3D Scene, or a SpatialWorld instance.
- An empty/no-boundary cave chunk currently throws in collision preparation; the host must handle this as a zero-collider retirement separately, NOT as a production failure for all-solid or all-air chunks. Previous Stage26 `prepareCaveCollision` remains specifically for a **cut surface**, while Stage27 produces cave volume walls/floors/ceilings.
- `CaveNavSource` in `TerraWeaveCavernsMultiNav.h` gained an optional `Density` callback; omitted callback retains original base+entrances behavior. `makeCaveAuthoredNavSource(shared_ptr<const CaveVolumeSnapshot>,CaveNavConfig,heightAtCm)` captures the same immutable authored snapshot and invokes `caveSnapshotFieldCm` for cave walkable span sampling and links. Build a separate `nav::Mesh` through `CaveNavShadow::prepare`, and publish only after matching collision and visual revisions at an exclusive owner barrier.
- Nav span detection is still vertically sampled and approximate; it does NOT solve arbitrary ceiling clearance, suspended floors, complex non-manifold geometry, or agent-specific clearance/physics. Do not mark navigation acceptance without actual gameplay tests.
- The CPU collision payload is directly suitable for Stage26 `CaveSpatialShadow::prepare` and `CaveBox3DShadow::prepare`; these owners still have visibility/atomicity limitations. Box3D body creation instantly enters physics and may briefly overlap the old body. Stage25 owner barrier is bookkeeping rather than an actual rollback.
- STRATA remains a presentation geometry LOD owner; gameplay physics and nav are based on the versioned authored snapshot. Nonblocking GPU retirement, native Scene auto-commit and rollback-safe multi-owner swaps are still not complete.
- Stage27 regression source includes a floor/wall/ceiling mesh, signed-origin conversion, deduplication, invalid geometry and budget refusal, plus Carve/Fill native nav span differences. **Test source has not been compiled/executed in a full AIVERSE checkout.** Run CMake/CTest and Box3D gameplay acceptance before marking pass.

## Stage 28 — fully-filled cave tile retirement, physics co-staging (2026-11-23)

Read `engine/world/TerraWeaveCavernsVolumeCollision.h`,
`engine/world/TerraWeaveCavernsSpatialShadow.h`,
`engine/world/TerraWeaveCavernsBox3DShadow.h`,
`engine/world/TerraWeaveCavernsVolumePhysicsStage.h`,
`engine/render/TerraWeaveCavernsGpuSwap.h`,
`engine/render/TerraWeaveCavernsEditedSceneTransaction.h`,
`tests/terraweave_caverns_empty_tile_test.cpp`, and
`docs/agent-handoffs/caverns-stage28-2026-11-23.md`.

- `prepareCaveVolumeCollision` now accepts a **fully empty** indexed `CaveMesh` (both vertices and indices empty) with valid version/physical tile parameters, returning a zero-triangle `CaveCollisionPrepared` and zero-body-geometry `BodyDesc`. Half-empty sources (vertices without indices) are rejected. This is a retirement request, not a failed generation.
- `CaveSpatialShadow::prepare` accepts empty triangles with colliderId 0; `commit` then removes the previous dedicated collision-ID range and records the new revision/count=0. It still requires exclusive owner access; partial insertion rollback remains for nonempty updates.
- `CaveBox3DShadow::prepare` accepts an empty `BodyDesc`; `commit` destroys the old body and records the new revision without creating a fake new body. This needs a real exclusive non-simulating Box3D owner barrier; **global multi-owner atomicity still not guaranteed**.
- `CaveGeometrySwapOwner::stageEmpty(renderRevision)` stages removal of the current visual GeometryScene without uploading a dummy scene. `activate(hostCommit)` draws nothing only after the external owner callback confirms its global visibility barrier. Old GPU scene remains in bounded retirement until a real idle/shutdown proof. `CaveEditedSceneTransaction::prepareVisual` routes all-empty prepared batches through `stageEmpty` automatically.
- `CaveVolumePhysicsStage::prepare(surface,resolution,spacing,sourceRevision,firstColliderId,spatial,userData)` derives authoritative collision once from the actual edited CaveMesh and prepares both SpatialWorld and Box3D CPU shadow states, with rollback of staged CPU data on partial preparation failure. `prepared(revision)` and `abort(revision)` manage the paired stage. `spatialOwner()` and `boxOwner()` expose the existing owner commit primitives; **do not sequentially call them as if this guaranteed an atomic four-owner commit**.
- `tests/terraweave_caverns_empty_tile_test.cpp` covers physical zero-triangle payload, invalid half-empty input, SpatialWorld retirement from one active collider, Box3D empty descriptor stage/abort, and paired prepare/abort for nonempty and empty versions. It requires the normal AIVERSE Box3D/SpatialWorld linked test target; it has NOT RUN in the complete engine checkout.
- **Major remaining gap:** when a batch has *some* empty and *some* nonempty chunks, their model source list omits only the empties, but the host must also retire their previous per-shard visual instances. Never use the all-empty path to accidentally clear unrelated chunks, and never silently retain deleted cave geometry in a combined resident scene.
- NavMesh still needs an authored-SDF rebuild for a now-filled chunk, prepared via Stage27 `makeCaveAuthoredNavSource` and Stage26 `CaveNavShadow`. This is a host responsibility, not automatically invoked by Stage28.
- Full C++ compiler/CMake/CTest/GPU/Box3D runtime and native editor tests are NOT RUN this stage. Update skill and independent dated handoff on every subsequent implementation stage.

## Stage 29 — full mixed-shard snapshots + matching authored NavMesh staging (2026-11-23)

Read `engine/render/TerraWeaveCavernsShardCatalog.h`,
`engine/world/TerraWeaveCavernsAuthoredNavStage.h`,
`tests/terraweave_caverns_mixed_shard_test.cpp`,
`tests/terraweave_caverns_authored_nav_stage_test.cpp`,
and `docs/agent-handoffs/caverns-stage29-2026-11-23.md`.

- `CaveShardCatalog` stores authoritative **committed** `CaveBatchChunk` entries keyed by signed `ChunkKey3`. An entry may contain an empty mesh, representing an authored zero-geometry tombstone; an erased entry represents unloaded source. `preview(shard, edits, batchRevision)` computes an immutable **complete** shard snapshot including unmodified resident chunks and the replacements, rejects duplicate or stale key edits and preserves deterministic order. It does NOT mutate committed sources. `adoptSnapshot` updates the authoritative catalog only after a real external render+physics+navigation commit; this catalog is not the GPU resident itself.
- `prepareFullCaveShardPresentation(snapshot,options)` converts the whole shard (including unchanged models) to Stage11 `CaveBatchPrepared` while excluding empty source instances. If every source is empty, it returns an empty prepared payload and the GPU owner must call `CaveGeometrySwapOwner::stageEmpty`; it MUST NOT upload a dummy mesh. If only some sources are empty, the unchanged ones MUST still appear in the new GeometryScene. This is an O(whole-shard) reload, NOT incremental STRATA source insertion, and requires stage24 GPU-owner lifetime handling.
- `CaveAuthoredNavStage::prepare(shared_ptr<const CaveVolumeSnapshot>, CaveNavConfig,terrainHeight,error)` builds a **shadow native** `nav::Mesh` using the exact author snapshot's cave density, including entrances, Carve/Fill. `commit(sourceRevision)` replaces the published nav pointer only with a matching staged snapshot; `abort(revision)` leaves the old active mesh unchanged. Nav source sampling must be version-consistent with the exact physics and visual candidate; stage under a paused-reader barrier.
- Fixed `makeCaveAuthoredNavSource` argument-evaluation order: copy `snapshot->settings` and entrances before moving the shared_ptr into the callback. Never read from a potentially moved snapshot; C++ argument evaluation order is not guaranteed.
- **No real unified four-owner atomic commit:** the catalog and nav shadow prepare changes, but stage26 Box3D creates live bodies immediately on its commit. An exclusive game-step/reader pause is required for every SpatialWorld/Box3D/nav switch, and a correct rollback/recovery contract is still missing. Stage25 barrier bookkeeping does NOT grant real atomicity. `CaveEditRuntimeBridge::publishReady` must only ACK after a verified external transaction.
- Stage29 has two regression SOURCE files; full AIVERSE C++ compiler/CMake/CTest, GPU and Box3D/NavMesh game acceptance have NOT run for this stage. Do not claim tests passed. Continue mandatory agent skill + separate handoff for every next stage.

## Stage 30 — complete-shard GPU host with bridge adapter (2026-11-23)

Read `engine/render/TerraWeaveCavernsShardGpuHost.h`,
`engine/render/TerraWeaveCavernsShardCatalog.h`,
`tests/terraweave_caverns_shard_gpu_host_test.cpp` and
`docs/agent-handoffs/caverns-stage30-2026-11-23.md`.

- `CaveShardGpuHost` manages a separate `CaveGeometrySwapOwner` and real `GeometryScene::loadPrepared` staging per `CaveShardKey`. It preserves active visual sources in unrelated shards while preparing a **complete** replacement of the target shard (all unchanged and edited source slots, including zero-geometry tombstones). Empty entire shards use `stageEmpty`, not dummy GPU models. Do not submit the same cave through older CutTerrainResident/GeometryScene adapters too.
- `stage(device,shard,edits,batchRenderRevision,spec)` prepares one shard and opens a pending immutable `CaveShardSnapshot` without updating catalog or active scene. `registerCommittedSource` is ONLY for initial bootstrapping of already committed authoritative source data, not as a shortcut to acknowledge editor edits.
- `stagePreparedEdits(device,vector<CavePreparedEdit>,generatorVersion,batchRenderRevision,outputShardKeys,spec)` converts Stage22 CPU completions to `CaveBatchChunk`, checks a common sourceRevision, groups by signed spatial shard and stages **every** impacted shard. If one stage is rejected, cancels previously staged candidates from that call; failed GPU owners are retained for later explicit idle recovery.
- `activate(shard,ownerCommit)` and `activateGroup(shards,ownerBatchCommit)` require the actual external graphics/SpatialWorld/Box3D/NavMesh owner to have staged and coordinated the full revision before returning true. `activateGroup` preflights all shard stages, invokes ONE external callback and then switches GPU shard owners under exclusive access; catalog entries change only afterward. Do NOT pass no-op true callbacks: **the class does not implement a physically atomic cross-owner transaction** and a partial failure after the host callback cannot be rolled back by this helper.
- `cancel(shard)` retains discarded GPU resource owners for later safe cleanup. `draw(renderer)` walks only committed shard owners. `collectAfterGpuIdle` and `shutdownAfterGpuIdle` require actual device idle proofs and are not per-brush-frame operations; the editor owns a matching graphics lifetime and must stop submissions before final shutdown.
- `groupCaveEditsByShard` in the catalog is deterministic, rejects duplicate keys/invalid source and is independent of GPU device. `CaveShardCatalog::adoptSnapshot` now enforces its configured shard capacity and validates sources before mutation, rather than assuming all shards are hard-coded to 4×4×4.
- **Remaining:** Stage23 native Editor still has only CPU worker preparation and DOES NOT yet instantiate/draw/activate this GPU host. The actual game physics owner is not connected to `activateGroup`; user-visible render after a brush is not proven. Box3D stage26 still mutates bodies upon commit. Native full CMake/CTest and GPU screenshot have not been run for Stage30; tests are authored only.

## Stage 31 — explicit visual-only GPU preview inside native Editor Scene (2026-11-23)

Read `apps/editor/Editor.cpp`, `apps/editor/Editor.h`, `apps/editor/windows/TerrainWindow.cpp`,
`engine/world/TerraWeaveCavernsEditRuntime.h`, `engine/render/TerraWeaveCavernsShardGpuHost.h`,
`tests/terraweave_caverns_editor_visual_preview_test.cpp` and `docs/agent-handoffs/caverns-stage31-2026-11-23.md`.

- Native `TerrainWindow` exposes an **opt-in and default-off** `Editor::cavernsVisualPreviewEnabled` checkbox labeled visual-only. Its help explicitly says Box3D, SpatialWorld and NavMesh are **NOT updated** and gameplay edits have NOT been published.
- `CaveEditRuntimeBridge::visualPreviewCandidates()` returns a complete, version-current, Stage16-seam-certified CPU candidate batch ONLY after all dirty chunks are prepared and no jobs remain. It does NOT call `dirty_.acknowledge`, change the Stage22 publication gate or signal that physics/nav are ready. A new author revision invalidates these candidates until rebuilt.
- The existing `Editor::CavernsEditHost` owns a persistent `CaveShardGpuHost` for native Scene visual preview. `stageVisualPreview(Editor&)` uses Stage30 `stagePreparedEdits(device, candidates, generatorVersion, visualRevision, ...)` outside `r3d_.beginScene`, and `activateVisualOnly` for **render-only** activation. It never invokes Stage22 `publishReady` or fakes an all-owner gameplay ACK. The preview defaults to `enableStrata=false` because this is the portable GeometryScene path; Stage24's true STRATA owner interface remains an opt-in GPU staging API, not proved in this editor path.
- `Editor::renderScene` draws committed cave preview sources only when the Scene is an editable flat world and preview is enabled. It calls `CaveShardGpuHost::draw(r3d_)` exactly once in the scene model submission phase, after `r3d_.beginScene`, without changing normal terrain drawing. Do not add a second cave draw path elsewhere.
- The GPU preview owner is explicitly retired using `shutdownAfterGpuIdle(device)` in Editor shutdown, and at project changes/close after a real GPU-idle proof; if retirement refuses, retain ownership and surface a diagnostic instead of dropping GPU owners. Never render old project cave visuals when `projectDir != ws().project.dir`.
- `CaveShardGpuHost::activateVisualOnly(shardKeys)` is distinct from `activateGroup(shards,externalGameOwnerCommit)`. It has a direct visual-only name and must not be used as proof of any Box3D/NavMesh or gameplay commit; it retains the real GPU lifecycle safeguards and authoritative per-shard catalog.
- Stage31 source-only regression test `tests/terraweave_caverns_editor_visual_preview_test.cpp` asserts Stage22 visual preview does not ACK dirty gameplay edits, and that later source revisions invalidate prior CPU candidate batches. No full native build, GPU live render, or editor screenshot was executed. **Do not claim STRATA GPU selection, Box3D gameplay or CTest passed.**
- Remaining production tasks: CMake/CTest compilation of all stages (high priority); lifecycle acceptance for device-loss and project-switch; real game-oriented all-owner atomic graphics+physics+navigation swap and GPU-fenced resource retirement; visual tests for mixed shard Fill/Undo and cross-shard seams. Continue mandatory dated handoff and skill every stage.

## Stage 32 — frame-paced Scene GPU preview, retry backpressure and diagnostics

Read `engine/render/TerraWeaveCavernsPreviewPacer.h`, `engine/render/TerraWeaveCavernsShardGpuHost.h`, `apps/editor/Editor.cpp`, `apps/editor/Editor.h`, `apps/editor/windows/TerrainWindow.cpp`, `tests/terraweave_caverns_preview_pacer_test.cpp` and `docs/agent-handoffs/caverns-stage32-2026-11-23.md`.

- `CavePreviewPacer(retryFrames=30)` is a self-contained C++20 policy: `reset(authorEpoch,groupCount)`, `mayStage(epoch,deviceFrameSerial)`, `staged(frame)`, `failed(frame)`, `ready(epoch)`, `remaining()`, `failures()`, `retryAfterFrame()`. Allows **at most one GPU shard upload per device frame** and delays reattempt of a rejected source by 30 frames. No timers or detached background tasks.
- `Editor::CavernsEditHost` now converts the fully seam-certified Stage22 CPU batch into `groupCaveEditsByShard` once per authoring epoch, stages **one complete GeometryScene shard per render frame**, retains all staged shards unseen until ALL are ready, then calls editor-only `CaveShardGpuHost::activateVisualOnly` once for the group. Staging errors defer retries without discarding current committed visual scene. When a new MapDoc author epoch arrives or user disables visual preview, cancel any unfinished candidate stages; GPU owners are retained for safe retirement. Existing active previews can remain hidden until opt-in is enabled again.
- `CaveShardGpuHost::stats()` reports `activeShards`, `pendingShards`, `retiredScenes`, `pendingGpuOwners` and `maxActiveRevision` from **actual** resident ownership rather than fabricated counters. The TerrainWindow shows active/staged/remaining/retired sources and current visual revision, failure count and retry frame. These are visual preview counters only; `visualCommitConnected` continues to refer to the missing gameplay all-owner commit.
- The preview continues to use `CaveBatchSpec.enableStrata=false` (portable GeometryScene fallback, no claim of STRATA cluster GPU selection). It does NOT call `CaveEditRuntimeBridge::publishReady`, does NOT ACK game dirty revisions, does NOT mutate Box3D/SpatialWorld/NavMesh and does not prove full synchronized gameplay state.
- The per-frame rule budgets **number of shard upload calls**, not GPU milliseconds/bytes or retirement workload. `GeometryScene::loadPrepared` may still be expensive for one large shard; STRATA retirement still may require a true idle barrier at project shutdown. Do not claim hitch-free asynchronous GPU streaming.
- **Actual validation:** local isolated C++20 policy reproduction compiled and executed with `-Wall -Wextra -Werror`, stdout `stage32 pacer PASS`. A matching GitHub test source also covers invalid retry settings. The full editor, CMake/CTest, GPU backend render and native physics integration remain NOT RUN. Do not infer GPU success from policy test.
- Continue explicit skills + dated handoff for every stage. Next: compile all AIVERSE native targets and Stages1–32 tests, fix real link/renderer API issues and run a native Scene/Metal test before further gameplay-commit claims.

## Stage 33 — CMake/CTest build verification and evidence (2026-11-23)

- Root `CMakeLists.txt` now registers `tests/caverns/CMakeLists.txt` under `AIVERSE_BUILD_TESTS AND NOT EMSCRIPTEN`. The initial portable test suite includes four dependency-light C++20 sources: `terraweave_caverns_mesh_test`, `terraweave_caverns_swap_policy_test`, `terraweave_caverns_owner_barrier_test` and `terraweave_caverns_preview_pacer_test`. Each has `aiverse_keep_test_asserts` so `assert` remains active in Release; CTest labels are `Caverns`, `TerraWeave`, `CPU`. This is **NOT a registration of every stage 1–32 integration test**. Some remaining tests depend on Box3D/GeometryScene/editor libraries and need native target/link requirements investigated.
- `tools/caverns-stage33-verify.sh` documents a reproducible native checkout verification: configure Debug with `AIVERSE_BUILD_TESTS=ON`, `AIVERSE_ENABLE_PHYSICS=ON`, editor on and template auto-fetch off; build the four smoke targets; run `ctest -L Caverns`; optionally build and test ALL targets using `AIVERSE_CAVERNS_FULL_BUILD=1`. It writes `provenance.txt`, `working-tree.txt`, `configure.log`, `build-smoke.log`, `ctest-smoke.log` and optional full build logs. If CMake/target link fails, the script exits nonzero and logs actual failures.
- Local container could not clone full `chromealex/AIVERSE` due DNS `Could not resolve host: github.com`. GitHub connector gave source reads and updates, but GitHub combined statuses/workflow run lists for stage32 SHA were empty. Do **NOT** report full AIVERSE build success.
- **Actual local PASS:** four smoke tests compiled and executed with CMake 3.31 in a **minimal reconstructed source tree** at `/mnt/data/caverns-stage33-smoke` (local copies of mesh + swap-policy + owner-barrier + preview-pacer sources/headers). `ctest --test-dir ... -L Caverns` returned `4/4 passed, 0 failed`. Two earlier single-program smoke checks also passed. This is evidence ONLY for the portable CPU contracts in the reconstructed harness; the exact GitHub checkout was NOT cloned or compiled.
- Next: run the actual native script inside an authorized full checkout on macOS, inspect real CMake errors, add dedicated build/link targets for Stage17–32 graphics/editor/physics regression sources, run STRATA/Box3D/SpatialWorld/NavMesh/Scene device integration and keep the mandatory dated handoff. Do not merge main without that evidence.


## Stage 34 — genre-independent authored-cavern GAME runtime edit host (2026-11-23)

Read `engine/world/TerraWeaveCavernsGameplayEditHost.h`, updated `engine/world/TerraWeaveCavernsEditRuntime.h`, and `docs/agent-handoffs/caverns-stage34-2026-11-23.md`.

- `CaveGameplayEditHost(resolution,spacingCm,shardConfig,dirtyBudget,workers)` connects **one authored source snapshot** to the existing Stage22 `CaveEditRuntimeBridge`, Stage30 `CaveShardGpuHost`, Stage28 per-chunk `CaveVolumePhysicsStage` and Stage29 `CaveAuthoredNavStage`. It is genre independent. The owning game/editor-runtime module must instantiate and retain it for the world lifetime; declaring the header alone does NOT install callbacks in an RTS/FPS game loop.
- `edited(bounds,shared_ptr<const CaveVolumeSnapshot>)` canonicalizes a defensive immutable copy, assigns a strictly increasing independent runtime epoch even if MapDoc Undo restores an older persisted sourceRevision, cancels previous *uncommitted* staging and invalidates the changed chunks. `consumeAuthoringOperation(WorkspaceResult, snapshot)` reads `terraweave_caverns_paint`'s `dirtyBounds` and validates the saved source revision, then forwards to `edited`. A non-Workspace runtime may call `edited` directly after its own SDF mutation or load.
- `pumpCpu(budget)` performs bounded background mesh generation; `advance(device,SpatialWorld,navCfg,terrainHeight,globalColliderIdAllocator,visualRevision,cpuBudget,geometrySpec)` is the intended scene-owner frame hook. It polls CPU jobs and, once all dirty meshes are ready and certified, prepares matching versioned GPU GeometryScene shards, unsimplified cave wall/floor/ceiling collisions for SpatialWorld and Box3D, and shadow native NavMesh from the same SDF. The methods stage resources but DO NOT mutate the published game world.
- `stageReady` can be called separately at a safe graphics-owner boundary; its `ColliderIdAllocator(key,triangleCount)` must reserve globally nonoverlapping dedicated collider IDs across every world resident. It stages zero-collider/empty-chunk retirement too. If any step fails, `cancelPending` aborts the staged CPU owners and cancels unpublished GPU shards while retaining old GPU scenes for safe retirement.
- `commitAtOwnerBarrier(commitOwners)` explicitly requires an actual exclusive sim/render/nav owner callback. It checks the staged version, source chunk identities and all prepared owners, then uses `CaveShardGpuHost::activateGroup` inside Stage22 `CaveEditRuntimeBridge::publishReady`. **Only a true successful external commit lets the bridge ACK dirty edits.** The callback gets `CaveGameplayCommitView{meshes, visualShards, mutablePhysicsOwners, navigationOwner, snapshot,visualRevision}` to publish matching resources. Returning false leaves dirty state pending and the old GPU scene visible. Do not call `commitAtOwnerBarrier([](...){return true;})`: that would falsely claim a gameplay commit.
- Stage26 `CaveBox3DShadow::commit` still creates immediately active bodies and destroys old ones. Sequentially calling SpatialWorld, Box3D and NavMesh commits is NOT a rollback-safe all-or-none transaction; Stage35 must implement the native engine exclusive barrier + fault recovery or inactive body support. This Stage34 host **automates preparation** but does not yet make a magic atomic physics swap or register itself in the native game loop.
- `registerCommittedMesh` can bootstrap known committed chunk CPU geometry and seam state (not GPU preload by itself); `draw(renderer)` draws only active shard GeometryScenes; `collectAfterGpuIdle` and `shutdownAfterGpuIdle` handle graphics-owner lifecycle. Full native GPU/physics should only be used by the owner host with device lifetime guarantees. Never draw a second overlapping cave resident path.
- **Feature-first request:** no new tests were run for Stage34. Full-checkout build/CTests, Box3D simulation, GPU capture and gameplay acceptance remain postponed. Source changes and API contracts should be documented in the per-stage handoff. Follow the requested next step of completing gameplay owner publication, streaming and resource retirement before final QA.

