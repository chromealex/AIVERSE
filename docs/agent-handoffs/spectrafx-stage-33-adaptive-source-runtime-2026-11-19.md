# SpectraFX Stage 33 — adaptive GPU checkpoints, generation-safe pooled sources, opt-in live editor telemetry

Date: 2026-11-19
Branch: `feature/universal-entity-inspector-fps`
Starting HEAD observed: `9d9126d23acf941001f3e6e00aa33c5db6ad9794`
Latest observed source/docs commit before this handoff: `af43a9ee0373ac476b3de6c1ab9f6570c44000ad`
Operations use GitHub connector's guarded file contents updates; no cherry-pick, main merge, rebase, branch rename or force push was performed.

## What changed (exact commits / changed files)

- `6b333631fbfc72fe2272dac77567130f092927e7` — new `engine/fx/SpectraAdaptiveCheckpoint.h`: pure adaptive interval controller (bounded doubling on rejection, halving after N successful completed captures), frame-revision counters and opt-in policy.
- `5c52e5e276922c3158b9de81cf8d91f16b82ec21` — `engine/fx/SpectraGpuParticles.h`: `ComputeEmitterDesc::adaptiveCheckpoint`, status current interval / next due revision, adaptive statistics and controller instance.
- `05a9dabec06ece1884ed318367debe415ddc3554` — `engine/fx/SpectraGraphRuntime.h`: `GraphRuntimeOptions::adaptiveCheckpoint`.
- `0f86e75586d7d7ee5a670005b2dc16574a2f317d` — `engine/fx/SpectraGraphRuntime.cpp`: propagate adaptive options to eligible GPU emitter.
- `7b3149645ef969c0609515e744408ec57c91a76a` — `engine/fx/SpectraGpuParticles.cpp`: schedule async capture by submitted GPU revision, defer and backoff on unavailable bandwidth or in-flight capture, recover cadence on completed callback, publish status.
- `293d21344588cddf429b2bb154771a2759b604df` — `engine/fx/SpectraGpuPoolWorld.cpp`: factor entire adaptive policy into pooled GPU group cache keys, preventing mismatched cadence sharing.
- `d58c73bc0f890aeb0cdf825869ae739b5c63f92c` — `engine/fx/SpectraGpuPoolWorld.h`: `PoolSourceId` (worldEpoch+slot+generation), bounded per-world source budget, source lifecycle and parameter APIs, source slot storage, binding revision guards, active-source telemetry.
- `08fe18b4fa71bafc92132ad740b25e88cd9b1324` — `engine/fx/SpectraGpuPoolWorld.cpp`: source allocation/recycling with generation protection, per-source parameter snapshot, pause/resume/retire, emit through existing compatible pooled burst, binding revision invalidation and scene rollback source invalidation.
- `ce65638734a54fdee083760f63ce4c1251677516` — `apps/editor/Editor.h`: optional non-owning GPU telemetry pool pointer, setters/accessors.
- `1c29997f6b8455dc30045ccc8c0db7094ff46faa` — `apps/editor/Editor.cpp`: null borrowed telemetry pointer on editor shutdown.
- `82db8334b8cce8ed59747b6d0942ee3def14d95f` — `engine/fx/SpectraGpuPoolWorld.h`: aggregate adaptive backoffs, recoveries, max group interval.
- `508cd40d7639340578f1b5eb1bc4f017d880b8a0` — `engine/fx/SpectraGpuPoolWorld.cpp`: sum source adaptive telemetry on request.
- `763964477084f379d8dd1a0c51d7da79aac1e75a` — `apps/editor/windows/SpectraGraphWindow.cpp`: present attached GPU checkpoint stats, source counts and bandwidth live; explicit CPU-preview-only state when not attached.
- `f191fd3694a0d672eb2f0f5d8e1f64e161785d1e` — `apps/editor/windows/SpectraGraphWindow.cpp`: align warning color with existing `ui::Tone::Warn` enum.
- `af43a9ee0373ac476b3de6c1ab9f6570c44000ad` — `docs/spectrafx.md`: public contract, usage, actual limitations.

## Architecture and example C++ API

```cpp
namespace spectra=aiverse::spectra;
spectra::GraphRuntimeOptions opts;
opts.checkpointEveryFrames=15;
opts.adaptiveCheckpoint.enabled=true;
opts.adaptiveCheckpoint.maxIntervalFrames=240;
opts.adaptiveCheckpoint.recoverAfterCompletions=3;
opts.checkpointBudget=sharedBudget; // optional Stage 32 limiter

// Same options are accepted by pooled defineGraph/defineEffect.
spectra::PoolSourceId source=pool.createSource("impact");
pool.setSourceNamedParameter(source, "speed", 3.f); // schema field required
pool.emitSource(source, pos);
pool.pauseSource(source);
pool.resumeSource(source);
pool.retireSource(source);

// Optional editor attachment; caller retains and destroys pool after detach.
editor.setSpectraGpuTelemetry(&pool);
// ... while live ...
editor.setSpectraGpuTelemetry(nullptr);
```

Only a renderer-owned presentation source is represented. Existing CPU and GPU effects still share their resource group; per-source default params are snapshotted at creation and copied into each burst. A source can be retired or paused without affecting other source handles; `PoolSourceId` has a per-initialization epoch plus generation-guarded slot identity. Rebinding the same name to another effect changes the binding revision; old sources cannot emit into the replacement silently. A rollback retires all live source slots and keeps generation counters intact. Source slots are bounded by `PoolWorldBudgets::maxSources`, default 4096.

Adaptive checkpoints only operate for a positive `checkpointEveryFrames` baseline, and are OFF by default. No GPU submission means no new scheduling revision. Busy/rejected checkpoint attempts double an adaptive interval up to `maxIntervalFrames`; healthy completed captures halve it towards baseline after configured successes. Readback budgets remain enforced by Stage 32. This only changes visual diagnostics and recovery opportunities, not ECS simulation, authority or gameplay networking.

The SpectraFX editor view displays live attached data from `GpuPoolWorld::checkpointStats()`; normal Scene preview still executes the established CPU ParticleSystem. Attachments are opt-in non-owning pointers and must be detached before their source pool is destroyed. A null pointer displays an explicit unavailable state, not fake zeros.

## Critical limitations / risks

1. `pauseSource` / `retireSource` **only stop future births**. Already-born GPU particles age out naturally. This does NOT implement selective kill/fade-out of physical GPU particles, proper continuous GPU emission scheduler, attachments to transforms, or a multi-emitter GPU simulation with source ownership of every slot. No such claim should be made.
2. The pool's GPU group creation still requires strict billboard subset eligibility. An unsupported effect remains in shared full CPU BatchWorld. `RequireGpu` is never silently downgraded.
3. Checkpoint snapshots may be stale when their callbacks arrive. A successful readback does not establish exact CPU/Metal trajectory parity or emitter continuity. Current-revision-only fallback remains.
4. Header-only adaptive scheduling is owner-thread-only. Revision scheduling does not incorporate wall-clock latency, real GPU readback throughput, or future device fence timing. Backoff recovery may lag if all future attempts are denied indefinitely.
5. Source handles belong to one initialized pool; do not persist them in gameplay ECS save/rollback or network packets. The world epoch counter is process-local and is not a globally stable GUID.
6. Source param API uses declared named parameters only. Per-source parameter updates affect future births, never already-active particle instances.
7. Editor GPU telemetry is optional and **not** wired into the existing CPU Scene preview's rendering pipeline.
8. No changes to Prisma postprocessing, PHYSIS weather, Terra landscape or genre packages.

## What was actually verified

- Read the existing Stage 32 branch and source files, submitted GitHub connector file updates, received commit SHAs.
- Corrected a UI enum spelling after reading the actual `EditorUtil.h` `ui::Tone` definition.
- **NOT RUN**: native C++ compiler/CMake build, Metal shaders, CTest, editor window launch, GPU performance benchmark, renderer playback, live GPU telemetry, concurrency stress, per-source lifecycle tests, rollback fault injection or callback-cancellation tests. This implementation is still unverified executable source, not certified production-ready.

## Concrete next stage 34

1. Add GPU physical-slot ownership per pooled source to support selective kill/stop/fade without impacting other sources. Account for GPU-indirect compaction, pending births, source generation and Metal command-buffer ordering; never recycle a physical slot before all prior GPU work referencing it is safely ordered.
2. Add a reusable continuous emission scheduler with rate/time/distance gates; particle slots and transforms must remain presentation-only. Supply source handle APIs for per-source `stop`, `pause`, `resume`, attach/detach and lifetime.
3. Add an actual optional Metal GPU preview/diagnostic host integration in the editor, not only the telemetry attachment hook. Preserve CPU fallback and clear capability diagnostics.
4. After requested feature implementation is complete, compile C++/Metal and add regression tests (adaptive bounds, source generation reuse/rebind/rollback, callback budget pressure, UI compile) before production merge.
5. Preserve a new stage handoff with files, exact SHAs, tested status, limitations, branch and next action. Avoid force pushing over parallel agents.
