# SpectraFX Stage 43 — privacy-safe ZIP support bundle, native capture mailbox and bounded GPU reset retries

Date: 2026-11-19
Branch: `feature/universal-entity-inspector-fps`
Stage 42 starting HEAD: `d6cfde41fb2b49da90c512272e90956b5a6404e3`
Last Stage 43 source/documentation commit before handoff: `f56bdb3c18d9ea63d64b3c246216a602d24c1689`
Main head at start: `9ee6ff6fbf390bd45363bc92c838f492dfe92eee`, already ancestor of feature (0 missing main commits, feature 616 ahead). **No merge/rebase, force push or main mutation.**

All changes were SHA-guarded GitHub connector source edits. No local repository checkout, CMake build or actual GPU device execution occurred.

## Changed files / exact commits

### Fixed-schema, privacy-safe ZIP support package

1. `4c01d3b423939ddcdd73849ad935969ef7d65b27` — **new** `engine/fx/SpectraDiagnosticBundle.h`: bounded in-memory ZIP STORE serializer (no zlib): three fixed entries `manifest.json`, `checkpoint_summary.json`, `preview_timeline.json`. Uses CRC32, static file names, <=180 sanitized `GpuSafeTimelineFrame` samples and <=128 KiB total. Does NOT accept raw .spfx bytes, arbitrary entry names, graph source paths, per-particle positions or VM scalars. Returns false without replacing output if invalid/oversized.
2. `d811485a6fafa88155120d819245ba80d7b27ef9` — `apps/editor/Editor.h`: explicit `saveSafeSpectraGpuBundle(out,groupIndex)` API.
3. `73af129ca585b9ebb5f4036f8d8dcc4439421ced` — `apps/editor/Editor.cpp`: local ZIP publication using a current-revision, CRC-valid GPU checkpoint, privately generated manifest/summary and sanitized samples from editor-owned preview only; temp-file+rename. No automatic upload or raw diagnostic payload.
4. `988ab74b2fb61adce9f45253ee39b5660d428ada` — `apps/editor/Editor.cpp`: privacy-safe filenames use `safe-bundle-group-N-M.zip` / `safe-report-group-N-M.json` with numbered local suffix, **not GPU frame serial**. Do not overwrite prior safe reports; avoid mixing editor preview history with an unrelated native external GPU telemetry pool.
5. `c4252d5b30dde4b0c2bc06a1133dc90381cbdc60` — `tools/spectrafx/inspect_spfx.cpp`: explicit offline `--bundle <safe.zip>` CLI option, independent of any `--include-coordinates`/identifiers flags; ZIP always redacted with empty sanitized timeline for offline use.
6. `b1993573a91dd8a521f1a65983f7c91ee7a7e3ba` — ZIP header `engine/fx/SpectraDiagnosticBundle.h`: explicit `<cstddef>` include for standalone API correctness.
7. `40abd3b9f49a6e96b5d653fce11a898599c98f44` — `apps/editor/windows/SpectraGraphWindow.cpp`: explicit **Save SAFE ZIP bundle** action, distinct from **Save RAW GPU checkpoint (sensitive)** and the optional sensitive-bound JSON report.
8. `3784bf175e7fcf77eb79ed5119d5974b2e6ae333` — same graph window: reset sensitive bounds/identifier consent flags and group selection when the attached GPU pool identity changes, not only when the group index changes.

**Privacy:** The ZIP builder unconditionally passes default redaction policy to `gpuDiagnosticReportJson` and formats only numeric/boolean sanitized frame fields. No raw .spfx file, original graph name/file path, world bounds, absolute RHI serial/revision, content fingerprint, per-particle position, VM scalar array or network/upload action is present. The ZIP's `manifest.json` declares all these exclusions. The raw V2 `.spfx` remains sensitive and must be separately requested, never included in safe ZIP.

**Native API:**

```cpp
#include "engine/fx/SpectraDiagnosticBundle.h"
auto info=spectra::inspectGpuCheckpointDiagnostic(rawSpfxBytes);
std::vector<spectra::GpuSafeTimelineFrame> safeFrames;
std::vector<uint8_t> safeZip;
const bool ok=spectra::makeSafeGpuDiagnosticZip(info,safeFrames,safeZip);
// output contains three fixed JSON ZIP-STORE entries, <=128 KiB, no raw SPFX.
```

Offline CLI example (NOT executed here):
```sh
c++ -std=c++20 -O2 -I. tools/spectrafx/inspect_spfx.cpp -o inspect_spfx
./inspect_spfx --bundle /path/to/support.safe.zip /path/to/scene.spfx
```
The CLI writes to one explicitly supplied local path. Offline timeline entry is empty. ZIP validity/extraction has NOT been executed.

### External editor → native graphics owner capture mailbox

9. `86665b347c7a42a910bb84ab244e9bcde4cdd9f0` — **new** `libraries/include/aiverselib/genre/SpectraGpuCaptureMailbox.h`: single-owner-thread, non-GPU request mailbox with `Idle/Requested/Claimed/InFlight/Ready/Failed/ReleaseRequested` phases, monotonic request sequence, target group index, GPU result status, bounded error string, explicit release. The mailbox never issues RHI commands or touches gameplay ECS.
10. `2cc313a5e343c306887e3ce5b65a0337d53899b1` — `libraries/include/aiverselib/genre/SpectraFX.h`: publishes mailbox from native-project umbrella.
11. `07fe6700f3cf0f02f4abae7a393f02d707f10603` — `examples/spectrafx/native_ecs_attachments.h`: host consumes mailbox before its scene render pass, calls the existing one-at-a-time `GpuPoolCaptureHost` and publishes async status. Release is handled at a later owner-thread graphics frame. All capture state is cancelled and mailbox cleared before rollback, Device reconstruction and shutdown.
12. `9d72bee37aabcc42005b8ac9f7956cf9ba2f3c3e` — `apps/editor/Editor.h`: optional NON-OWNING `setSpectraGpuCaptureMailbox`, request/release API and pointer; external native host MUST detach pointer before destruction.
13. `7260f8122b88898b5604d28c36edefb7a0e9058a` — `apps/editor/Editor.cpp`: UI posts external native capture intent only; refuses requests when viewing editor-owned GPU preview or invalid group.
14. `40abd3b9f49a6e96b5d653fce11a898599c98f44` — `apps/editor/windows/SpectraGraphWindow.cpp`: native mailbox status, selected group request, release control; no Metal readback/compute/reset triggered from ImGui.
15. `a469ce5e2347e6f3c055b22cb36967c69c7f5c07` — mailbox header explicit utility include.
16. `4c6560602b83b07451058b608241fc985c54a941` — native example `poolTelemetry()` and `recoveryHost()` non-owning accessor integration.
17. `eeb313a31ae0c603c7890398dfe84cc7f208e910` — example calls bounded recovery safe-frame service, and clears capture mailbox state after native capture result consumption.
18. `cb3dfb870f95f3edaf1ef1d957fea33ce69d1994` — `examples/spectrafx/native_ecs_attachments.md`: connection/detach contract and sample host frame sequencing.

**Thread/lifetime contract:** The editor and the native graphics owner must interact with the mailbox only on the SAME owner thread. Editor `queueSpectraExternalGpuCapture(index)` writes intent; the host checks the queue after Device.beginFrame, before pool.step; at safe compute-to-render boundary it submits a bounded async readback; the editor later sees Ready/Failed. UI release is only an intent; the host consumes and cancels at its next safe frame. All GPU uploads/readbacks, Stage 32 shared bandwidth, Stage 35 completed-frame kill fences and Stage 40 checked input staging remain owned by native RHI. Non-owning pointers for pool, mailbox and recovery host MUST be detached before host/pool/device destruction.

### Bounded safe-frame recovery retries and editor notifications

19. `5b446ecf47391852cc91b5c0a3548c0b1cb69a12` — `libraries/include/aiverselib/genre/SpectraGpuRecoveryHost.h`: optional `serviceQueuedResetAtSafeFrame(ownerFrameOrdinal)` return enum NoWork/Deferred/ResetSucceeded/ResetFailed/NeedsNewDevice; bounded exponential retry spacing (up to 32 **native owner frames**, no timers, GPU spin or background work); `PoolRecoveryEvent::consecutiveResetFailures`, `retryNotBeforeOwnerFrame`.
20. `038c45977d6a87deb8ed540977708f1c8361fa27` — same recovery host: avoid duplicate intermediate callbacks when safe-frame reset is serviced and cooldown subsequently updated.
21. `c993dabe4ee985255bed646af8a92dd73bcfd9bb` — same header: portable numeric-limit handling for retry frame saturation.
22. `eeb313a31ae0c603c7890398dfe84cc7f208e910` — native example calls retry service before normal presentation at owner safe point; resets capture/mailbox before attempting rollback.
23. `a165adec7054b90c10d670fe877a155f478a2b42` — `apps/editor/Editor.h`: optional NON-OWNING `setSpectraGpuRecoveryHost`, status access and UI-only `queueSpectraExternalVisualReset`.
24. `fdac5c375eceb741283a02ebd0d13f58b711a08c` — `apps/editor/Editor.cpp`: editor requests reset intent, NEVER does GPU reset itself.
25. `8e3617fb3dd5696a0c16483ef5cd9a4b36f8bd2d` — `apps/editor/windows/SpectraGraphWindow.cpp`: show native recovery action, transition sequence, consecutive failures, next eligible owner frame, explicit queue button and lost-device warning.

**Recovery contract:** The host calls `serviceQueuedResetAtSafeFrame(ownerFrameOrdinal)` AFTER `gfx::Device::beginFrame` and OUTSIDE all Metal render/compute encoders. A failed recoverable reset spaces retries up to 32 host frames, rather than submitting failing GPU writes every frame. It is not an atomic multi-group GPU transaction. `NeedsNewDevice` still requires the platform to create a DISTINCT healthy gfx Device, preserve the old Device through GPU-pool teardown, and reconstruct effect definitions from CPU-only manifest. Living GPU particles and old renderer source generations are deliberately lost. No UI control executes RHI commands.

### Documentation and AI ownership

26. `1bc992a01c657b26f4189c6bdc29a6bdd34558c9` — `tools/spectrafx/README.md`: safe bundle CLI/size/privacy limits and native mailbox ownership contract.
27. `cb3dfb870f95f3edaf1ef1d957fea33ce69d1994` — `examples/spectrafx/native_ecs_attachments.md`: complete native/editor mailbox wiring and cleanup, owner-frame bounded retry.
28. `d1abfcad6dba237f16fb281ad9ad435030fa43b3` — `docs/spectrafx.md`: Stage 43 public API and limitations.
29. `994f18acc2cf37f7aedba984ff62f052f6575df3` — `skills/visuals/aiverse-spectrafx/SKILL.md`: agent usage and privacy rules.
30. `f56bdb3c18d9ea63d64b3c246216a602d24c1689` — `AGENTS.md`: latest Stage 43 pointer, staged ownership, explicit NOT RUN status.

## Tests and source checks actually completed

- Read and verified feature branch HEAD and main ancestry at Stage 43 start; main was already fully included.
- Inspected Stage 42 privacy report, V2 checkpoint parser, editor source/window, native capture host, recovery host and sample.
- Source-level reviews of the new ZIP writer (ZIP STORE local/central directory structure, fixed JSON filenames, length/CRC and <=128 KiB), non-owning editor pointers and mailbox/request sequence state.
- Re-fetched new ZIP header and native sample during implementation; adjusted safe report/ZIP filenames to exclude GPU frame serials and prevented unrelated editor history entering an external pool's diagnostic.
- Fixed header includes, reset sensitive report consent when GPU pool changes, and removed duplicate callback notifications for bounded retry service.
- **NOT RUN**: any local or CI C++ compile, ObjC++/MSL shader compilation, CTest, ZIP extraction/integrity checks using unzip, running the offline CLI, native editor launch/screenshot, on-device Metal capture, multi-group readback queue/fence stress or Device-loss reset fault injection. No performance measurements.

**Status: CODE AND DOCS COMMITTED, NATIVE EXECUTION UNVERIFIED.** Do not claim ZIP correctness, graphics recovery, thread safety across different OS threads or GPU performance based only on commits. Test and correct errors before enabling by default or merging to main.

## Known limitations and Stage 44 handoff target

1. The ZIP is fixed-schema, uncompressed and <=128 KiB, intentionally not a general-purpose archive. Full raw .spfx remains a separate sensitive export; no user path/symbol/VM data are allowed in safe ZIP.
2. The native UI mailbox is single owner-thread only. UI↔render threads running concurrently need a separate synchronized command queue designed by the native application, NOT sharing this unsynchronized mailbox across threads.
3. The non-owning editor pointers can dangle if the host forgets to detach before pool/mailbox/recovery destruction; native sample and README document explicit detach ordering. Device recreation cannot be invoked inside ImGui.
4. The retry service tracks host frame ordinal, not real time. A stopped frame pump neither retries nor reports a new state. A device requiring recreation must be handled by native platform graphics logic; in-flight RHI callback budget settlement may outlive the host capture request.
5. The Stage 32 async readback/Stage 35 kill quarantine and Stage 40 checked GPU staging paths still require real hardware validation. Partial reset upload acceptance does not imply full Metal GPU command completion.
6. Source stage's ZIP builder, CLI, UI and mailbox are NOT compiled or run. Do not claim they function on all platforms yet.

**Next Stage 44:** finish any remaining functional admission/status logic, inspect new C++ source for mismatched headers, then switch to native validation: compile standalone SPFX CLI, run ZIP integrity and privacy tests, native C++/ObjC++/MSL, CTest, editor/Metal smoke, fault-injected in-flight checkpoint cancellation, mailbox owner-thread safety and device-loss/rollback simulation. Preserve date-specific handoff, exact tested status, branch/commit and no force pushes.
