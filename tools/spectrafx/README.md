# SpectraFX .spfx V2 offline inspector (Stage 41)

This is an **offline native C++ diagnostic** for raw SpectraFX Metal
GPU checkpoint files exported by the editor or native
`GpuPoolCaptureHost`. It has no dependency on gfx::Device, a Metal
driver, the scene renderer, or an authoritative ECS World. It does
**not** restore particles, edit a checkpoint, execute shader code,
play a VFX graph or load a savegame.

## Build and use (after native toolchain is configured)

From the **AIVERSE repository root**:

```sh
c++ -std=c++20 -O2 -I. tools/spectrafx/inspect_spfx.cpp -o inspect_spfx
./inspect_spfx path/to/checkpoint.spfx
./inspect_spfx --json path/to/checkpoint.spfx
```

Exit codes: `0` structurally valid V2 payload (it may contain
semantic warnings), `1` invalid format/checksum, `2` invalid
arguments or file input. The tool reads at most
`kGpuCheckpointExportMaxBytes` (524,288 GPU bytes + 16,384 lifespan
bytes + 48 V2 header bytes = **540,720 bytes**). Input files that
change during reading are not accepted.

The inspector reports a specific structural failure:
truncated header or payload, file too large, incorrect SPFX magic,
unsupported version, invalid header or slot count, mismatched
declared payload size, trailing bytes, or CRC32 mismatch. For
structurally valid data it reports the captured revision, Metal
frame serial, physical/live/dead slot counts, invalid ages,
nonfinite live attributes, inconsistent CPU lifespan mirrors,
and bounding coordinates of finite live particles.

Use the reusable header `engine/fx/SpectraCheckpointInspector.h`
from another native diagnostics frontend:

```cpp
std::vector<uint8_t> fileBytes = /* bounded file input */;
auto report = aiverse::spectra::inspectGpuCheckpointDiagnostic(fileBytes);
if(!report.structurallyValid()) {
    // gpuDiagnosticErrorString(report.error)
} else if(report.hasSemanticWarnings()) {
    // Investigate numeric/VM data; this does not make the file replayable.
}
```

The export is **same-build native ABI diagnostics only**:
`GpuCheckpointExportHeader` V2 is 48 bytes, and the payload is an
array of 128-byte `GpuCheckpointParticle` values plus 4-byte
per-slot remaining CPU lifetime. The CRC covers the **payload**,
not all header fields. Cross-platform, cross-compiler or
cross-version portability is intentionally unsupported. V1 is
rejected, not silently interpreted. No GPU checkpoint import,
rewind or gameplay save exists.

These files may include internal world positions, graph scalar VM
inputs and VFX parameter values. Treat exported diagnostics as
project data; review them before sharing externally.

## One-at-a-time native GPU capture host

The public native header `<aiverselib/genre/SpectraFX.h>`
exports `GpuPoolCaptureHost`. It is renderer-side only and
must be driven on the owning graphics Device thread:

```cpp
GpuPoolCaptureHost captures(pool);

// Between rendering frames, after the pool has been initialized:
bool queued=captures.request(/*groupIndex=*/0);

// On each future graphics frame, after Device.beginFrame delivered callbacks
// and BEFORE any new pool.step:
const auto& state=captures.pollBeforeStep();

// After pool.step(0) in the pre-pass, before Device.beginPass:
if(captures.status().phase==GpuCapturePhase::Queued)
    captures.submitAfterStepBeforePass();

// After a future graphics frame reports Ready:
std::vector<uint8_t> diagnostic;
if(captures.takeReady(diagnostic)) {
    // Already bounded and checked by the offline inspector.
}

// On rollback, pool shutdown or Device recreation:
captures.cancel();
```

In the included `examples/spectrafx/native_ecs_attachments.h`,
the `NativeAttachmentExample` implements these steps itself:
`queueGpuCheckpoint(0)` freezes VFX presentation, the frame
loop completes readback asynchronously without mutating the GPU
particle revision, and `takeGpuCheckpoint(bytes)` releases
the freeze. If the host has advanced the GPU revision in the
meantime, the capture is rejected instead of relabeled fresh.

`GpuPoolCaptureHost` uses a single bounded request, an exact pool
lifecycle epoch and request ID, weak callback lifetime references,
a maximum of 600 presentation-frame polls and a hard payload limit.
It never starts a worker thread or blocks on a GPU fence. A timeout
does **not** cancel a Metal driver's in-flight readback; RHI
readback ownership and Stage 32 budget settlement remain in the
emitter, and a second request may be denied until callbacks settle.
All CPU readback bytes remain diagnostic-only and never enter
authoritative simulation ECS.

**Verification:** Source committed in GitHub only. Native compiler,
CTest, file corruption tests, Metal readback callback cancellation
and editor integration have NOT RUN for Stage 41.

## Stage 42 — privacy-by-default output and explicit report bundles

The offline inspector now **redacts world bounds, exact GPU frame
serials, revisions and content CRC fingerprints by default** in
both text and JSON output. It never prints raw particle positions
or VM scalar inputs. This matters when attaching a diagnostic report
to an issue tracker or sharing with an external AI agent.

```sh
# Default output is safe aggregate counts with no scene coordinates.
./inspect_spfx --json checkpoint.spfx

# Explicitly save a LOCAL privacy-redacted JSON report:
./inspect_spfx --report report.safe.json checkpoint.spfx

# Opt in to coordinate bounds or device identifiers, individually:
./inspect_spfx --json --include-coordinates checkpoint.spfx
./inspect_spfx --json --include-identifiers checkpoint.spfx

# All options can be combined, but raw particle/VM arrays are NEVER
# included in this JSON report by any flag.
./inspect_spfx --report private-debug.json --include-coordinates \
  --include-identifiers checkpoint.spfx
```

The command refuses to overwrite the checkpoint input path,
checks the 540,720-byte input bound before allocating, uses a
temporary output file and a rename to publish the explicitly
requested report. It does **not** upload data, scan arbitrary
directories or create background monitoring tasks. The source
`.spfx` file is itself **sensitive RAW GPU state** and must not be
included in a support bundle unless the user separately chooses
to share it.

For other native programs, use
`engine/fx/SpectraDiagnosticReport.h`:

```cpp
GpuDiagnosticInspection checked=inspectGpuCheckpointDiagnostic(raw);
GpuDiagnosticReportPrivacy privacy{}; // EVERYTHING sensitive off by default
std::string safeJson=gpuDiagnosticReportJson(checked,privacy);
```

The editor's SpectraFX window now supports indexed GPU-group
selection. **Save safe JSON report** writes only aggregate per-slot
counts and a summarized preview history; individual particle
positions, VM scalars, absolute graphics frame serials and source
filenames are omitted unless the user explicitly checks the
separate bounds/identifiers options. The adjacent
**Save RAW GPU checkpoint (sensitive)** action is a distinct
opt-in, not part of safe report generation. Both actions require
a full, fresh, current-revision capture and save only locally.

Native capture selection also supports
`GpuPoolCaptureHost::requestNext(policy)` with
RoundRobin (default), LargestEstimatedGroup, or
MissingOrStalestCheckpoint. This still queues ONE GPU group
request and respects the Stage 32 shared GPU readback budget.
The owner-thread frame loop MUST freeze VFX mutation until the
result is taken or cancelled.

Native recovery events now distinguish an explicit
`PoolRecoveryPhase::ResetRequested` from
`ResetRetryRequired` and `DeviceRecreationRequired`.
`GpuPoolRecoveryHost::requestPresentationReset(reason)` only
requests work; the host later calls `retryPresentationReset()`
inside an active graphics frame outside encoders. Events have
a stable `transitionSequence` and expose device-replacement
attempt counts and whether an authored-effect recovery catalog
is retained. No callback silently creates a graphics Device.

**Verification:** Source/commit readback only in Stage 42.
The CLI is NOT YET compiled or executed on real/corrupt files.

## Stage 43 — local redacted support ZIP and native request mailbox

A **safe diagnostic ZIP** can now be created from a complete V2
`.spfx` checkpoint without loading Metal. This operation is ALWAYS an
explicit local action and the archive contains exactly three fixed-name,
plain JSON files: `manifest.json`, `checkpoint_summary.json`, and
`preview_timeline.json`. No original raw `.spfx` file, full particle
positions, VM registers or sensitive graph content is accepted by the
bundle API. All archive entries are ZIP STORE (no external zip library)
with CRC32 and a **128 KiB** total byte cap.

```sh
# Compile from the repository root; not yet executed for Stage 43.
c++ -std=c++20 -O2 -I. tools/spectrafx/inspect_spfx.cpp -o inspect_spfx

# Explicitly save one local safe ZIP. Regardless of any other CLI flags,
# this ZIP is ALWAYS redacted (world bounds and GPU IDs omitted).
./inspect_spfx --bundle /path/to/support.safe.zip /path/to/source.spfx

# JSON output is a separate explicit action; raw inputs are never
# automatically included as an archive entry.
./inspect_spfx --json --report /path/to/report.safe.json \
  --bundle /path/to/support.safe.zip /path/to/source.spfx
```

The CLI refuses to use the raw checkpoint path as the ZIP output. It
writes the archive through a temporary file and then renames it to the
requested destination. The archive manifest explicitly declares
`redacted: true`, `containsRawCheckpoint: false`,
`containsWorldCoordinates: false`, `containsVmScalars: false`,
`containsGpuIdentifiers: false`, and `uploadedAutomatically: false`.
The offline timeline entry is an empty array; the editor can supply up
to **180 already sanitized samples** instead.

The editor now has a distinct **Save SAFE ZIP bundle** button that
ignores the separate sensitive checkbox settings, never includes
raw GPU diagnostic bytes, and uses a local `safe-bundle-group-N-M.zip`
filename with **no absolute GPU serial in the safe filename**. The
matching safe JSON filename is similarly serial-free. The adjacent RAW
`.spfx` action remains explicit and is NOT part of this safe bundle.

### External render-host capture mailbox

The public native header `<aiverselib/genre/SpectraFX.h>` exports
`GpuCaptureRequestMailbox` from
`libraries/include/aiverselib/genre/SpectraGpuCaptureMailbox.h`.
An editor may attach a **non-owning** pointer supplied by the external
native renderer. The editor can request a named GPU group index,
inspect `Idle / Requested / Claimed / InFlight / Ready / Failed`
status, and request release when it is finished. It cannot invoke
Metal calls or bypass the owner's normal pre-render phase.

A native host can use the existing
`examples/spectrafx/native_ecs_attachments.h` integration:

```cpp
// While NativeAttachmentExample fx is alive, on the SAME owner thread:
editor.setSpectraGpuTelemetry(&fx.poolTelemetry());
editor.setSpectraGpuCaptureMailbox(&fx.captureMailbox());
editor.setSpectraGpuRecoveryHost(&fx.recoveryHost());
// Before native fx/shutdown or destruction, detach these pointers:
editor.setSpectraGpuRecoveryHost(nullptr);
editor.setSpectraGpuCaptureMailbox(nullptr);
editor.setSpectraGpuTelemetry(nullptr);
```

The native example actually services mailbox requests inside
`prepare()` after the host opens its gfx frame. It calls
`GpuPoolCaptureHost::request(group)` and, after frozen
`pool.step(0)`, submits the readback before the scene pass. A
late callback cannot publish into a new host lifecycle generation.
The UI must NOT call `gfx::Device::readBuffer`, `beginCompute`, or
GPU reset directly.

### Bounded retry and recovery notifications

`GpuPoolRecoveryHost::serviceQueuedResetAtSafeFrame(ownerFrameOrdinal)`
can be called from the native render owner, inside an active Device
graphics frame, outside encoders. On a recoverable failure it increases
retry spacing by host frames (up to 32), rather than attempting failed
Metal reset uploads every frame. Its `PoolRecoveryEvent` includes
`consecutiveResetFailures` and
`retryNotBeforeOwnerFrame`. The editor can attach a non-owning
`GpuPoolRecoveryHost` for read-only status and explicitly queue a
VFX-only reset request; Device recreation remains the platform host's
responsibility. An unrecoverably lost Device must not be retried as
an ordinary VFX reset.

All these APIs are presentation-only, genre-neutral, and cannot
modify authoritative ECS state or create background GPU work.

**Stage 43 verification:** GitHub file commits and source inspection
only. The new ZIP writer, CLI packaging, editor UI and actual Metal
readbacks have **NOT YET** been compiled, executed or hardware-tested.
