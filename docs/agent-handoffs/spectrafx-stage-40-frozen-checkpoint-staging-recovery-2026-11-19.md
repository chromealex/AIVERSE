# SpectraFX Stage 40 — frozen GPU checkpoints, checked staging retry, V2 CRC and recovery hysteresis

Date: 2026-11-19
Working branch: `feature/universal-entity-inspector-fps`
Starting HEAD inspected: `72a8a68511cfefcd0d78fd8946060e1a50f669ee`.
Main HEAD inspected: `9ee6ff6fbf390bd45363bc92c838f492dfe92eee`.
At the start, main was already fully included in the working branch (0 commits behind main, 554 feature commits ahead). No merge/rebase, force-push or main modification was needed/performed.

Latest source/documentation commit before this handoff: `ba0f1cd3f18d5309b575216ddb8e6a49ed671c38`.

## Changes made, with exact commits

### 1. True zero-dt GPU freeze, asynchronous checkpoint capture

- `05de06cceb83b699c24c90cfa11e9a8f79106692` — `engine/fx/SpectraGpuFramePlan.h`: optional `integrate` flag, default true. GPU frozen frames can skip Update VM while still gathering camera-facing vertices, executing atomic compaction and generating indirect draw arguments.
- `47da65d6088f5af4a1f09d929e57aa75a28f6a28` — `engine/fx/SpectraGpuParticles.cpp`: `step(0)` without births, source kills or live VM parameter updates now skips Update VM and does NOT increment `vmFrameRevision_`. New birth/kill/VM mutation, nonzero dt or dirty staged data still invalidates the old revision on successful submission.
- `86388c14415559adac114e31b5835f741cbe2fce` — `engine/fx/SpectraGpuPoolWorld.h`: `requestGpuCheckpoint(groupIndex, callback)`, an opt-in nonblocking per-group capture API.
- `f081298967e6af36dfe96fd8d2a1ed53ba3a011d` — `engine/fx/SpectraGpuPoolWorld.cpp`: checks an initialized, healthy, Metal-active group before delegating to Stage 32/Stage 30 async chunked readback.
- `2509ed5bfc18388f3729bda6e3c8193932e10d68` — `apps/editor/Editor.h`: `requestSpectraGpuPreviewCheckpoint` and `SpectraGpuCaptureState::{Idle,Queued,InFlight,Ready,Failed}` plus status note.
- `e08add97a551a4c07992873220b82a7bf4401fe8` — `apps/editor/Editor.cpp`: freezes preview, cancels queued forward-step requests, schedules checkpoint on safe pre-pass graphics frame or waits for an existing one, and only marks ready when the retained revision is complete/fresh; clears state on preview replacement/restart/stop.
- `8a19ffc2bde5056db4938e17c2b7c08de209e2f2` — `apps/editor/windows/SpectraGraphWindow.cpp`: **Freeze & capture GPU state** control and status message.

**Semantics:** A returned async callback `complete=true` indicates all requested buffer chunks arrived, NOT that the readback is fresh after later GPU simulation. Only `GpuCheckpointStatus::restoreEligible` and `PoolCheckpointStats::exportReadyGroups` (same current revision, no pending readback/births/kills/fences) authorize binary export. Frozen GPU frames can still update camera-facing vertices without changing particle+VM registers. If dt>0 or any births/kills/VM edit occur, revision advancement resumes. The request is explicitly opt-in; there is no background readback.

### 2. Checked Metal input uploads, recovery after partial staging failures

- `be5e72af5522364762aa122e2627223fef22efbd` — `engine/fx/SpectraGpuParticles.h`: `gpuStagingDirty_`, status `stagingRecoveryPending`, `gpuStagingUploadFailures` counter.
- `7f874368f655ad2d391bf55758084cd4c78f2f7d` — `engine/fx/SpectraGpuParticles.cpp`: checked storage transfers for VM error counter, VM live parameters, spawn state/slot IDs/VM seed params and live gather indices; if a later transfer or compute encoder fails, retain unsent births, keep CPU-owned physical slots allocated, suppress drawing, mark staging dirty and invalidate the prior checkpoint; retry all inputs on the next safe GPU frame. Input dirty flags clear only after successful ordered compute submission.
- `189fb8b09e55309f07089492b374f8418250e7ff` — `engine/fx/SpectraGpuPoolWorld.h`: `stagingRecoveryGroups`, aggregate `gpuStagingUploadFailures`.
- `73649bf257c127e6303ed1b0f476ad8fbf357ae3` — `engine/fx/SpectraGpuPoolWorld.cpp`: aggregate staging-recovery diagnostics.
- `fae1ca4d296aaeb020edab09e3af13af4a6135f6`, `b2da042fa2b00dbe93ffd6a4c4e6d390cea16ab1` — `engine/fx/SpectraGpuParticles.cpp`: VM error readbacks are also rejected until staging or reset state is consistent.

**Semantics:** Checked RHI methods report command-encoding acceptance, not hardware completion. Previously accepted blit uploads are not rolled back transactionally. The renderer keeps the queued source births/CPU slot mirror, blocks stale draw/checkpoint/error diagnostics and retries all relevant input transfers before any new particle simulation becomes visible. A fully unhealthy graphics Device still requires explicit reconstruction via the Stage 38/39 recovery host. The Stage 35 source kill fence quarantine remains unchanged.

### 3. Diagnostic binary V2 with CRC32 validation

- `a277ce642ff4dddc8c72c68bba56e950986e4a6f` — `engine/fx/SpectraCheckpointExport.h`: replaces Stage 39 **40-byte V1 header** with **48-byte V2**, magic/version/size/slot count/current capture revision/frame serial/payload bytes/CRC32/reserved, and table-free `gpuCheckpointPayloadCrc32` and `validateGpuCheckpointDiagnostic` for same-build integrity checks. Raw payload cap remains 4096 physical slots × (128-byte GPU particle/VM state + 4-byte CPU lifespan mirror), plus header.
- `0e15055333da06fd6cbaedb3afd668f9a299855b` — `apps/editor/Editor.cpp`: validate header/magic/version/bounds/CRC before publishing a `.spfx` file.
- `4c0309b53ef76b5b2fc386645cb1082dc1848a54` — `apps/editor/windows/SpectraGraphWindow.cpp`: V2 diagnostic label and staging-retry/error telemetry.

**Semantics:** CRC32 detects truncated/corrupt diagnostic payloads. V2 does NOT implement GPU import, native savegame, portable ABI serialization, previous V1 migration or historical GPU rewind; the binary still carries the currently captured native layout. The existing `exportFreshCheckpoint` gate is mandatory. A checksum alone never proves freshness or GPU/CPU numeric parity.

### 4. Retain recovery catalog across failed Device import; opt-in pressure hysteresis

- `1b4706ee726c1dcc9ab6d1063054715c26fd7267` — `libraries/include/aiverselib/genre/SpectraGpuRecoveryHost.h`: retained optional CPU-only `PoolRecoveryManifest`. If an import into the fresh graphics Device fails and destroys a partial new pool, subsequent `replaceDevice()` calls can retry with a different healthy Device while preserving authored effects/defaults. In the interim, `inspect` reports DeviceRecreationRequired rather than a ready renderer. Old graphics resources must be disposed while the OLD Device is alive on the first attempt.
- `e164d16656c664af6472dc8193ec56a2c451a121` — `engine/fx/SpectraGpuPoolWorld.h`: `PoolLoadSheddingPolicy::recoverBelowPercent` (default 85, valid 1..100), particle/readback pressure latches and held-frame telemetry.
- `486545efdb04c96ada1a3b7dda9bfb4043dcd7d0` — `engine/fx/SpectraGpuPoolWorld.cpp`: particle-count and readback bytes-in-flight soft-limit hysteresis, overflow-safe integer recovery threshold, latch reset on policy changes, pressure-held frame counters. No change for projects where the Stage 39 policy is disabled.

**Semantics:** This is presentation-only approximate load control. Particle count uses previous frame's CPU/GPU estimate, and readback pressure is reservation bytes, NOT measured GPU memory or PCIe/UMA bandwidth. Automatic births may be shed; explicit manual VFX still have their existing global burst budget. None of these values affect authoritative ECS or networking.

### 5. Documentation and agent contract

- `d2e0307a17b1597dddcf01505c04783acc862c82` — `docs/spectrafx.md`: Stage 40 public API, changed checkpoint V2 format, safety, edge cases and verification status.
- `660f0c4f7668ef6932747155dc01c2a724b162ae` — `skills/visuals/aiverse-spectrafx/SKILL.md`: agent usage and native integration guidance.
- `ba0f1cd3f18d5309b575216ddb8e6a49ed671c38` — `AGENTS.md`: latest Stage 40 pointer and ownership/correctness boundaries.

## What was actually verified

- Retrieved current GitHub `feature/universal-entity-inspector-fps` and `main` branch states (main already ancestor).
- Inspected existing Stage 39/32 checkpoint, Metal RHI checked upload, GPU frame planner, source pool, recovery host and editor render-pass entrypoints.
- Wrote source via GitHub connector with required content SHA guards and re-fetched changed files/API symbols after implementation.
- Source-level consistency review corrected VM error diagnostics to reject partial staging/reset state.
- **NOT RUN:** native C++ CMake/Clang build, ObjC++ compilation, Metal MSL shader build, CTest, native editor startup, real GPU freeze/capture/export, V2 CRC fault-injection, source generation replay, partial upload stress, device replacement retries or actual GPU performance benchmarks.

**Status: source committed, executable/hardware behavior NOT VERIFIED.** Do not call these tests passing, merge into production, assume Metal numeric parity, or claim that snapshot bytes are a portable savegame.

## Limitations and concrete next Stage 41

1. Expose an offline, read-only V2 `.spfx` inspector and optional diagnostic export metadata (no GPU state restore) for developers/agents, including invalid-version/CRC/truncation reporting.
2. Add bounded native frame-host automation for capture/diagnostic requests at actual graphics safe points; do not perform blocking reads or create a second graphics owner. Preserve editor Freeze semantics.
3. Tighten recovery host event/retry failure modes, verify live source tracking is never reattached before a healthy Device and correct committed ECS snapshot.
4. Once features-first is complete: build C++/ObjC++/MSL, run SpectraFX CTest, Apple Metal smoke, checkpoint source-kill/load-shed stress, CRC corrupt payload checks, GPU callback device-loss cancellation and CPU/GPU visual parity. Fix failures before main merge.
5. Continue separate dated handoff per stage with exact committed files/SHAs, owner branch, API and bounds, tests ACTUALLY RUN vs NOT RUN, limitations, and next action. Never force-push concurrent agent work.
