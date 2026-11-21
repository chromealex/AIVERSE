# AIVERSE MotionWeave / Universal View — Stage 128 handoff

Date: 2026-11-19
Branch: `feature/universal-ecs-animation-pipeline`
PR: https://github.com/chromealex/AIVERSE/pull/22 (Open / Draft, base `feature/universal-entity-inspector-fps`)
Previous: [Stage 127](animation-stage127-model-epoch-bridge-pinned-appearance-2026-11-19.md)

## Repository status / parallel-work safety

- Continued Stage 127 HEAD `dfb3e76c9b7bdaacd29b757894de4f997418d269`.
- At start, `main` `d71c6d47e532b25fb816a8bb1f96c41e5275a165` was already an ancestor, ahead **937**, behind **0**.
- After Stage 128 feature source commits, ahead **941**, behind **0**, HEAD `a728c212c25611458fb138a23d37dda21b27a8b7`.
- No main writes, forced updates, merges or rebase. PR #22 stays DRAFT; recheck main, PR base and other feature branches next turn. NEREID/PHYSIS/TerraWeave/Prisma/SpectraFX/texture/batching are separate owners.
- This stage ONLY changes genre-free native appearance batch CPU ownership and GPU epoch preflight. MotionWeave ECS clock, renderer RHI, ModelResourceCache's existing GPU-retirement queue and genre simulation were NOT changed.

## Source work, commits, API

1. `58b48aa8709f047af7c6a3ad912c32af9b14ad2a` — `engine/view/AppearanceAssetRegistry.h`:
   - Extended `PinnedAppearanceBatch` with optional strong `std::shared_ptr<const render::Model>` CPU owner references.
   - `addOwned(snapshot, world, modelOwner)` accepts a real shared-ownership Model and validates it matches the published snapshot Model pointer before any batch mutation. On failure or throw, rolls back new model pin and leaves the previously assembled packet set untouched.
   - `pinRelatedModel(owner)` accepts only models referenced by existing packets' base draw, canonical animation source, visual Model LODs or child attachments. Rejects unrelated Model pointers and deduplicates already pinned models.
   - `missingModelPins()/allModelsPinned()/requireModelPins()/modelPinCount()` let native hosts verify every borrowed Model pointer actually has an opt-in strong CPU owner before View update/render. Optional missing streamed LODs (null model pointer) do not require a pin.
   - `clear()` destroys packets and appearance snapshot pins, and then releases the strong Model owner refs **only after** the host has completed the synchronous update/draw transaction.
   - Existing `add(snapshot,world)` remains for render hosts that cannot offer shared Model ownership, such as `ModelResourceCache`'s current `unique_ptr<Model>` map.
2. `73ca58cef50e8943a93db46dae2a17af83d29d66` — same file:
   - `validatedSpan()` enforces all Model owner pins before exposing a `std::span<const view::Object>`. Native clients can use the SAME validated span for `View::update(span,dt)` and `View::submit(renderer,span)`. This is source-only opt-in; legacy `span()` stays unchanged.
3. `c36a6a68249570aa1ba6c61ab6fd56462eadaad4` — same file:
   - `validatedSpan(gfx::Device&, RenderDeviceLifetime::Lease)` additionally checks current Device identity, owner thread and active GPU Device generation; rejects an expired/foreign generation without proceeding to View. The GPU lease is NOT a GPU fence and does NOT prove GPU completion or resource residency.
4. `a728c212c25611458fb138a23d37dda21b27a8b7` — same file:
   - `addOwned` now deduplicates strong Model refs across thousands of actors referencing the same Model. It retains only ONE strong reference per distinct model pointer per batch (plus separate references needed for other distinct source/LOD/attachment models).

## Usage for native shared-ownership hosts

```cpp
aiverse::view::PinnedAppearanceBatch batch;
auto snapshot = registry.find("hero");
if(!snapshot) return;
// These are real owners supplied by the HOST; never manufacture
// non-owning shared_ptr aliases around ModelResourceCache::unique_ptr.
std::shared_ptr<const aiverse::render::Model> heroOwner = sharedHeroModel;
std::shared_ptr<const aiverse::render::Model> lodOwner = sharedLodModel;
if(!batch.addOwned(snapshot, heroWorld, heroOwner)) return;
batch.edit(0).lods.push_back(compiledLod);
if(!batch.pinRelatedModel(lodOwner)) return;

// gpuLifetime is an active host-owned render::RenderDeviceLifetime.
auto objects = batch.validatedSpan(device, gpuLifetime.lease());
// The host keeps both GPU models resident until the matching GPU
// retirement marker confirms every submitted draw has completed.
view.update(objects, deltaTime);       // exactly ONE MotionWeave update
view.submit(renderer, objects);        // synchronous model packet consumption
batch.clear();                         // release CPU batch pins
```

The host must independently guarantee that `heroOwner` and `lodOwner` are genuine owning `shared_ptr` controls and their custom deleters DO NOT destroy in-flight GPU buffers. This API cannot detect a fake/no-op `shared_ptr` deleter or extend an expired GPU epoch. `RenderDeviceLifetime::Lease::validFor` checks the DEVICE generation and owner thread, but asynchronous GPU work needs the host's separate true completion/fenced retirement. Do not destroy models' GPU handles merely because the CPU batch was cleared. If a model owner uses a custom deleter to call `releaseModel`, the deleter must coordinate GPU retirement appropriately.

## Existing ModelResourceCache integration

- The generic `engine/render/ModelResourceCache.h` was **read but not modified**. Its `ModelMap` uses `std::unique_ptr<Model>`, its insert/replace/invalidate/trim/shutdown methods have explicit render-thread and GPU-retirement rules. It cannot currently give an authentic strong `shared_ptr<const Model>` to `PinnedAppearanceBatch::addOwned` without changing owner semantics. DO NOT wrap a cache-owned raw Model pointer in a no-op/shared alias just to pass strict checks.
- Existing cache-backed RTS/editor hosts should use the original `batch.add(snapshot,world)` borrowed path and ensure their ModelResourceCache transaction explicitly detaches View/Graph/LOD snapshots and waits/retire GPU work before cache eviction. Before trying to enable strict pinned mode for those hosts, define a real cache-level reader/eviction guard or a controlled shared-ownership conversion together with the cache owner.
- This stage deliberately DID NOT rewrite ModelResourceCache ownership or renderer GPU-fence queue, which are shared by parallel agent workstreams.

## Actual work and verification

Performed:
- Connected GitHub PR/branch/main ancestry reads and Stage 127 handoff.
- Inspected generic `ModelResourceCache` ownership and fence-retirement implementation, `RenderDeviceLifetime::Lease` rules and `PinnedAppearanceBatch` baseline.
- Four targeted GitHub source commits using current file blob SHA, readback of exact feature file and code review.

NOT RUN: native checkout, CMake configure, compiler/link, CTest, CI, GPU Metal/Vulkan/GL validation, editor/FPS/RTS visual parity, failure injection, sanitizers, native asset reload tests, ownership stress tests or benchmarks. Source-only change is **NOT VERIFIED for compilation/runtime**. Keep PR #22 Draft; do not claim the host GPU resources are automatically pinned.

## Known limits and exact next Stage 129

1. The optional owned batch needs real `shared_ptr` model owners; `ModelResourceCache` remains `unique_ptr` and still uses a borrowed host lifecycle. No automatic ownership bridging exists yet.
2. An owned batch can pin base/source/visual LOD/attachment Model CPU objects but the host still owns GPU residency and deferred resource destruction. A Device lease is NOT a GPU completion fence.
3. A packet can be edited after pins are checked; native callers MUST finish `edit` and pin all referenced models before taking `validatedSpan`. Do not mutate the batch while jobs are running or Renderer3D is consuming its packets. `validatedSpan` is a preflight, not a write lock.
4. A large batch's pin completeness check scans its packets and owner collection; this happens once at the host submission boundary. It has NOT been benchmarked for huge crowds.
5. Need a real, owner-approved `ModelResourceCache` eviction/read guard if legacy unique-ownership hosts want the same lifetime protection, without introducing GPU leaks or altering RHI lifetime. Work with the ModelResourceCache owner and respect concurrent branches.
6. After features are complete, run the full native CMake build, CTest, Metal/Vulkan/GL visual parity, hot reload and GPU-fence failure tests, sanitizers and performance benchmarks, then fix warnings before considering merge to main.
7. Next agent must compare fresh `main`, the PR base and peer branches; produce **Stage 129 standalone handoff** and update `AGENTS.md` after every stage. Do not force push.
