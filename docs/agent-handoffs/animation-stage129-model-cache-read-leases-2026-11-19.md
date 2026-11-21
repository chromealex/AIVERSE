# AIVERSE MotionWeave / Universal View — Stage 129 handoff

Date: 2026-11-19
Branch: `feature/universal-ecs-animation-pipeline`
PR: https://github.com/chromealex/AIVERSE/pull/22 (Open / Draft)
Previous: [Stage 128](animation-stage128-native-model-lifetime-pins-2026-11-19.md)

## Branch / peer agents

- Continued Stage 128 HEAD `8d1a0bd559073707c78058317b7eda4dfab5d49e`.
- `main` at start `d71c6d47e532b25fb816a8bb1f96c41e5275a165`,
  feature ahead 947 / behind **0**. After the **eight source commits**
  listed below, feature ahead 955 / behind 0, HEAD
  `bc272c70ba2291ddd0dc3fd656f9d65a64ac5a24`.
- PR #22 base is still `feature/universal-entity-inspector-fps`, NOT
  `main`. No main writes, forced updates, rebase or unrelated merge.
  Check latest main/PR-base and sibling branch heads on resume.
- This stage ONLY changes genre-free `ModelResourceCache` CPU read
  protection, `PinnedAppearanceBatch` integration and RTS/editor
  adapter read scope. It does not modify GPU fence ownership, physics,
  NEREID, TerraWeave, SpectraFX, Prisma, texture RHI or batching.
  The cache is used by other teams, so review native compilation and
  branch integration conflicts before merging this feature.

## Source commits and architectural changes

1. `069a5d227d072d53e87a33723ce5068a84053ee3` —
   **NEW** `engine/render/ModelReadLease.h`.
   - `ModelReadLeaseState` stores atomic active reader count,
     active lifetime flag and ModelResourceCache owner's thread ID.
   - Move-only `ModelReadLease` holds a BORROWED `Model*` and a
     shared lifetime-control state (not a shared Model owner).
     `get()`/ `valid()` fail if owner was retired or called on
     foreign thread. Destruction/ `reset()` decrements the active
     count; the token does not free GPU handles or hold a fence.
2. `e1bb635bac1834de97d008382d719c5f2cf57f45` —
   `engine/render/ModelResourceCache.h`:
   - Adds `acquireModelRead(const Model*)`,
     `acquireModelRead(const std::string&)`,
     `activeModelReaders()`, private `readState_` and
     `requireNoModelReaders`.
   - Cache remains `ModelMap=std::map<std::string,
     std::unique_ptr<Model>>`. Acquiring by pointer checks it
     is CURRENTLY resident in this exact owner cache; a retired,
     foreign or unknown pointer cannot acquire a read lease.
   - Lease acquisition is confined to the bound render-owner
     thread, and later Device binding must match that thread.
   - `invalidateSources`, `replace`, `trimUnused` and
     `shutdown` now fail closed with `logic_error` if any
     leases are outstanding, BEFORE detaching/retiring a Model.
     This prevents eviction while submitted borrowed CPU Model
     references are still being read. Publishing additional
     non-conflicting Models and polling previously retired GPU
     fence queues are not blocked.
   - Destructor rejects active leases by printing a diagnostic
     then `std::terminate()`. This is intentional fail-fast on
     an otherwise unavoidable use-after-free of uniquely owned
     CPU Models. Hosts MUST finish all read leases before the
     owning ModelResourceCache is destroyed; do not treat this
     as regular control flow.
3. `b36e034b4cfda2d4876d4f67d0ddeb51b3c03159` —
   `engine/view/AppearanceAssetRegistry.h`:
   - Adds `PinnedAppearanceBatch::addCacheOwned` for REAL
     `ModelReadLease` tokens acquired from a `ModelResourceCache`,
     and `pinCacheRelatedModel` for its visual LOD, source rig
     and child attachment Models.
   - Batch validates exact published Model pointer and whether
     related Models occur in its authored Object packets.
     Accepted cache tokens are move-only and deduplicated across
     repeated instances of the same cache Model.
   - `missingModelPins` now counts either genuine strong
     `shared_ptr<const Model>` owners (Stage 128) OR valid
     cache read leases, and `validatedSpan` works with both.
   - `clear` destroys the View Object packets and material/graph
     pins FIRST, then releases shared CPU owners and finally
     the cache read leases. Do not release while worker jobs or
     Renderer3D reads are in progress.
4. `cd5b9668e65a957350d0ccc351b29c77b6fd168a` —
   `ModelReadLease.h`: move-only `ModelCacheReadScope` for one
   coarse multi-model render transaction, using the SAME active
   reader counter. It holds no raw `Model*`; ideal for legacy
   hosts issuing many model draw packets with old raw pointers.
   Its destructor automatically releases the global cache read
   protection after the synchronous draw finishes.
5. `628557b416ea10cab65c01bffd091cb3557cc1b1` —
   `ModelResourceCache.h`: adds
   `acquireFrameReadScope()`, owner-thread-checked, usable
   after synchronous load/preparation. It blocks eviction of
   ALL resident Models while the scope is active. This is
   deliberately conservative; it does not attempt a brittle
   per-object count in old genre adapters.
6. `5866d5bfa7ec05ebd9f3cbfe564937ad6d9117d7` —
   `modules/rts/view/RtsView.cpp`:
   - `RtsView::draw` now acquires an RAII cache-wide read
     scope AFTER its initial synchronous `loadType` loop
     and holds the scope while resolving scene objects,
     MotionWeave graph pose evaluation and synchronous
     Renderer3D model packet submission.
   - Existing RTS `RtsView::shutdown` and hot reload already
     detach graphs/Models before eviction. New runtime guards
     throw instead of silently invalidating active draw pointers.
     Deferred GPU retirement is still handled by the original
     `ModelResourceCache::retired_` queue.
7. `606a065ec2ea08fdd3f5bd3029a4f63e8efb4f25` —
   `RtsView.h`: read-only `activeModelReadLeases()` for
   host lifecycle diagnostics.
8. `bc272c70ba2291ddd0dc3fd656f9d65a64ac5a24` —
   `apps/editor/windows/StatsWindow.cpp`: shows currently
   active eviction-blocking Model read leases in the editor
   Stats panel when count is nonzero.

## C++ API examples

Native cache-backed batch (unique ownership retained):

```cpp
aiverse::render::ModelResourceCache& cache=cacheOwner;
auto published=appearanceRegistry.find("hero");
if (!published) return;

aiverse::view::PinnedAppearanceBatch batch;
auto cpuRead=cache.acquireModelRead(published->model);
if (!batch.addCacheOwned(published,world,std::move(cpuRead))) return;
batch.edit(0).lods.push_back(compiledLod);
auto lodRead=cache.acquireModelRead(compiledLod.model);
if (!batch.pinCacheRelatedModel(std::move(lodRead))) return;

// All references are owned or guarded; optional device lease preflight:
auto packets=batch.validatedSpan(device,gpuLifetime.lease());
view.update(packets,dt);       // one presentation ECS graph update
view.submit(renderer,packets); // synchronous consume of pose and matrices
batch.clear();                 // releases cache leases after all draws
// Only NOW is cache eviction/replacement allowed again. Cache's own
// GPU retirement queue still protects previously submitted GPU handles.
```

Existing RTS compatibility path:

```cpp
// After synchronous loadType() calls, before the first raw Model use:
[[maybe_unused]] auto guard=resources_.acquireFrameReadScope();
// Resolve animation source, draw all raw models and animations here.
// Guard destructor releases before the host's next eviction boundary.
```

The cache scope is CPU protection only. It does **not** make
`Model::vb`, textures, skins, GPU buffers or geometry pages safe
to destroy. Only the owner's existing GPU work-completion tokens,
fences or proven device idle allow that. The resident cache stays
`unique_ptr`-based; DO NOT manufacture a no-op `shared_ptr` to
simulate ownership, and do not change shared GPU resource policy.

## Actual checks performed vs postponed validation

Performed:
- Connected GitHub PR/branch/main ancestry read and prior Stage 128
  handoff; inspected ModelResourceCache's unique ownership,
  source invalidation, replacement, trim, shutdown and GPU
  retirement contracts.
- Inspected `RtsView::draw` initial sync load and ordering, then
  inserted the cache-wide guard after the known early load loop.
- Updated source through eight exact-branch GitHub commits with
  fresh blob SHAs and re-read edited interfaces/source.

**NOT RUN:** native checkout, CMake configure/build/link, CTest,
GitHub CI, Metal/Vulkan/GL validation, FPS/RTS/editor visual tests,
concurrent read/eviction failure injection, ASan/TSan/UBSan,
GPU ownership tests, VRAM or CPU performance benchmarks. Source
remains **UNVERIFIED FOR C++ COMPILATION/RUNTIME**, and PR #22
MUST remain Draft. No measured performance benefit claimed.

## Risks, unfinished work and Stage 130

1. The cache read counter is global per cache, deliberately blocking
   all evictions while any reader exists. A native host attempting
   `replace`, `invalidateSources`, `trimUnused`, or
   `shutdown` during a draw now gets `logic_error`. Move these
   mutations OUTSIDE the `acquireFrameReadScope` boundary.
2. The guard cannot stop an external actor from directly mutating
   a borrowed `Model*` in-place. The Model must be immutable
   during View graph evaluation and submission; reimport requires
   a separate publication/Model epoch.
3. `ModelReadLease` and `ModelCacheReadScope` are owner-thread
   scoped; `get` returns null on a foreign thread. Reader release
   uses atomic decrement to support exception unwinding.
   `ModelResourceCache` destruction with active leases is a
   fatal host bug and deliberately terminates instead of allowing
   borrowed use-after-free.
4. Existing `RtsView::draw` has a global scope, so asset loads
   that INSERT brand-new models are still permitted; any
   REPLACEMENT/trim during the scope now fails closed. The native
   visual parity and potential streaming interaction need testing.
5. `PinnedAppearanceBatch`'s strict validation is a PRE-FLIGHT,
   not a lock against mutation of its own packets. Finalize
   attachments/LOD before `validatedSpan`, do not edit/clear
   until synchronous submission completes.
6. Next Stage 130: finish any remaining ownership/animation
   feature work, then run full native configure/build/CTest,
   GPU backend + editor/RTS/FPS visual parity, sanitizer,
   Model pointer recycle, read-lease eviction-failure and
   hot reload tests. Fix compiler warnings/errors, compare
   current main/PR-base/parallel agents before integration.
   Produce a separate Stage 130 handoff and update AGENTS.md.
