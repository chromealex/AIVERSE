# SpectraFX Stage 36 — GPU scatter-kill optimization, native ECS sample, editor playback controls

Date: 2026-11-19
Branch: `feature/universal-entity-inspector-fps`
Starting branch HEAD: `8e2dbd6125ec97abcd437eeb0f25447b086247cc`
Latest observed HEAD before handoff commit: `e39ab6990ecdbee734e03a73b3051b4cbeb6a239`
Feature branch was not rebased/force-pushed or merged into main for this stage. User's request to integrate main first was already satisfied in Stage 35: main was ancestor of working branch. Do not merge unrelated feature changes into main.

## Code and documentation written (exact commits)

1. `4fe580a3467398e71edc07b9d2a7f73f1b985367` — `engine/fx/SpectraGpuShaders.h`: MSL and future-reference GLSL shader mode 6, scatter-invalidate particles given physical slot index buffer at compute binding 14.
2. `8a20cd1c2a0d8d76a0adc8c16bb0e7568e14c095` — `engine/fx/SpectraGpuParticles.h`: GPU kill index buffer ownership, dispatched-kill and index transfer byte telemetry.
3. `079212999a3c8ca1bae3b163b3ac913b3998fbf5` — `engine/fx/SpectraGpuParticles.cpp`: allocate/bind/release GPU kill index buffer, replace fragmented dead-particle state blits with one checked up-to-16KiB index upload, dispatch kernel mode 6 before Spawn/Integrate/Compact inside one ordered compute encoder; retain completion-fenced physical-slot quarantine.
4. `a68ca27550f03adc3612afcac940f59b23654f51` — **new** `examples/spectrafx/native_ecs_attachments.h`: native project integration example using existing ECS SourceComponent, read-only fixed-point position resolver, committed tick read, viewport pre-pass/draw/post-pass ownership, generation-safe replacement and pool-wide visual rollback reset.
5. `ef5892648208fb6481f3a43e227712aa4a7b2fcf` — `apps/editor/Editor.h`: GPU preview playback speed, pending one-frame step API.
6. `d59f16a372f4a6101971941a1137b9b76b4669d7` — `apps/editor/Editor.cpp`: clamp opt-in Metal GPU visual speed 0.1x..2x; when frozen, Step GPU advances exactly one 1/60-second simulation update without creating a new burst. Does not change gameplay time.
7. `b433176628b49953a04ef20c88d985482145b8f2` — `apps/editor/windows/SpectraGraphWindow.cpp`: GPU speed slider and Step GPU.
8. `a45e1a826ff5d66c583bf13dd82bf901eb2b3376` — `engine/fx/SpectraGpuPoolWorld.h`: aggregate GPU kill scatter dispatch and bytes telemetry.
9. `334ac81968de76bd82b10a86cd0c5b36385cea89` — `engine/fx/SpectraGpuPoolWorld.cpp`: sum GPU group scatter-kill metrics.
10. `9a23b3df59ebec5099ddb19c40e7bf5f8a57d734` — `apps/editor/windows/SpectraGraphWindow.cpp`: display scatter dispatch count and index transfer KiB.
11. `27bf8a0e15f735a41ab92e7301a3ac60685130c3` — **new** `examples/spectrafx/native_ecs_attachments.md`: renderer/native host initialization, attachment, pose, rollback integration guidance and limitations.
12. `3caf9484f01371d1325f93ad18532845703c3e1f` — `docs/spectrafx.md`: Stage 36 API and boundaries.
13. `d1378cb9c0e242271d0da5229b0d3a85609ed2e6` — `skills/visuals/aiverse-spectrafx/SKILL.md`: updated agent integration instructions.
14. `e39ab6990ecdbee734e03a73b3051b4cbeb6a239` — `AGENTS.md`: latest handoff pointer and GPU source-kill/genre-neutral ownership rules.

## Runtime architecture

- GPU particles remain presentation-only, not saved in authoritative ECS, gameplay hash, physics or network rollback. ECS-to-presentation bridge from Stage 35 remains the sole persistent source adapter and uses the existing `SpectraSourceComponent`.
- A source's GPU physical slots may be marked for selective kill, but cannot be reused immediately. Stage 35 quarantines them until `completedFrameSerial() >= submittedFrame` for a successful Metal command-buffer completion; Stage 36 retains this safety gate unchanged.
- Before Stage 36, selective kill uploaded up to `capacity` fragmented 64-byte GPU `ParticleState` runs. The new shader mode 6 reads `killSlots_` at buffer binding **14** and invalidates only the provided physical slots. The CPU sends a single checked, bounded list of 32-bit slot indices into GPU storage before `beginCompute`, then issues one scatter dispatch first in the existing compute encoder, restoring birth count uniforms for subsequent phases. No new RHI/device queue, shader entrypoint or gameplay subsystem.
- The current GPU capacity cap is 4096 physical slots, so scatter-kill input upload is <= 16 KiB per step. This is an **algorithmic bound**, not measured bandwidth. If checked upload fails, the call exits without returning killed slots to the free list or publishing a false successful step.
- GPU VM/custom attribute sidecar data may remain in killed slots but Spawn initializes reused slots. All GPU work still requires a healthy Metal device and correct queue/frame/pass ordering. Multi-source isolation uses source tokens allocated by the pool.
- `ComputeStats::gpuKillScatterDispatches` and `gpuKillIndexBytes` are counted per GPU emitter and aggregated into `PoolCheckpointStats::killScatterDispatches` / `killIndexBytes`. Editor displays these numbers as estimates of submitted work, not stopwatch or hardware profiler numbers.

## Native example

Use `examples/spectrafx/native_ecs_attachments.h`, documented in the accompanying `.md`. It is **not a runnable stand-alone binary**; the native game supplies gfx Device, a compiled effect, scene pass and authoritative World. The example's `DemoPose` is FIXED POINT and remains in ECS. Only its const resolver converts it to presentation coordinates. The example creates existing `SpectraSourceComponent` instances, binds `PoolContinuousEmission`, calls `GpuEcsAttachmentPresenter::syncCommitted` once per presentation frame, shares CPU fallback or opt-in compatible Metal GPU group through `GpuPoolWorld`, and demonstrates generator-safe replacement and explicit rollback.

## Editor additions

Opt-in **Metal GPU** preview now exposes Step GPU and visual speed 0.1..2.0x on top of Stage 35 Pause/Freeze/Loop/Stop. Step acts only when frozen, advances one fixed 1/60-second GPU visual simulation update with no new burst, and leaves authoritative gameplay tick rate untouched. The existing CPU editor preview remains the default. No full GPU effect graph parity or GPU scrubbing backward is claimed.

## Actual verification

- Successfully fetched current GitHub branch and source files, compared API ownership and inspected Metal compute shader and GPU slot pipeline.
- Successfully created or updated listed source files and reference documentation via SHA-guarded GitHub connector writes.
- Re-fetched all nine key source/header files and confirmed current symbols/paths and latest branch HEAD at `e39ab6990ecdbee734e03a73b3051b4cbeb6a239`.
- **NOT RUN:** CMake/Clang compilation, Objective-C++/Metal compilation, CTest, editor boot, on-device GPU visual preview, GPU fence fault injection, multi-source GPU isolation, native sample compilation, end-to-end rollback, hardware performance and memory profiling.

## Limitations and next Stage 37

1. Validate shader buffer-14 binding, scatter-mode MSL ABI, upload failure behavior, correct GPU kill-to-Spawn ordering and completion serial on Apple Silicon before relying on selective GPU kills. Existing native compilation, CTest and editor checks remain pending by user choice to finish features first. Build success is NOT claimed.
2. The demo remains a native integration header rather than a fully bootstrapped example app. Integrate a real native game loop and add scoped presentation attachment manager if desired, keeping authoritative ECS isolated.
3. Extend interactive editor GPU playback with truly controlled visual timeline/snapshot stepping (no unsupported backward GPU scrub). Add explicit GPU capability/readback failure diagnostics and disallow invalid commands gracefully.
4. Audit Stage 35 ECS bridge error recovery (policy replacement and allocation failure mid-sync are not globally transactional); consider two-phase admission of sources and deferred releases.
5. Audit GPU resource teardown and queued readback callbacks on device loss; maintain Stage 32 shared limiter fairness.
6. Preserve separate dated per-stage handoff with exact SHAs and actual tests run or NOT RUN. Do not force push or change main without explicit user request.
