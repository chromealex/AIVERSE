# SpectraFX Stage 42 — privacy-safe diagnostic reports, selected GPU groups, explicit host recovery events

Date: 2026-11-19
Working branch: `feature/universal-entity-inspector-fps`
Starting HEAD: `14eae2b5d1bc07c0fcdcd8cc8c2601f1c88d332e`
Latest observed implementation HEAD before handoff: `99dde12f453fcbe9fe27fba1e7b0203bb636d180`
Main checked: `9ee6ff6fbf390bd45363bc92c838f492dfe92eee`. Main was already fully contained (feature 595 commits ahead, 0 behind); no merge/rebase/force push or main mutation.

All changes were SHA-guarded GitHub connector writes, not a local checkout. There is NO claim of compilation or GPU execution.

## Exact commits / changed files

### Privacy-redacted, explicit-action diagnostics

1. `d12ef8fd8b1d8711d5f8838d669b0b65177f3abc` — **new** `engine/fx/SpectraDiagnosticReport.h`: fixed-schema `gpuDiagnosticReportJson(inspection,privacy)` producing aggregate-only JSON. No raw particle positions, VM/program inputs, original file name, scene-coordinate bounds or exact GPU serial/revision/CRC by default. Bounds and identifiers are separate explicit booleans.
2. `0cdaf9e782c72e053b8cc572f8b097bf195ad371` — `tools/spectrafx/inspect_spfx.cpp`: default redaction for plain and `--json`, explicit local `--report <safe.json>`, independent `--include-coordinates` / `--include-identifiers` opt-ins, bounded input and temp-file publication. Never auto-upload or export raw GPU payload to JSON.
3. `8e88f9afb360b957cdf03b60f75f8ce03019afed` — `apps/editor/Editor.h`: indexed raw GPU checkpoint saver plus separate redacted report API.
4. `b76791cf22bac27c5d85aea868843b8a752a7cbc` — `apps/editor/Editor.cpp`: select exact GPU group or preserve legacy auto-selection; export safe aggregate inspection JSON and sanitized preview timeline (sample ordinal/counts/status, no per-frame GPU serial, no VM or coordinates), explicitly write local JSON through temp+rename under `userDataDir()/spectrafx-diagnostics/`.
5. `4ccafe6460f60329e1fbdaa9a38ec2c57e402a59` — `apps/editor/windows/SpectraGraphWindow.cpp`: selected group control, group freshness/status, separate **Save safe JSON report** and **Save RAW GPU checkpoint (sensitive)** controls, explicit sensitive-bounds/GPU-identifiers opt-in checkboxes.
6. `cdc6469c488211c6b56462bb99d28dc9715d98f5` — same editor window: reset both sensitive opt-in booleans whenever group selection changes.

**Privacy contract:** The raw binary V2 `.spfx` is **sensitive** because it contains world-space positions and per-slot GPU VM scalar data. A safe report is a sanitized aggregate JSON, **never** a rewritten or redacted copy of the raw checkpoint. By default it omits exact GPU serial/revision/CRC, original paths and world-coordinate bounds. Individual min/max bounds and identifiers require separate explicit consent, and even opting in NEVER exposes per-particle positions or VM scalar arrays. Files are written only on explicit user actions; no automatic uploads, background monitoring or network requests. JSON describes same-build diagnostic data, not ECS save/rollback or GPU replay.

### Group-local status, source scheduling and capture policy

7. `ba61d7a3869bdc96c06a73454aa323fa5ddb88f2` — `engine/fx/SpectraGpuPoolWorld.h`: `PoolGpuGroupSummary`, `gpuGroupCount()`, `gpuGroupSummary(index,out)` read-only public API. It intentionally excludes GPU group shader key/serialized graph names and raw VM values.
8. `4ea83dab8c7a44605c6759e5978a752aeb23ce07` — `engine/fx/SpectraGpuPoolWorld.cpp`: group summary: Metal active, estimated physical particles, GPU resource byte estimate, latest successful step, previous reset acceptance, full checkpoint status and current export readiness. Invalid index returns false without mutating output.
9. `7314176a3b5e54c93da6f014cc208054814b07f8` — `libraries/include/aiverselib/genre/SpectraGpuCaptureHost.h`: `GpuCaptureTargetPolicy::{RoundRobin,LargestEstimatedGroup,MissingOrStalestCheckpoint}`, `requestNext(policy,maxBytes)`, skips already pending groups and still admits at most ONE request per host (Stage 32 shared readback limiter remains enforced by emitter at submission).
10. `99dde12f453fcbe9fe27fba1e7b0203bb636d180` — same capture host: `takeReady` rechecks that the pool is alive, matches exact lifecycle epoch and that the selected group still has exportable checkpoint at the captured revision. A Ready result can no longer be taken after a later GPU mutation/reinitialization.

**Capture contract:** `request(groupIndex)` remains available. For one-at-a-time routing, call `requestNext(policy)` between graphics frames; native host freezes ECS VFX presentation, calls `pollBeforeStep()` after `Device.beginFrame` and before any new `pool.step`, then calls `submitAfterStepBeforePass()` following frozen `step(0)`. A callback cannot publish stale data to the new request; a cancelled RHI readback still owns its underlying callback/reservation until settled. Stage 35 GPU source kill-completion fence, Stage 32 readback bandwidth limit and Stage 40 checked GPU staging must remain intact.

**Editor limitation:** The selected non-owning **external** GPU telemetry pool may be exported only when its own native renderer has produced a fresh checkpoint. The editor does NOT schedule compute/readback against an external graphics owner. The editor's built-in opt-in GPU preview schedules only its own group-0 capture.

### Recovery event/notification improvements

11. `f59e7f3cb9d2b1cf545df5450141a35794d90021` — `libraries/include/aiverselib/genre/SpectraGpuRecoveryHost.h`: `PoolRecoveryPhase::ResetRequested`, `requestPresentationReset(purpose)` (queues intent, no RHI command), stable incrementing `PoolRecoveryEvent::transitionSequence`, counts of explicit resets and Device replacement attempts, pending/reset recipe flags; status differentiates queued, retry-required, device-recreation and ready.
12. `816d918b8a9839b4f873816353df86d194272e00` — recovery host: reject a new reset request if a lost/nonrecoverable Device requires graphics recreation rather than masking loss.
13. `1e2e4a72fe965fdc1e1a67495adfa6205473a31a` — `libraries/include/aiverselib/genre/SpectraFX.h`: expose the redacted reporting API through the standard native include.
14. `fe6339aa4be1432423144a15da08eea907b94480` — `examples/spectrafx/native_ecs_attachments.h`: explicit queued visual reset/status; selected or automatic one-group capture; separate `takeSafeGpuDiagnosticReport(json,privacy)` and `takeGpuCheckpoint(rawBytes)` output paths.

**Recovery contract:** The event callback reports status changes but never recreates a platform `gfx::Device`. `requestPresentationReset()` only marks a request and blocks normal native sample presentation through `recovery.poll()`. Host later calls `retryPresentationReset()` in an open gfx frame, outside graphics/compute encoders; a failed reset remains retry-required. Device loss requires a distinct newly created healthy Device while keeping the old one alive through old-pool shutdown. New device loads CPU-only authored effect definitions, NEVER live particle buffers or old ECS renderer source IDs. The monotonic transitionSequence remains unchanged across repeated equivalent polls; it increments when observable status/progress/attempts change.

### Documentation and next-agent instructions

15. `9c6db5e2d10283e284dbef10819699446c686956` — `tools/spectrafx/README.md`: explicit privacy defaults/flags, local report outputs and capture policy.
16. `6fee315804bde6f282d46262ff57c525c793b4e8` — `examples/spectrafx/native_ecs_attachments.md`: sample safe vs sensitive report, per-group capture and queued recovery action.
17. `5f8455aec709136fbcc391b85c2dd235929f0364` — example README: clarify taking a Ready capture consumes that result; another privacy-option request needs another completed GPU capture.
18. `4c8b14aeef0888770de8a040d2b7dd8f75f93f42` — `docs/spectrafx.md`: public Stage 42 API and privacy boundaries.
19. `68b87de1840238211d36f9832d51e20c73f45bd0` — `skills/visuals/aiverse-spectrafx/SKILL.md`: agent integration and no-raw-data defaults.
20. `a11d08983ccaa0b1fd9f77fef3257233177e986f` — `AGENTS.md`: latest Stage 42 ownership, safety, handoff pointer.

## Tests and checks ACTUALLY performed

- Read current main and feature branch heads; confirmed main already ancestor, no merges/rebases needed.
- Inspected Stage 41 V2 inspector, bounded CLI, async capture callback code, pool group stats, native ECS presentation adapter, editor UI and graphics recovery event logic.
- All above commits were accepted through SHA-guarded GitHub file writes, followed by file refetch verification of 16 key source/doc files.
- Source review caught/fixed sensitive option flags persisting after GPU group changes and the possibility of taking stale bytes after an already Ready capture.
- **NOT RUN:** native C++/ObjC++/Metal build or link, offline CLI execution on V2 valid/corrupt/truncated files, CTest, editor startup/UI screenshots, on-device Metal multi-group capture, RHI readback budget/fence correctness, simulated device-loss rollback, privacy audit with real project paths, GPU/CPU parity or performance benchmarks.

**Status: SOURCE IMPLEMENTED AND COMMITTED; EXECUTION UNVERIFIED.** Do not claim passing builds, correctness on real Metal hardware, safe external sharing of raw SPFX binaries or production readiness.

## Known limitations / risks

1. Safe JSON reports still contain aggregate counts and optional exact GPU group ordinal. This is suitable for internal triage, not a rigorous public-data anonymity guarantee. Check project data-handling policy before sharing; VM program scalar inputs and raw coordinates are never emitted by report helper.
2. Stage 42 opt-in bounds can reveal scene geometry; exact GPU serial/revision/CRC can correlate captures. Both are disabled by default and cleared when the selected editor group changes.
3. One native host still has one outstanding capture. No bulk automatic multi-group scanning or GPU readback storm is introduced. On-device shared RHI and Stage 32 limiter can still reject an otherwise valid request under pressure.
4. Recovery request can be queued before graphics-frame creation but must not run checked GPU reset outside the active graphics safe point. Stage 38 multi-group reset is restartable preparation, not hardware-atomic restoration, and may require host graphics Device replacement.
5. The editor still stores raw checkpoints via a separate explicit action; those files contain sensitive positions and VM inputs. The new JSON summary doesn't sanitize/change those raw files in-place.
6. GitHub source readback does not prove compilation, renderer UI behavior or async callback health. Must run native builds/tests before main merge.

## Concrete next Stage 43

1. Add optional diagnostic ZIP/bundle packaging ONLY after explicit user action, with a manifest listing privacy flags and ensuring no raw V2 payload or original file path is included by default. Never auto-upload.
2. Extend UI selection to an explicit per-GPU-group on-demand capture request interface provided by native render host, preserving non-owning editor telemetry and safe pre-render scheduling.
3. Harden failed/missing capture UI and queued reset status with non-blocking retry notifications; preserve stable event sequence and avoid event feedback loops.
4. When feature work is complete, compile C++/ObjC++/MSL, run CTest, offline V2 parser redaction/CRC tests, real Metal one-group/multi-group readback/fence tests, source generation rollback and device-loss tests. Fix failures before merging into main.
5. Continue one separate dated handoff per stage, include exact source SHAs/branch, APIs, tests ACTUALLY RUN vs NOT RUN, limits and next action. Preserve concurrent agent edits; never force push.
