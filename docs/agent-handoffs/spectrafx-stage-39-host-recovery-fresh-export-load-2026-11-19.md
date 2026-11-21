# SpectraFX Stage 39 — native recovery host, fresh GPU checkpoint export and fair VFX load shedding

Date: 2026-11-19
Branch: `feature/universal-entity-inspector-fps`
Starting feature HEAD observed: `23253ede04ef11973826e1f3b1b4751854884fa5`.
Last source fix observed before handoff: `e77f125c74b5e10fde90865a8a3f5a2ed197e31b`.
Changes were committed directly to the existing working feature branch via SHA-guarded GitHub connector file updates. **No main merge, rebase or force push was performed.**

## Exact implementation commits/files

### Binary diagnostics: complete CURRENT-revision GPU checkpoint only
- `5a909ff60f2aa186a9b22dfdc7ac69e8a6ca3433` — **new** `engine/fx/SpectraCheckpointExport.h`: self-contained, bounded native binary encoder, versioned `SPFX` header, slot-count/size checks, strong no-mutation-on-validation-failure.
- `59db6b0301519cf40cb0ee2728161883890dac13` — `engine/fx/SpectraGpuParticles.h`: `ComputeParticles::exportFreshCheckpoint(bytes,maxBytes,reason)` API.
- `8e43eb41cc04f061f49c3bc70e68cee186895964` — `engine/fx/SpectraGpuParticles.cpp`: no readback initiated; requires healthy active GPU, complete/eligible same-revision retained checkpoint, no pending readback, births or kill-fences or failed reset; bounded copy.
- `8c6cf6e5e5b731da16023b038963c1d531acc329` — `engine/fx/SpectraGpuPoolWorld.h`: group-scoped bounded exporter.
- `3eeb1a10d0177e90b9d4c2a64f2854e285be2c10` — `engine/fx/SpectraGpuPoolWorld.cpp`: forwards export only for a valid healthy active Metal GPU pool/group.
- `3e064aa817260998df7ae10648e35ac04273bca5` — `engine/fx/SpectraGpuPoolWorld.h`: `PoolCheckpointStats::exportReadyGroups` telemetry.
- `e94711173a737badd92a75962e67ce6f366f13aa` — `engine/fx/SpectraGpuPoolWorld.cpp`: counts groups eligible to export at time of query.
- `e77f125c74b5e10fde90865a8a3f5a2ed197e31b` — `engine/fx/SpectraCheckpointExport.h`: explicit 40-byte V1 header size.

The diagnostic payload has a 40-byte header plus up to 4096 * (128-byte particle + VM payload + 4-byte CPU lifespan mirror), i.e. **512 KiB GPU particle/VM data + 16 KiB CPU lifespan mirror + 40-byte header**. It is the CPU memory representation of an existing GPU readback, not a stable portable serialization format, authoritative rollback, savegame, or replayable GPU history. The header includes captured revision and frame serial. No CRC, schema migrations or GPU-side restore are provided.

### Fair opt-in VFX overload protection
- `01b64fb4438bb601db4ee6f26142de4619442670` — `engine/fx/SpectraGpuPoolWorld.h`: `PoolLoadSheddingPolicy`, runtime setter, opt-in `PoolWorldBudgets::loadShedding`, per-world pressure stats and round-robin source cursor.
- `5b0375b97554bd37a58663ff75ae0801bfeae801` — `engine/fx/SpectraGpuPoolWorld.cpp`: validates/adapts budgets, rotates source visitation start each frame, bounds automatic bursts, reserves configured manual-burst headroom, suppresses automatic births when estimated particle occupancy or shared Stage 32 checkpoint bytes-in-flight reach thresholds; records dropped births and pressure state.
- `a12f2dbdc66849e62aa9997d7e5cc0393f15667a` — `engine/fx/SpectraGpuPoolWorld.cpp`: corrected a **source inspection finding** in the round-robin loop — declare generation-safe physical source slot index before constructing `PoolSourceId`.

Policy is **OFF by default**. It does not throttle explicit manual `burst` calls beyond their existing global frame budget. Automatic emissions are bounded against both source and shared limits; skipped births are discarded rather than queued for large catch-up spikes. `estimatedParticleSoftLimit` uses the preceding frame's CPU+GPU **estimate** (not exact hardware GPU particle count). Stage 32 readback pressure is based on reserved bytes in flight, not measured PCIe/UMA throughput. Fairness is rotating start, not a weighted fairness guarantee.

### Native renderer-side status/action orchestration
- `f767a364d1be09a350920a8f41f9786471f67283` — **new** `libraries/include/aiverselib/genre/SpectraGpuRecoveryHost.h`: `GpuPoolRecoveryHost`, structured `PoolRecoveryPhase/Action/Event`, deduplicated transition callback, `poll`, `retryPresentationReset`, explicit `replaceDevice`; observes existing Stage 38 `PoolResetReport`/health and existing Stage 35 ECS source presenter.
- `99f5c6950511f95cfab3e0fb20bffa1ec00c9aa3` — `libraries/include/aiverselib/genre/SpectraFX.h`: public native include.
- `9df4b47bb3b78ede6f88f923cf6d031b40b15d3e` — `examples/spectrafx/native_ecs_attachments.h`: wires recovery host and optional event callback, checks recovery state before syncing ECS presentation, calls shared explicit Device replacement, exposes checkpoint export and load policy setter.

Native recovery host has **NO platform Device ownership** and performs no asynchronous work. It emits a change callback when reset progress/health/action changes. If the graphics Device is lost, the native platform host first creates a healthy new Device and holds the old Device alive long enough for GPU resources to be destroyed. The CPU-only Stage 38 `PoolRecoveryManifest` rebuilds effect definitions and defaults; the renderer-owned ECS source IDs and pose history are discarded and then recreated from the committed World. **Live GPU particles are not recovered.** A reset retry must run in an open gfx frame, outside render/compute passes.

### Editor diagnostic binary and telemetry
- `2a34966f2b74a80b8d5c19950fcb1b99e3311991` — `apps/editor/Editor.h`: opt-in fresh GPU diagnostic export API returning path or reason.
- `0f9285a2fc19dd5d0bb2c58b9bb9b462f8fa8f3f` — `apps/editor/Editor.cpp`: writes only validated current-revision `.spfx` bytes to `platform::userDataDir()/spectrafx-diagnostics/`, through temporary file + rename; never requests a new GPU readback.
- `f510e0e1bed4a927daf1960c56eaddb12569190e` — `apps/editor/windows/SpectraGraphWindow.cpp`: export-ready group count, Save fresh GPU checkpoint button and per-world pressure/budget/source-capacity telemetry.
- `73d3f3aa726c59b0b9944b4033a9f3fadd3c99ad` — `apps/editor/Editor.cpp`: configure **only the opt-in Metal preview** for periodic 15-revision GPU checkpoint cadence with adaptive max interval 120. Native game default checkpoint cadence remains disabled.

### Documentation/agent integration
- `708853de754f8a8554241d9cd6f1893101790d41` — `examples/spectrafx/native_ecs_attachments.md`: host events, explicit device rebuild, pressure and binary export usage.
- `d761fd15192cb6436a70bfb95038f18c5a573997` — `docs/spectrafx.md`: public Stage 39 contracts and caveats.
- `d3c9ab04ccf89e4877d3de8953bfc41b08b97f17` — `skills/visuals/aiverse-spectrafx/SKILL.md`: native agent usage guidance.
- `d8c6dd4d72b8c3015d519668c4472688b3c896c9` — `AGENTS.md`: latest handoff reference and genre-neutral ownership rules.

## Verification ACTUALLY performed

- GitHub branch HEAD and source files fetched; API and ownership boundaries inspected for existing GPU checkpoint, CPU fallback, RHI, native ECS adapter, editor source and Stage 38 recovery.
- All listed GitHub file writes received commit SHAs; key source was read back during implementation.
- Static review **detected and fixed** a new undefined source index in the rotating continuous-emitter loop.
- **NOT RUN:** local CMake or Clang build; Objective-C++/MSL compilation; CTest; editor UI launch; actual fresh checkpoint binary export; on-device Metal readback; simulated device-loss/recovery; partial reset and capacity/pressure fault injection; multi-source fairness benchmark; CPU/Metal visual parity or GPU profiler.

This stage is **SOURCE IMPLEMENTED, EXECUTION UNVERIFIED**. Source commits do not establish performance, binary ABI portability, safe hardware recovery or shipping readiness.

## Explicit limits and next Stage 40

1. Confirm the native 40-byte V1 binary layout and same-revision export guard on macOS/Apple Silicon and Windows compiler paths; do not treat the diagnostic blob as portable data. A retained GPU snapshot can be fresh only during narrow windows between asynchronous completion and a later GPU dispatch. Enable checkpoints only when the host can afford the Stage 32 bandwidth budget.
2. Device recovery still requires platform ownership of OLD and NEW graphics Devices, explicit resource teardown, committed ECS source reattachment and reconciliation of renderer time. Callbacks report state but do NOT autonomously rebuild the OS graphics Device.
3. Check readback callback cancellation and `GpuPoolWorld` destruction/recreation against real Metal RHI completion fences; unknown completion status must never release source-killed physical slots.
4. Validate per-source load-shedding pressure and round-robin fairness with sparse/dense pools. The particle soft limit is based on a previous-frame estimate, and reserved manual bursts may go unused; measure actual results before changing defaults.
5. Extend native renderer host with a deliberate GPU checkpoint capture request for paused/frozen previews if warranted by device validation; never silently perform blocking GPU reads from an editor UI frame.
6. When the user has finished feature-first development, compile C++, ObjC++ and MSL; run SpectraFX CTest, a real editor, GPU checkpoint/cancellation stress, device-loss smoke, and CPU/Metal parity tests; fix all failures before merging main.
7. Preserve a dedicated dated handoff with changed files, commit SHA, actual run vs unrun checks, limitations and concrete next action. Do not overwrite concurrent agents or force push.
