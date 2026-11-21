# SpectraFX Stage 35 — read-only ECS attachments, GPU completion fencing, Metal editor preview controls

Date: 2026-11-19
Branch: `feature/universal-entity-inspector-fps`
Starting branch HEAD observed: `4a50d05329840e713d6179a1519fb2f16a8d8604`
Main HEAD at ancestry check: `9ee6ff6fbf390bd45363bc92c838f492dfe92eee`
Status at start: feature branch was **450 commits ahead** and **0 commits behind** main, merge-base matched main HEAD exactly. **Main already present. NO MERGE NEEDED or executed.** Neither main nor other agent branches were changed.

## Changed files and exact source commits

1. `52eadf791c980e4d8591ca180e1b97b94bd43c91` — **new** `libraries/include/aiverselib/genre/SpectraGpuAttachmentEcsAdapter.h`: genre-independent `GpuEcsAttachmentPresenter`, querying the existing `SpectraSourceComponent` via const ECS iteration, entity.packed() generation identity, per-effect continuous policy, selective detach, pause on missing pose, rollback reset.
2. `940632a730f39903892df98cd88298b4891c0632` — same header: finite policy validation, correct unit Fixed literal, explicit `detachAll(killLiving)`.
3. `d13fd21ecf9cbd740fd011e394b62d4b7d65d742` — same header: validate new declarations before detaching prior sources, monotonic committed-tick `syncCommitted`, explicit reset on rollback and tick clearing.
4. `f867d8c3f248837a180a88d9ba09d206ac6fa617` — `engine/gfx/Gfx.h`: non-pure virtual `updateStorageBufferChecked` and `completedFrameSerial` default unsupported.
5. `bdce4593aa04c3c21d0907165eecf1138d4d70f9` — `engine/gfx/metal/GfxMetal.mm`: checked current-frame Metal storage blit upload, successful command-buffer completion serial retained in a shared atomic for safe owner-thread polling.
6. `5ad1201330829573743094d0586a052d37d88ae3` — `engine/fx/SpectraGpuParticles.h`: source-killed physical slots quarantine, pending fence and reclaimed-slot telemetry.
7. `f697ff383cade12da86f8498c15c295d8c2a5b91` — `engine/fx/SpectraGpuParticles.cpp`: coalesced source kills use checked RHI upload, pending slots migrate to completed-frame quarantine only after accepted compute; safe slot reclaim only when RHI reports a successful completed GPU frame, else retain. Checkpoint capture/CPU restore disallow when pending or quarantined kills exist; intentional kills excluded from fallback loss estimate.
8. `a9551115f29ec244e789dd2753fb324b6b1b99a7` — `libraries/include/aiverselib/genre/SpectraFX.h`: exports new native ECS source presenter.
9. `cb8ba123a463dea783945149e562d68c78e4cde8` — `engine/fx/SpectraGpuPoolWorld.h`: pooled `killedSlotsAwaitingFence` and `killedSlotsReclaimed` telemetry.
10. `e88476d37b9832888c5841b810dd6e42a8a713ba` — `engine/fx/SpectraGpuPoolWorld.cpp`: aggregates GPU source kill fence telemetry.
11. `7494eafe44429426951900f1e02005b23b7d7450` — `apps/editor/Editor.h`: Metal GPU preview active/paused/frozen/loop controls.
12. `5221dfcb3da40cb7ccd5e90640ea1cedf3ea4d92` — `apps/editor/Editor.cpp`: pause prevents new bursts; freeze advances GPU visual simulation with zero dt; loop remains opt-in. No authoritative gameplay impact.
13. `9774bb997ffbf10f9b726101d73c775fb4fbbe77` — `apps/editor/windows/SpectraGraphWindow.cpp`: Play/Pause, Freeze, Loop, Stop buttons and GPU kill-fence telemetry.
14. `f720cb7ad8b7d7092ee23001136bd4d4d4f66965` — `docs/spectrafx.md`: Stage 35 public API, usage, limits and merge status.
15. `7d58083b82cb21b2f13f484f13a00ea6e5e5ca1a` — `skills/visuals/aiverse-spectrafx/SKILL.md`: AI implementation guidance for generic ECS and Metal fences.
16. `6567488c232fcaa69ea4df678ec2e9f6d59c1752` — `AGENTS.md`: latest stage handoff pointer, ownership rules, no production claims.

## Architecture / native-host API

```cpp
#include <aiverselib/genre/SpectraFX.h>
using namespace aiverse::spectra;

GpuPoolWorld pool;
pool.init(device);
pool.defineEffect("flame",compiledEffect,gpuOptions); // opt-in GPU if supported

GpuEcsAttachmentPresenter bridge(pool);
PoolContinuousEmission policy{};
policy.burstsPerSecond=12.f;
policy.maxBurstsPerFrame=8;
bridge.bindEffect("flame",policy);

// On each rendered frame, after gfx::Device::beginFrame and before pass:
pool.beginPresentationFrame(camera,viewProj,width,height);
if(!bridge.syncCommitted(committedTick,world,resolveRenderPose)) {
    // Explicitly inspect bridge.reason(); no implicit success.
}
pool.step(dt);
// pool.draw(fog) inside scene depth-tested render pass
// pool.endPresentationFrame() after render pass

// Outside any active encoder, before authoritative tick replay:
bridge.resetForRollback(); // resets whole pool; must be dedicated scope
// When stopping the bridge without resetting shared pool:
bridge.detachAll(true); // selective source kill
```

No ECS components beyond the existing `SpectraSourceComponent` are introduced. No GPU handles, render-frame counters, particle physics, renderer objects or source IDs become deterministic authoritative ECS state. Policy is explicitly defined in presentation code. Missing pose pauses only new births and can resume, disable/removal uses `killOnDetach`, effect/revision/policy changes cause safe source replacement. The component's `scale` is conservatively restricted to exactly `1_fx`: strict GPU additive billboards cannot preserve arbitrary scale semantics. Same committed tick may be re-synchronized for pose interpolation; decreasing ticks require explicit presentation rollback reset.

Metal checked blits must run in a current frame before any encoder. `completedFrameSerial` tracks a successfully completed Metal command buffer, not just CPU submission. Killed physical GPU slots remain quarantined until their recorded submission frame is reported completed. If the RHI lacks completion tracking or device goes unhealthy, those slots are conservatively retained rather than reused. Source callbacks and checkpoint snapshots remain presentation only. No automatic assumption of lossless GPU-to-CPU recovery.

The editor's opt-in `fx_graph_preview {gpu:true}` still requires strict Metal-compatible graph and hardware. Play/Pause stops new births but lets particles age. Freeze stops visual time. Loop repeats one-shots; Stop releases the preview at the next safe phase. The panel also reports killed slots awaiting GPU completion vs safely reclaimed.

## Actually performed verification

- Retrieved both main and feature branch HEADs and compared ancestry on GitHub; main HEAD was exactly the merge base and 0 main commits were missing from the feature branch.
- Inspected ECS component types and prior CPU/GPU presentation adapters, GPU slot scheduler, Metal RHI command-buffer/submission path, the editor rendering entrypoints and branch source files.
- All above source and documentation commits acknowledged by GitHub connector.
- **NOT RUN:** local CMake or Clang builds, Objective-C++ compilation, MSL shader compilation, CTest, editor launch, graphics device tests, live GPU preview, multi-attachment stress, id reuse/fault-injection, frame-completion latency or GPU/CPU parity. This is **SOURCE IMPLEMENTED, EXECUTION UNVERIFIED**, not release-ready.

## Known risks

1. The presenter's `sync` can apply some source operations before a later runtime allocation failure (e.g. source budget exhausted), although malformed authored declarations are prevalidated before detachment. Recover by retrying at the next safe presentation frame; do not claim global transactional sync.
2. `GpuEcsAttachmentPresenter::detachAll` requires a safe graphics phase, and the pool must outlive the adapter. GPU sources must not be serialized in a save/rollback. A dedicated pool per rollback scope is needed since `resetForRollback` clears everything in the pool.
3. A checked GPU blit reports encoder submission, not successful GPU execution; only a later `completedFrameSerial` releases quarantined slots. A fault after partial kill blits leaves all pending slots retained for later retry. On GPU loss they are discarded together with the resource, never reused unsafely.
4. Metal's success serial assumes ordered command buffers in the single existing command queue. If the RHI adds additional GPU queues, explicit cross-queue fences will be needed. Unchecked GPU state uploads for ordinary births remain unchanged and require separate hardening.
5. `GpuEcsAttachmentPresenter` rejects non-unit ECS scale rather than approximating it. Actual mesh/trail/light/multi-emitter GPU effects remain unsupported and stay on the full CPU path unless `RequireGpu` explicitly rejects them.
6. Editor GPU preview remains limited to strictly eligible Metal billboard graphs. Freeze does not guarantee an identical screenshot to CPU prewarm/advance, and the UI has not been manually validated.
7. No real Metal hardware test, shader build, editor startup, numerical parity or performance data has been collected for Stage 35. Do not merge to production based on the presence of source code.

## Next Stage 36

1. Complete source-owned continuous render-pose attachment integrations in a small engine-native sample project, including ECS generation recycling and rollback, with clear one-world ownership semantics.
2. Improve GPU source-kill update efficiency: compact/scatter kills on GPU or bound fragmented blit ranges; first profile actual Metal cost on representative burst/source counts. Extend checked uploads to remaining GPU seed and VM buffers and fail atomically where possible.
3. Add explicit editor Metal preview play/scrub/time controls only when backend supports the semantics; avoid promising GPU feature parity with CPU graphs.
4. Once features-first phase ends, compile C++/Objective-C++, run CTest, Mac Metal smoke, real GPU completed-serial tests, ECS detach/rebind/revision tests, zero-dt and rollback tests. Correct errors before main merge.
5. Maintain one dated handoff per stage with exact code/doc SHAs, actual tested status, current branch and concrete next work; preserve concurrent agent edits and do not force push.
