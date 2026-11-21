# SpectraFX Stage 41 — offline checkpoint inspector and native capture host

Date: 2026-11-19
Branch: feature/universal-entity-inspector-fps
Starting HEAD: d5fab8bee77ed41aa8d6bdf321c347deab2ae2ba
Last code/docs HEAD before handoff: fa3af3fdca4c280a21ffb374b1d512a83a670981
Main HEAD checked: 9ee6ff6fbf390bd45363bc92c838f492dfe92eee. Main was already fully contained in feature (feature ahead 577, behind 0); no merge, rebase, force push or main mutation.

## Completed implementation with exact commits

- fd61ee4e43451d2c004f0989b174fc1e40182331 — NEW engine/fx/SpectraCheckpointInspector.h: bounded offline V2 parser; explicit bad-magic, unsupported-version, bad-header, bad-slot-count, truncated/extra data, length mismatch and CRC errors; reports live/dead GPU slots, bad ages, nonfinite values, lifespan mismatch, finite bounds.
- f91b5cef14abacdb3c876cf673cc5caf9663cbf9 — NEW tools/spectrafx/inspect_spfx.cpp: one-file C++20 CLI, plain/--json output, checks file size <=540720 bytes before allocation, detects file changes during read, exit codes 0 valid V2 / 1 corrupt format / 2 usage or I/O failure.
- 304ff270524abf68e1dab5d2a8699588a31d59c0 — inspector fixes NaN GPU ages, including values that otherwise would have been classified dead.
- a5f5f01e05693efa752bd1e6e3db832c97296936 — NEW tools/spectrafx/README.md: native compilation command, parser details, byte limits, host usage and diagnostic data privacy warning.
- 339bea5dd05e3bd54cd67abb810c62266df3ea56 — engine/fx/SpectraGpuPoolWorld.h: lifecycleEpoch() accessor for async result epoch matching.
- 56a31e80a26b61cccb66bee9c590e0d8cb6337dd — NEW libraries/include/aiverselib/genre/SpectraGpuCaptureHost.h: one-request bounded async GPU capture state machine, owner-thread safe-point submission, before-step completion polling, complete+fresh V2 payload validation, weak callback state, group and byte cap, explicit cancellation.
- 9199178627dcd9171190d4ca4326fdf8e5614a11 — capture host binds callback state to exact request ID, avoiding lost-current or stale-old result.
- a35d237a4e3289852c130ad84d8da9916d1916c7 — capture host adds 600 rendered-frame poll timeout and allocation-free destructor callback invalidation.
- fa27fca3a1cefa18694984eaa0016ac30b1e6ec3 — libraries/include/aiverselib/genre/SpectraFX.h exports capture host.
- a3a513b42418736a57d80eadf89f36a8806cde3b — examples/spectrafx/native_ecs_attachments.h adds queueGpuCheckpoint, gpuCaptureStatus, takeGpuCheckpoint, cancelGpuCheckpoint and the actual renderer host frame lifecycle integration.
- 51c94d5d712ef6d0c00b9b4babb7c003b7146e61 — sample declares vector/utility includes.
- 7f7f53972fb066b02b30960dbbf61e7de8393e11 — engine/fx/SpectraGpuPoolWorld.h adds boundToDevice() identity check.
- 83dd0a81f094b9dbfe6822b62c1f24749065cce3 — libraries/include/aiverselib/genre/SpectraGpuRecoveryHost.h rejects replacing an active graphics Device with the very same old gfx::Device object.
- ce603ad3c2ec1c6e04720e45a89b5d3026a6d3c8 — examples/spectrafx/native_ecs_attachments.md documents capture order and offline tool.
- 88106143cc0de5e726bff3d0b9ad70f7a4dfff5e — docs/spectrafx.md public Stage 41 APIs.
- df49e77869bf508645eed8e10ad866a63d37386b — skills/visuals/aiverse-spectrafx/SKILL.md updated agent guide.
- fa3af3fdca4c280a21ffb374b1d512a83a670981 — AGENTS.md current stage and renderer/ECS ownership rules.

## API and lifecycle contract

Offline CLI (example command, NOT executed in this stage):

    c++ -std=c++20 -O2 -I. tools/spectrafx/inspect_spfx.cpp -o inspect_spfx
    ./inspect_spfx --json path/to/checkpoint.spfx

Format: V2 native diagnostic ABI, 48-byte header, up to 4096*128 bytes particle/VM payload and 4096*4 bytes CPU lifespan mirrors; exactly 540720 bytes maximum. CRC covers payload, NOT entire header. No V1 migration or portable savegame support. This tool does not open Metal, play visual effects, mutate ECS or restore live GPU buffers.

Native C++ API via <aiverselib/genre/SpectraFX.h>:

    GpuPoolCaptureHost captures(pool);
    captures.request(0); // between graphics frames
    // At each frame: Device.beginFrame() -> captures.pollBeforeStep()
    // Frozen VFX presentation: pool.beginPresentationFrame -> pool.step(0)
    // -> captures.submitAfterStepBeforePass() -> scene pass/draw
    // -> pool.endPresentationFrame -> Device.endFrame()
    std::vector<uint8_t> diagnostic;
    if(captures.takeReady(diagnostic)) { /* offline analysis only */ }
    // Before rollback, Device recreation or pool.shutdown:
    captures.cancel();

Only one readback request at a time. Each callback holds a weak pointer to per-request state, not the owner/pool pointer. The controller matches request ID and exact pool lifecycle epoch; it checks completed current GPU revision before publishing a bounded, CRC-validated blob. It stops polling after 600 graphics-frame checks. This is owner-thread driven, never a background task or a synchronous GPU readBuffer call.

The native sample preserves checkpoint freshness by freezing VFX presentation and skipping render-pose/ECS source updates while capture is Queued/InFlight/Ready. If a graphics Device is replaced or rollback occurs, the capture is cancelled first. A late RHI callback cannot publish into the new request, but an already submitted Metal readback is not forcibly cancelled; Stage 32 budget settlement still belongs to RHI callbacks.

Graphics Device recovery must create a DISTINCT new healthy device. The old pool is destroyed while its previous Device remains alive; only CPU effect definitions and defaults are reimported, never old GPU particles/source handles. No new authoritative ECS mechanics or genre dependencies were introduced.

## Checks actually performed

- GitHub branch/main ancestry lookup and source interface inspection.
- SHA-guarded source writes; re-fetched the new inspector, CLI, README, public header, capture host and ECS example to verify paths and references.
- Source review caught/fixed missing callback-state request ID and NaN-age diagnostic.
- NOT RUN: CMake/Clang C++ compilation, ObjC++/MSL compilation, CTest, CLI invocation against valid/corrupt files, editor startup/screenshot, on-device Metal readback/fence/cancellation, GPU Device-loss recovery, CPU/GPU parity and performance benchmarks.

Status: SOURCE COMMITTED; EXECUTION NOT VERIFIED. Do not claim build/tests passed.

## Current limits and concrete next stage (42)

1. Add explicit privacy-safe diagnostic bundles/reports with redacted world coordinates and VM scalar inputs; never upload private debug data automatically.
2. Support configurable capture selection for multiple GPU groups while retaining one readback per native host and sharing Stage 32 GPU bandwidth budget.
3. Improve native renderer recovery UX with clear reset/lost-device/retry status events, without pretending to restore live GPU particles.
4. When features-first concludes: compile native Mac C++/ObjC++/MSL, run existing SpectraFX CTest, test offline V2 invalid-version/truncation/CRC, test async callback cancellation and source/recovery epochs on real Metal. Fix discovered problems before merging to main.
5. After each further feature stage keep a separate handoff documenting changed files, exact SHAs/branch, API, actual tests and limitations. Preserve other agents work and never force-push.
