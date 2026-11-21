# AIVERSE LENS Stage 44 — transactional Roads rebuild, weather and immutable camera packets (2026-11-20)

## Repository, branch and actual code commits

Repository: chromealex/AIVERSE.
Branch: feature/lens-motionweave-integration (NOT merged into main).
Parent Stage 43 handoff HEAD: f9fd70cf65ac5399f42271f72332c8b37c2d728f.
Previous handoff: docs/agent-handoffs/lens-motionweave-integration-stage43-owner-prepare-commit-fog-shores-2026-11-20.md.

- a68e13eaec573855fa0cabb49c9f1ba07e3a31b8 — RtsRoadView.h: separate candidate-only buildUnpublished API and strict upload error policy.
- 8a6d08294197c8297103c47751bb8d9ad2941a03 — RtsRoadView.cpp: stop retiring published meshes at beginning of preparation; fail hard on staged road chunk, painted image and decal/verge GPU failures.
- 4e2f5c115a54a9ebe06c3b5d521918a1dafe4dfc — RtsRoadView.cpp: new private candidate for geometry, road network surface, dynamic material images, decal meshes, props, weather and preview models; rollback on GPU failure and publish only after preparation.
- b2a21798f548bf943412b7fc7b924ca4dcb88526 — RtsRoadView.cpp: preserve existing road-kit texture ownership flags on commit; use valid ADL std::swap on RoadGeometry.
- 1ea8c0e26e12c7a3b7eb1cb1d5d4e58744e3537f — RtsRoadView.cpp: stage ghost preview before retiring its old model.
- 223096bd54e2088b05c60acc4995140a50a3133a — RtsRoadView.cpp: stage both weather film and puddle meshes before retiring any published weather mesh.
- de9dc99cc53aa85e0b90b3fd41f556a97f1017c1 — RtsRoadView.cpp: clear intentionally empty ghost presentation without leaving stale old model.
- 0a9b20a9e93b1cffd4b1d251ecb0daa42afadd2e — RtsRoadView.cpp: freeze road decals, film/puddles, texture references, camera projection and shadow-quality settings in per-camera deferred draw commands.
- 6908a5011c271bcb1e649a0eb0192be6e00cf905 — tests/tests.cpp: road/ground-surface rollback and weather upload failure tests.
- 4f821f1068dd40b20aa5fc2da20593136c636bd2 — RtsRoadView.cpp: explicit utility include for resource swaps.
- 1dbfe03561c256af917c57ab5773def8f2790bf7 — tests/tests.cpp: choose drivable asphalt road fixture for the wet film test.

## API and behavior implemented

1. RtsRoadView::rebuild() now creates a private RtsRoadView staging owner, copies the road types, network and look metadata, shares existing GPU kit image handles as BORROWED, and copies material/painted variant indices. It invokes buildUnpublished(), buildWeather(), and when enabled buildGhost() on that staging owner before touching published state.
2. The private buildUnpublished does not call release() or advance surfaceGeneration_ before successful publication. It prepares RoadGeometry, wheel-surface sample tiles, chunks, props, static decal+verge GPU buffers, film source data and road batches independently.
3. Staging image variants generated through paintedMaterial() are private owned resources. On failed upload, only the new candidate's Model, SpriteMesh and newly allocated image resources are cleaned; the original published Models, per-cell road surface, generation and deferred retirement queue are untouched. Candidate chunk GPU errors are NOT swallowed: strict staging rethrows.
4. When all uploads succeed, old published models and sprite buffers move into the existing NativePresentationRetirement queue. Prepared images/materials, paint variants, models, props, geometry, sampled wheel surface, decals, weather film/puddles, preview ghost and stats are published via swaps/moves. surfaceGeneration_ increments only at successful commit, and dirty_ clears only then.
5. Existing texture kit images survive the transition: copied candidate image handles are marked borrowed while staging so failed cleanup cannot destroy old kit textures, and their original ownership flags are restored on publication. Newly created painted GPU images remain owned by the active View.
6. Direct same-map weather changes use a separate two-phase implementation: both wet road film and puddle vertex buffers are prepared before either previous buffer is queued for retirement. Failed allocations preserve the existing weather mesh pair and wetStep_/puddleStep_ for retry.
7. The custom road decal draw callback no longer captures mutable RoadView fields with [this]. It captures the current decal/verge/film/puddle SpriteMesh handles, atlas texture refs, view projection and quality flags by value. It borrows the stable SpriteBatch pipeline owner, which must remain alive until host RHI completion.
8. Ghost preview model updates now stage a replacement and retain the old model on failure; an intentionally empty ghost removes the prior preview. Optional missing asset handling remains allowed outside strict GPU staging failures.
9. Only a genre-specific RtsRoadView adapter was modified. The generic OwnerPrepareCommit and FramePublicationEpoch from Stages 42–43 remain independent of game genre. Roads are now one locally atomic owner, but are NOT YET registered in a global prepare/commit transaction.

## Tests written (NOT executed)

- road_view_staged_rebuild_preserves_old_gpu_and_wheel_surface_on_failure: builds a real long asphalt road on a 2-node/1-edge RoadNetwork and fake RHI; changes its node coordinate, injects a buffer allocation failure, expects old chunk stats, pending-retirement count, wheel-surface data and surfaceGeneration unchanged; retries and checks generation advances only after successful publication.
- road_view_weather_upload_failure_keeps_previous_generation: creates an asphalt road, changes wetness/puddles, injects a GPU buffer failure, expects no old weather retirement and no road-generation change; retries and shuts down.
- Both use existing fogfaulttest::Device, Renderer3D and the existing tests/tests.cpp executable target.
- Prior Stage 37–43 tests remain in the branch.

## Actual validation performed

- Used connected GitHub to read RtsRoadView.h/.cpp, SpriteBatch.h/.cpp, RtsRoadNet.h, Renderer3D.h, existing tests and Stage 43 handoff.
- Pushed all Stage 44 implementation and test changes through GitHub.
- Re-fetched the files and statically checked old resources are retired only after the unpublished road build completes, old image ownership is restored on commit, wheel-surface swap and generation advancement occur after commit, weather buffers stage before retirement, draw callback captures per-camera resource handles by value, and test names exist.
- Compared against parent Stage 43 via GitHub: 11 source/test commits before this handoff, only RoadView.cpp, RoadView.h and tests/tests.cpp changed.
- **NOT RUN:** CMake configure, any native C++ compilation, aiverse_tests/CTest, Metal/Vulkan/GLES compilation, actual GPU rendering, parity, memory or timing benchmarks. Do not claim compile success or test PASS.

## Known limitations and risks

1. Roads now stage/publish as ONE owner inside rebuild(), but RtsView still calls Road/Fog/Shore and other owners sequentially during its primary draw. A late Fog or animation failure may occur after Roads already committed. True global all-owner atomicity remains incomplete.
2. This staging path rebuilds the entire road network and duplicates its temporary CPU/GPU geometry and variant material arrays. No fixed hard global upload budget or resumable multi-frame staging yet exists; evaluate busy worlds with thousands of road chunks.
3. The candidate's borrowed textures rely on the parent RtsRoadView kit remaining alive during staging, all pending camera submissions, and retirement. TextureCache hot-reload and device-loss generation should be audited before claiming cross-owner rollback.
4. GPU/backend failures after a Model upload has begun may need quarantine when the device cannot prove GPU completion; existing abortFailedModelUpload logic remains responsible. Real GPU fault injection and resource-lease behavior are NOT validated.
5. The exact C++ fixture behavior (initial drivable asphalt mesh, weather film allocation, GPU buffer order) is not confirmed; adjust the new regression test assertions during the consolidated native run.
6. Older RtsRoadView::init, ghost-only material/texture creation and direct setNetwork/setMap calls do not yet participate in the FramePublicationEpoch invalidation automatically unless RtsView's host adapter invalidates its epoch. Follow up with a shared owner mutation generation.
7. Draw callbacks still borrow a live SpriteBatch pipeline owner. The host must not destroy/reinitialize it until queued custom commands and GPU work complete; copying GPU handles alone does not extend RHI owner lifetime.
8. The generic Stage 43 two-phase OwnerPrepareCommit is not yet used directly by RoadView's owner-local rebuild. To join the full RtsView graph, externalize Road candidate ownership and separate prepare from no-throw commit.

## Exact next stage — Stage 45

Introduce an externally-owned road candidate / prepare and no-throw commit API, and register Roads, Fog and Shores with the generic OwnerPrepareCommit at one pre-render publication boundary. Add a multi-owner fault-injection test where all earlier owners successfully stage and the last fails, verifying the original entire presentation remains active. Then extend coordination to Terrain/Detail, GroundFX and Sprite/MotionWeave animation state, preserving the existing camera frame epoch and resource leases. Continue one handoff per stage.

After feature completion, perform the user's requested ONE consolidated CMake/CTest pass and supported Metal/Vulkan/GLES compile/GPU parity checks; fix failures before merging into main.
