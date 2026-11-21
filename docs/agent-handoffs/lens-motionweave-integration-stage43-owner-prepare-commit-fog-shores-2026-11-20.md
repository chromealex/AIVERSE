# AIVERSE LENS Stage 43 — two-phase owner transactions: generic coordinator, Fog and Shores (2026-11-20)

## Repository / branch
- Repository: chromealex/AIVERSE
- Branch: feature/lens-motionweave-integration
- Stage 42 parent: 33cadfcae2eb23ba320e683bee2aed9fc4d57313
- NOT merged to main.
- Previous handoff: docs/agent-handoffs/lens-motionweave-integration-stage42-frame-publication-epoch-2026-11-20.md.

## Exact source commits
- b63e950dd60d91c731f76e4bd1255dea628c25a1 — engine/render/OwnerPrepareCommit.h generic two-phase coordinator
- 7d08f291dd9bccf644c5e5f6cbb20967f7032bf2 — engine/view/VisibilityField.h prepared CPU/GPU visibility update API
- 6bbce40e3df6e0e5078594d975270d5b4fd9db9c — engine/view/VisibilityField.cpp: private R8 update, commit/retirement and rollback
- c8113bd32ef1c225344c9ad9004951722f9024a9 — modules/rts/view/RtsView.cpp: Fog GPU retirement included in owner pre-render collection
- 7f29f7bf1af4a884eb80aa26889f4aa75ac13309 — modules/rts/view/RtsShoreView.h: separate unpublished Shore geometry builder
- a48c8eb175c46dfbd628021112da5f1844e6828f — modules/rts/view/RtsShoreView.cpp: stage shore/foam/prop models then publish them together
- 39ce10edbd9849626335201fc5f25a2790f19d00 — tests/tests.cpp existing fog build test adjusted to new texture generation
- d271065b29e6091da7a47b66ea6a65d66e76bb85 — tests/tests.cpp generic coordinator and Fog staging fault tests
- 0d64864f9f8f176d0b7c68ed3d305dd239a3922a — VisibilityField.h: no-op candidate marker
- 60231733e037b0284334752477f31f5f5c386b9d — VisibilityField.cpp: unchanged fog update avoids CPU copies and GPU uploads
- e517732009ab5fecabfbcfc65c0c63bce320f7c3 — tests/tests.cpp Shore rebuild late GPU failure test
- b026b8a03d42b9fc4e299835f76875db26c805ee — tests/tests.cpp stale fog candidate and no-op path test
- 9281858f41e9c2ba1c5b83db3f9c1845c44498dd — VisibilityField.h: collecting retirement when not initialized is a no-op
- 37b772cfa729d4104df5a9b72f74d228d6217677 — RtsShoreView.cpp: preserve previous type warnings on candidate publication

## Design and APIs actually implemented
1. Generic engine/render/OwnerPrepareCommit.h is free of game/genre dependencies. An OwnerPrepareCommit::Participant supports prepare() (may throw), commit() noexcept, rollback() noexcept. prepareAll() visits owners in dependency order; any late failure rolls back the attempted owners in reverse order. publish() calls ONLY prevalidated, noexcept commits after every participant has prepared. A transaction abandoned before publish rolls back automatically. The coordinator cannot enforce a participant's promise not to mutate published resources; owners must meet the contract.
2. engine/view/VisibilityField now provides a move-owned std::unique_ptr<PreparedUpdate> from prepareUpdate(vision,w,h,time). It computes target, fade and blur in private CPU arrays, allocates a replacement R8 GPU texture, and uploads only to that unpublished handle. A failed allocation/update or an abandoned candidate destroys only the private texture; the live fog texture, time and field remain unchanged. Candidate revisions reject stale commits. commitPreparedUpdate moves the complete CPU data and texture generation into the live owner and queues the previous GPU texture in NativePresentationRetirement. update() is source-compatible, delegating to prepare+commit.
3. Fog no-op update path checks unchanged visibility when no fade/dirty state exists; it advances time without duplicating full CPU grids or allocating any new GPU texture. Fog now exposes pendingGpuRetirements and collectRetired(dev), with no-op on an unbound owner. Shutdown drains retired generations after explicit GPU idle proof.
4. RtsView::pendingModelRetirements and collectRetiredModels include Fog's texture-generation retirement queue in the existing host-owned pre-render boundary. The Stage 42 FramePublicationEpoch remains responsible only for consumer readiness, not RHI completion.
5. Shores now separate buildUnpublished from rebuild: a private RtsShoreView candidate borrows existing type-kit GPU texture/material references, builds the complete bank/foam/prop Model set without mutating published models, and aborts the entire candidate on a failed late chunk or foam upload. On failure the candidate's never-drawn models are released and old published shores remain unchanged. After all uploads succeed, old models are deferred to NativePresentationRetirement and new models/props/stats are swapped into the live owner. Diagnostics from the prior kit are carried into staging.
6. Shore candidate GPU texture references belong to the live kit, not the candidate; the existing view must retain its kit/device lease through the entire frame and the retirement fence. The replacement currently recreates all shore chunks/prop models on each rebuild (correctness-first).
7. No new genre-specific APIs were added to the generic coordinator or VisibilityField.

## Tests written or changed — NOT RUN
- owner_prepare_commit_rolls_back_earlier_gpu_owners_on_late_prepare_failure: simulated Geometry/Fog/Shore owners, fail third prepare, verify no partial commit and reverse rollback; retry publishes all.
- visibility_field_prepared_gpu_upload_never_mutates_live_on_failure_or_discard: candidate discarded or GPU update throws; old texture, field and retirement queue unchanged; successful commit queues previous texture.
- visibility_field_late_coordinator_failure_discards_private_gpu_texture: real staged VisibilityField owner plus a mock later failing owner, verify previous GPU generation persists until a successful retry.
- visibility_field_rejects_stale_prepared_generation_and_skips_noop_upload: reject stale candidate, and verify a stable fog frame creates no GPU texture/update.
- shoreline_staged_rebuild_preserves_published_banks_on_gpu_failure: sample shoreline across a 100x48 grid, inject GPU index-buffer failure after the initial publish, check old bank scene/retirements preserved, and retry.
- Updated visibility_field_gpu_build_rolls_back_buffers_texture_and_idle_refusal for the immutable R8 generation semantics.
- All tests are in tests/tests.cpp and use fogfaulttest::Device/Renderer3D fake graphics. In particular the sample shoreline's mesh visibility and GPU allocation count MUST be verified in a native test.

## Actual validation
- Inspected engine/view/VisibilityField.h/.cpp, engine/render/NativePresentationRetirement.h, Renderer3D.h, RtsShoreView.h/.cpp, RtsWater.h/.cpp, RtsView.cpp, RtsRoadView.h/.cpp and existing tests.
- Committed implementation and regression tests in GitHub and re-fetched the changed source. Static source checks confirm generic coordinator, candidate and commit APIs, Fog retirement hook and shoreline publication ordering.
- NOT RUN: CMake configuration, C++ compilation, aiverse_tests or CTest, Metal/Vulkan/GLES backend builds, GPU visual comparisons or memory/performance profiling. No PASS claims are supported.

## Crucial limitations — Stage 43 is a partial owner migration, NOT all-owner atomic commit
1. The generic two-phase coordinator is available and its composition with the Fog candidate is tested in a dedicated regression fixture, but the entire RtsView::draw call is NOT yet converted to one coordinator. Fog's source-compatible update() still prepares and commits locally during the current late Fog pass. A later owner failure can leave earlier owners changed; the Stage 42 frame readiness fence only blocks secondary consumption.
2. The Shore rebuild stages/commits its own models atomically but is not yet registered in a common RtsView-wide transaction. Shore resource kit reload is still separately managed.
3. Roads rebuild remains non-atomic: release(dev) executes before the new road geometry and props finish. Road on-demand painted GPU textures, decal SpriteBatch, film/puddles, wheel-surface tiles and material caches require an independent staged candidate/ownership audit before migration.
4. Terrain/Detail, GroundFX, Sprite/Animation and other view owners have their own transactions or mutable frame producers, not a unified cross-owner prepare/commit API. The old CPU animation clocks and primary Renderer3D commands are not reverted after a late preparation failure.
5. Immutable Fog textures can temporarily increase GPU memory due to one private generation for each changed frame. The existing retirement queue backpressure and completion tokens are the safety policy; benchmark and add generation throttling as appropriate.
6. Unpublished Shore candidates borrow textures from their live kit and do not own those shared GPU textures. They must never be independently shut down as if they owned the kit. Failed Model upload/quarantine paths should be verified with real RHI completion behavior.
7. Existing direct calls to Fog update() remain source-compatible, and its preparedUpdate() requires same-thread commit/cancellation before GPU device destruction.

## Next agent — suggested Stage 44
- First migrate RtsRoadView::rebuild to a real unpublished candidate including painted materials/images, decals, weather and ground surface, preserving previous frame packets on late failure.
- Expose Fog/Shore/Road prepared owner types to a common RtsView render-frame coordinator so all can stage first and commit only after the final owner preflight. Extend to Terrain, GroundFX and Sprite/Animation with immutable CPU clocks.
- Add cross-owner test where Fog and Shore prepare succeeds but Roads fails last, verifying the old complete frame remains active. Preserve the Stage 42 epoch consumer gate.
- Keep genre-neutral engine transaction APIs and separate handoff documentation for each stage.
- After feature work, run the user's ONE consolidated native build+CTest and supported Metal/Vulkan/GLES compile/GPU checks, fix failures, then consider merging main.
