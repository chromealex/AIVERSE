# SpectraFX native ECS attachment integration — Stage 36

This example is a **C++ integration example**, not a runnable game executable.
The header [native_ecs_attachments.h](native_ecs_attachments.h) is a usable
reference for a native project's presentation module; the host must provide
a `gfx::Device`, viewport, scene rendering, and committed `World`.

## Native project integration

1. Include `<aiverselib/genre/SpectraFX.h>` from your project's native
   renderer module; link `AIVERSE::spectrafx`.
2. Define an effect in `GpuPoolWorld` (the sample uses `"demo_flame"`
   and passes a compiled `fx::EffectDef`). `PreferGpu` uses Metal
   only for the *entire* strictly supported billboard subset. All other
   effects use the full existing shared CPU implementation.
3. Attach the existing `lib::genre::SpectraSourceComponent` to an ECS entity.
   Use `scale=1_fx` because current strict pooled Metal path rejects
   non-unit source scale rather than silently changing semantics.
4. Provide a read-only `ResolvePose` callback using a game-native
   presentation transform. The sample stores an example *fixed-point*
   `DemoPose` in ECS and converts it into float positions only in the
   presentation callback. It neither stores GPU handles nor mutates World
   during rendering.
5. Call `NativeAttachmentExample::prepare` once per presentation frame,
   after the host calls `Device::beginFrame` and **before** the render
   encoder/pass. Call `drawInsidePass` inside the depth-tested render
   pass and `finish` after ending the scene pass. The device remains
   owned by the host. Both CPU and GPU effects share the same one-world
   frame submission.
6. On entity destruction or disabling the component, the bridge retires
   the old `Entity::packed()`-keyed source. A newly recycled entity
   index has a different generation and starts its own VFX source.
7. On rollback, end any active render pass/frame, then start a NEW
   `Device::beginFrame`, call `afterRollback` outside render/compute
   encoders **while that graphics frame is open**, then call
   `Device::endFrame`. Only on success restart presentation from
   the restored authoritative snapshot. A checked Metal GPU reset now
   reports failure if called with no active RHI command buffer.
   This resets **the entire pool**:
   use a dedicated rollback scope, not a pool shared with unrelated
   presentation systems.

## What the example does not implement

- A `main` function, application loop, device creation, scene renderer
  or game networking. Those belong to the native project.
- A custom source ECS component; it reuses the engine component.
- GPU collision, damage, authoritative physics, character attachments,
  materials outside the strict Metal subset, or arbitrary Niagara graphs.
- A completed C++ build or running Apple Metal sample. These stages
  are still **not tested**. Source existence is not a passing test.

## GPU selective-kill optimization

Stage 36 replaces the many-contiguous-run dead-state Metal blits with one
checked upload of up to `capacity * sizeof(uint32_t)` bytes and one
compute shader dispatch in **mode 6**. This scatter pass invalidates the
tagged physical slots **before** Spawn/Integrate/Compact in the same
compute encoder. Slots remain quarantined until Metal reports a
successfully completed frame serial, preserving the Stage 35 fence guard.
`ComputeStats::gpuKillScatterDispatches`, `gpuKillIndexBytes` and the
corresponding pooled checkpoint stats expose the work; they are
instrumentation counters, not measured GPU bandwidth or performance claims.

## Next validation after feature work

Build on macOS with Clang and the Metal device, run
`ctest -L SpectraFX`, check `tests/vfx_metal_compaction_smoke.mm`,
and add coverage for kill shader mode 6, fragments, empty-kill frames,
source generation recycling, committed tick rollback, and multi-emitter
GPU/CPU fallback. No build or test has been run for this Stage 36 handoff.

## Stage 38 — interpolated poses and Device reconstruction

The sample now uses `spectra::GpuEcsPoseInterpolator`, exported by
`<aiverselib/genre/SpectraFX.h>`. The presentation thread captures fixed
`DemoPose` coordinates from the **committed** ECS World once per tick.
Its `prepare(...,dt,alpha)` accepts render interpolation
`alpha` from 0 to 1, keeps up to two committed render-pose snapshots
and blends only entities with the **same packed generation ID**
appearing in adjacent ticks. A newly recycled entity index, a missing
pose, or a skipped committed tick will **snap**, never interpolate from
an unrelated old entity. Authoritative ECS World is not modified.

The sample exposes `rebuildOnRecoveredDevice(recoveredDevice)`:
it exports the existing pool's authored effect definitions as a CPU-only
`PoolRecoveryManifest`, shuts down GPU/CPU resources while the **old
Device is still alive**, invalidates presentation source tracking and pose
history, and imports the definitions into the **same pool object** using a
healthy replacement graphics Device. Existing presenter effect policies
are preserved, but all live GPU particles, source IDs and render
timelines are deliberately discarded. On the next successful
`prepare`, committed ECS sources are reconstructed. If importing
`RequireGpu` effects into an unsupported backend fails, the pool is
not published as recovered.

```cpp
// After the host has created a recovered Device, while the old Device
// still exists so the old pool can dispose of its resources:
bool recovered=fx.rebuildOnRecoveredDevice(recoveredDevice);
// The host may now destroy the old Device.
// Only on success may presentation resume from the committed World.
```

For other hosts, `pool.exportRecoveryManifest()` can be called even
while the existing GPU is unhealthy (but the pool object must still
exist). Create a **fresh** `GpuPoolWorld` and call
`fresh.importRecoveryManifest(&recoveredDevice,manifest)`. Recreate
the ECS presenter against that fresh pool. Neither path promises to
restore GPU-resident particle state or use stale GPU checkpoints.

The pool's `resetReport()` exposes which GPU groups already accepted
a partial reset, how many remain, and whether a device-loss rebuild is
required. While `presentationBlocked()` is true, no new GPU scene
frames, sources or effects may be emitted. An accepted group reset is
an **RHI command encoding status**, not a claim that the GPU has
completed the work.

No native sample, test suite or Metal device run has been executed
for this stage.

## Stage 39 — recovery host events and bounded GPU checkpoint export

The native C++ example now owns `GpuPoolRecoveryHost` next to its
`GpuPoolWorld`, `GpuEcsAttachmentPresenter` and renderer-only
`GpuEcsPoseInterpolator`. Before each presentation frame,
`recoveryStatus()` / `recovery_.poll()` detects whether the pool is
ready, needs another GPU-group reset, or requires an explicitly
recreated graphics Device.

The host may register a change callback:

```cpp
fx.onRecoveryEvent([](const spectra::PoolRecoveryEvent& event) {
    // Host handles this synchronously on its presentation thread.
    // event.phase, event.next, event.resetGroupsAccepted,
    // event.resetGroupsTotal, event.resetAttempts, event.reason
});
// No callback creates an OS graphics Device automatically.
auto state=fx.recoveryStatus();
if(state.next==spectra::PoolRecoveryAction::RecreateGraphicsDevice) {
    // The platform host creates a replacement gfx::Device.
    // Keep old Device valid until fx releases GPU resources.
    bool ok=fx.rebuildOnRecoveredDevice(recoveredDevice);
    // Old GPU particles and renderer source IDs are intentionally gone.
}
```

`fx.afterRollback()` now uses the recovery controller's
`retryPresentationReset()`. A reset still requires a graphics frame
that has begun and **no active encoder**. Do not present an ECS frame
while `PoolRecoveryEvent::next` requests reset or graphics recreation.
Renderer reattachment resumes from the committed World snapshot
after a successful rebuild.

### Optional automatic VFX pressure policy

```cpp
spectra::PoolLoadSheddingPolicy policy;
policy.enabled=true;
policy.maxContinuousBurstsPerFrame=48;
policy.reserveManualBursts=16;
policy.estimatedParticleSoftLimit=8000;
policy.maxReadbackBytesInFlight=1024u*1024u;
fx.setLoadShedding(policy);
```

Only continuously emitting sources are throttled. Explicit `burst`
calls retain their normal global frame limit. Source scheduling uses
a rotating first-source cursor, preventing persistent index-order
starvation. Particle occupancy is the *previous step's CPU/GPU
estimate*, not measured exact Metal alive count. On saturation or
readback budget pressure, continuous births can drop entirely for
that presentation frame. Skipped bursts are not queued for huge
catch-up work. This is opt-in; default policy disabled.

### Real diagnostic checkpoint bytes (not rollback snapshots)

`fx.exportFreshGpuCheckpoint(groupIndex,bytes)` calls
`GpuPoolWorld::exportFreshCheckpoint` and copies an existing
completed GPU capture only if its revision matches the current GPU
revision, no unsent births/kills/fences are pending, the Device is
healthy and the readback is complete. It performs **no GPU readback**
itself. An export consists of a 40-byte versioned header, up to
512 KiB of particle + VM data, and up to 16 KiB of CPU lifespan
mirrors (total <= `kGpuCheckpointExportMaxBytes`). The binary is
for same-build diagnostics, **NOT** a portable savegame format,
deterministic gameplay state or a GPU timeline rewind point.

The editor now offers **Save fresh GPU checkpoint** for an eligible
connected Metal pool. It writes `.spfx` data under the platform's
`userDataDir()/spectrafx-diagnostics/` directory. If the async
checkpoint is stale, incomplete or in progress, the button is
disabled or export fails explicitly. Reproducing the snapshot
requires a complete GPU/CPU restoration pipeline that is not claimed
by this diagnostic API.

This example has not been compiled or exercised on Metal hardware
for Stage 39. All actions are source implementations pending native
validation.

## Stage 41 — controlled native checkpoint capture and offline inspection

The native `NativeAttachmentExample` now owns
`spectra::GpuPoolCaptureHost` alongside its existing ECS bridge,
interpolated render poses, GPU pool and recovery controller. The new
capture workflow is **explicit, nonblocking and bounded**. The host
still owns its single `gfx::Device` and the full rendering lifecycle;
no new render thread or secondary graphics encoder is introduced.

```cpp
// Between Device frames:
if(fx.queueGpuCheckpoint(0)) {
    // The native example pauses VFX presentation at a stable revision.
}

// The existing per-frame loop continues normally:
// Device.beginFrame(...) delivers async RHI callbacks.
// fx.prepare(...) polls completion BEFORE any GPU state changes.
// It calls pool.step(0) while capture is pending or ready, then invokes
// GpuPoolCaptureHost::submitAfterStepBeforePass() between compute and pass.
// fx.drawInsidePass(...), fx.finish(), Device.endFrame() as usual.

if(fx.gpuCaptureStatus().phase==spectra::GpuCapturePhase::Ready) {
    std::vector<uint8_t> diagnosticBytes;
    if(fx.takeGpuCheckpoint(diagnosticBytes)) {
        const auto summary=
            spectra::inspectGpuCheckpointDiagnostic(diagnosticBytes);
        // summary is offline-only diagnostic information, NEVER ECS data.
    }
}
// To discard a pending/ready request and resume regular VFX:
fx.cancelGpuCheckpoint();
```

There is at most one capture request per host. The capture controller
records the `GpuPoolWorld::lifecycleEpoch()` and a request ID, checks
current GPU revision after a complete callback, stores at most
`kGpuCheckpointExportMaxBytes` bytes (540,720 bytes for 4096
physical slots), and stops waiting after 600 owner-thread graphics-frame
polls. Completion callbacks capture only a **weak pointer** to
capture state, not the native host or a GPU pool pointer. A callback
from a cancelled, failed or previously recreated GPU Device cannot
publish into the latest request.

The same demo calls `capture_.cancel()` BEFORE presentation rollback,
Device replacement or `shutdown()`. New ECS visual source bindings
are recreated from committed World state after recovery; old GPU
particles, capture bytes and renderer source handles are never
resurrected or fed into gameplay checksums.

To inspect a saved editor `.spfx` file without starting the editor or
Metal driver, compile and run the offline C++ CLI described in
`tools/spectrafx/README.md`:

```sh
c++ -std=c++20 -O2 -I. tools/spectrafx/inspect_spfx.cpp -o inspect_spfx
./inspect_spfx --json /path/to/checkpoint.spfx
```

The inspector reports detailed V2 error classes (magic/version,
header, truncation, bounds, length, CRC) and per-particle semantic
warnings (live/dead slots, nonfinite data, abnormal ages and lifespan
mirror mismatches). It does **not** edit the file, reconstruct the
simulation, import GPU state or make the V2 binary portable.

**Stage 41 status:** GitHub source committed; native/Metal compilation,
CTest, CLI execution, callback lifetime and restart smoke tests have
NOT RUN. Treat this as an unverified native integration example.

## Stage 42 — private reports and multi-group capture routing

A native project can request one GPU checkpoint using an explicit
group index or one of three selection policies. This is
presentation-only diagnostics: no gameplay ECS state is touched.

```cpp
// Between graphics frames:
fx.queueGpuCheckpoint(/*groupIndex=*/1);
// Or choose a group with one queued request at a time:
fx.queueNextGpuCheckpoint(
    spectra::GpuCaptureTargetPolicy::MissingOrStalestCheckpoint);
// Other policies: RoundRobin (default), LargestEstimatedGroup.
// Owner frame: Device.beginFrame -> fx.prepare() -> scene draw ->
// fx.finish -> Device.endFrame, as in the Stage 41 example.

// After capture is Ready, take a privacy-safe JSON summary INSTEAD of
// the raw V2 binary checkpoint:
std::string safeJson;
if(fx.takeSafeGpuDiagnosticReport(safeJson)) {
    // Report contains counts and numeric warnings only.
    // No world coordinates, VM scalar values or exact GPU serials.
}
// For a NEW capture where sensitive world-space bounds are explicitly
// required, request/complete a second readback first. takeSafeGpuDiagnosticReport
// CONSUMES Ready state and cannot take the same request twice:
fx.queueGpuCheckpoint(1); // only after previous Ready was consumed
// ... advance native presentation frames until Ready again ...
spectra::GpuDiagnosticReportPrivacy privacy{};
privacy.includeWorldBounds=true;
fx.takeSafeGpuDiagnosticReport(safeJson,privacy);

// For a deliberately requested RAW binary blob, use the separate
// fx.takeGpuCheckpoint(rawBytes). It contains sensitive scene positions
// and graph VM parameters and is NOT a savegame.
```

The capture controller's default selection uses a rotating start
across currently active GPU groups to avoid a permanent low-index
preference. The alternative policies select by previous CPU
alive-particle estimate or missing/stale checkpoint revision,
respecting existing pending GPU readbacks. This never starts
multiple asynchronous captures at once and cannot override the
Stage 32 per-frame/in-flight readback budget.

A native rendering host can request a visual reset *before*
beginning a graphics frame:

```cpp
fx.queueVisualReset(); // emits native ResetRequested status; no RHI work
// Later in a safe graphics frame, outside all rendering encoders:
device.beginFrame(width,height);
bool resetOk=fx.afterRollback(); // retries checked GPU group reset
device.endFrame();
```

The recovery callback now includes `transitionSequence`,
`hostResetRequests`, `deviceReplacementAttempts`,
`explicitResetPending` and `recoveryCatalogRetained`.
It reports a separate `ResetRequested` phase; failed
cross-group uploads remain `ResetRetryRequired`, and lost/failed
new-device imports remain `DeviceRecreationRequired`. On reset
failure the native host MUST NOT advance VFX presentation until
retry or graphics-device reconstruction succeeds.

The new `engine/fx/SpectraDiagnosticReport.h` and offline
`tools/spectrafx/inspect_spfx.cpp` implement redaction-by-default.
The editor now has separate explicit safe JSON and sensitive RAW
GPU checkpoint save actions. No automatic uploads of diagnostic
files, source graphs or VM data are performed.

These Stage 42 changes were source-committed but not run in a
native C++/Metal build or GPU hardware test.

## Stage 43 — explicit editor mailbox, safe bundles and owner-frame recovery

Native host projects can now connect a **non-owning**, owner-thread
mailbox to the existing editor. The mailbox carries only a GPU group
index, a bounded request sequence and state/error information.
It neither creates GPU resources nor initiates a Metal readback
from UI rendering. It is optional: standalone games without an
editor work exactly as before.

```cpp
NativeAttachmentExample fx; // outlives all editor attachments
// After fx.init(...), attach while the native renderer is active:
editor.setSpectraGpuTelemetry(&fx.poolTelemetry());
editor.setSpectraGpuCaptureMailbox(&fx.captureMailbox());
editor.setSpectraGpuRecoveryHost(&fx.recoveryHost());

// UI: Request selected group from render host.
// Native Device owner: Device.beginFrame -> fx.prepare(...)
// fx.prepare calls captureMailbox.claimRequested and capture.request
// BEFORE pool.step(0), then submits after step and before render pass.
// The host updates mailbox status as the async readback completes.
// UI: Save SAFE ZIP or JSON (no raw) or explicitly save RAW binary.
// UI: Release native capture. Host consumes release on next frame.

// MUST detach BEFORE fx.shutdown() or object destruction:
editor.setSpectraGpuRecoveryHost(nullptr);
editor.setSpectraGpuCaptureMailbox(nullptr);
editor.setSpectraGpuTelemetry(nullptr);
fx.shutdown();
```

All calls on the **same presentation/Editor owner thread**. The
mailbox is a plain message-slot API and does not contain network
locks, background tasks, RHI operations or authoritative ECS
component writes. It supports
`Idle/Requested/Claimed/InFlight/Ready/Failed/ReleaseRequested`
states with an exact sequence number. The native example cancels
the capture and clears the mailbox before rollback, GPU Device
replacement or shutdown. Late GPU callbacks cannot post data
to a newer sequence or pool lifecycle.

The native host also calls
`GpuPoolRecoveryHost::serviceQueuedResetAtSafeFrame(frameOrdinal)`
at a safe point after Device.beginFrame. A queued visual reset
is retried with an exponential frame-based cooldown capped at
32 frames if a recoverable reset fails, rather than issuing
GPU reset blits continuously every render frame. The host can
observe `PoolRecoveryEvent::consecutiveResetFailures` and
`retryNotBeforeOwnerFrame`; a lost Device requires platform
re-creation, not repeated reset attempts. No OS graphics Device
is created automatically.

For support cases, use **Save SAFE ZIP bundle** in SpectraFX Graph
Window. It creates one local ZIP STORE archive <=128 KiB containing
only `manifest.json`, `checkpoint_summary.json`, and
`preview_timeline.json` with at most 180 aggregate-only frames.
No raw `.spfx` file, full world positions, VM scalar arrays,
original paths, GPU frame serials or CRC fingerprints are stored.
Sensitive world bounds/identifier checkboxes used for an
ordinary JSON report cannot override this stronger ZIP redaction.
The default file naming includes a local numbered suffix but
**no GPU frame serial**. The RAW SPFX export is a different,
explicit user action.

The offline CLI also supports:

```sh
./inspect_spfx --bundle /path/to/support.safe.zip checkpoint.spfx
```

The CLI ZIP has an empty sanitized preview timeline. No automatic
upload, external HTTP request or file sharing is performed.

**Verification:** Source committed in GitHub. C++/ObjC++/Metal
compile, running the offline CLI, validating ZIP structure with
an unzip tool, real editor interaction, mailbox/device recovery
tests and CTest have NOT RUN for Stage 43.
