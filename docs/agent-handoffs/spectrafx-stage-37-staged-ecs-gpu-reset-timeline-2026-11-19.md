# SpectraFX Stage 37 — staged ECS admission, checked GPU reset, forward-only editor timeline

Date: 2026-11-19
Branch: `feature/universal-entity-inspector-fps`
Starting HEAD observed: `e81f03b1b510b668e697c568c3410a459c838ce1`
Last source commit before this handoff: `efbab51cfadf09e0bf18f318ea6d4024cb12561f`
No branch rebases, force pushes or changes to `main`. All source modifications were GitHub connector contents writes with blob SHA guards.

## Source and documentation commits (exact)

### Staged ECS source admission and binding revision
- `ac73f12242e069c1ee72275eefe6ba5bd6ce09a0` — `engine/fx/SpectraGpuPoolWorld.h`: read-only `canStopSource(PoolSourceId,bool)` preflight.
- `2dc0cbfd8b74450b52779b333c17e0a910941a25` — `engine/fx/SpectraGpuPoolWorld.cpp`: preflight GPU health, safe pass and source ownership.
- `8f51876fbea6794424af99f1a05a929f625e3826` — `engine/fx/SpectraGpuPoolWorld.h`: `sourceBindingCurrent()` independent of pause state.
- `2134e8f49f6f1de286e7f4f92fa5f115d9bc5996` — `engine/fx/SpectraGpuPoolWorld.cpp`: detect stale named-graph revision in an existing renderer-owned source.
- `8360561b5c44170a68a8efdee00f5f7f8ade416d` — `libraries/include/aiverselib/genre/SpectraGpuAttachmentEcsAdapter.h`: prevalidate finite resolved poses and effect policies, stage replacement sources with rollback of staging on failure, run non-mutating detach checks before commit, keep existing bindings if staging cannot proceed.

### Explicit GPU failure diagnostics, no stale frame draw
- `50f0600f2e9e83908496a0c0674e6ef91a7ce531` — `engine/fx/SpectraGpuParticles.h`: class-specific GPU health, kill upload, compute and pass-order failure counters.
- `941b024f65e0bff5812793c88e1caa53f749de6e` — `engine/fx/SpectraGpuParticles.cpp`: count actual failure paths.
- `c3be997e09ed68a312f43e79a9001c4397d29875` — `engine/fx/SpectraGpuPoolWorld.h`: aggregate failure counters plus known/unknown GPU health.
- `f46363159d1eab97f1fc3f6a85470166794b7dff` — `engine/fx/SpectraGpuPoolWorld.cpp`: on-demand GPU health/failure aggregate.
- `fa731c8c839f08fc5290d43ce0289e341179cab1` — `engine/fx/SpectraGpuPoolWorld.h`: mark whether each group completed a successful presentation step.
- `edd767134d1e1d0e72a5caf9ed6134ea23ee199f` — `engine/fx/SpectraGpuPoolWorld.cpp`: suppress stale GPU group draw calls after failed group step.
- `0cb2d48963352cc714e4eea19edee39d666011f3` — `engine/fx/SpectraGpuParticles.cpp`: clear standalone stale vertex publication flags after rejected GPU submissions.

### Safe GPU rollback reset
- `11e34bd507d9b103cc293b78c61dc5c1c18727d5` — `engine/fx/SpectraGpuParticles.h`: `tryClear()` bool reset contract, `gpuResetDirty_` fail-closed state, reset failure counter.
- `04ea7aabaa2eb3532350da871341ddd871afe519` — `engine/fx/SpectraGpuParticles.cpp`: checked Metal blits for state/VM attributes/parameters; CPU slot mirror only cleared if every command accepted; dirty flag blocks further GPU simulation after partial failure. Existing `clear()` is compatibility wrapper.
- `cae86f8991d5a47f438e1780c7d08363290bf4a4` — `engine/fx/SpectraGpuPoolWorld.cpp`: rollback calls all GPU group `tryClear` before shared CPU state/source bookkeeping; no false success if a GPU reset is rejected.
- `aeaf060d9206758871256b2b5bd3b112fd4c1c1b` — `engine/fx/SpectraGpuParticles.cpp`: reject kill/capture during incomplete GPU reset.
- `a3784bbcb3d600acad25c7c9866730c019168c59` — `engine/fx/SpectraGpuPoolWorld.h`: pooled reset-failure stat.
- `d4a67a03e14371fed9ee07140335f67a246a240c` — `engine/fx/SpectraGpuPoolWorld.cpp`: aggregate reset errors.
- `40a8cc73ff2d476970ae5cf7dcc84c3dca3ca076` — `engine/fx/SpectraGpuParticles.cpp`: explicitly disable GPU drawing during incomplete reset.
- `efbab51cfadf09e0bf18f318ea6d4024cb12561f` — `engine/fx/SpectraGpuParticles.cpp`: cleanup redundant async capture guard.

### Editor GPU timeline and documentation
- `9b3a2b7fccaef0777e1d274cd8c9f2a8d2844c7d` — `apps/editor/Editor.h`: bounded forward-frame request, explicit restart, elapsed visual time and preview error API.
- `5e21f36f8e1c64be79e8f4dc0a75910b4b1ac97a` — `apps/editor/Editor.cpp`: visual timeline with max 300 pending fixed 1/60 steps, retryable checked restart at safe pre-pass phase.
- `690b8496e89e4a2139fc35e1740a3926462505e0` — `apps/editor/windows/SpectraGraphWindow.cpp`: forward GPU advance/restart UI and actual Metal health/failure diagnostics.
- `5a529aafe1b33bf1842f3cdbe53b5ab2f936d5bd` — `apps/editor/Editor.cpp`: loop cadence follows simulated preview time rather than editor wall time.
- `f88125fb57582b63e3b307657ae6753d5996ffe5` — `apps/editor/windows/SpectraGraphWindow.cpp`: surface checked rollback reset failure counts.
- `9a124c7253086547a552369872c8ee7114662677` — `examples/spectrafx/native_ecs_attachments.h`: example rollback now requires active gfx frame outside encoders.
- `57ee2aafb5ad9fce0bbf42c6702d710425c18070` — `examples/spectrafx/native_ecs_attachments.md`: clarify graphics-frame ordering.
- `aa3cc4035bb439a2145459b237194157095bc8fe` — `docs/spectrafx.md`: public Stage 37 API and boundaries.
- `f72c9277aa3819bb06c40443b499897108a9c4ae` — `skills/visuals/aiverse-spectrafx/SKILL.md`: agent instructions.
- `af1e45c6bb019ecf1b70c06bdfe9f9ee714a50e4` — `AGENTS.md`: current handoff pointer and ownership rules.

## API/architecture

The ECS bridge is still read-only against `World` and the existing genre-neutral `SpectraSourceComponent`. A newly requested `PoolSourceId` is provisionally allocated, configured and checked before any existing attachment is retired. If allocation fails due to pool capacity, the old attachment remains unchanged. `GpuPoolWorld::sourceBindingCurrent` detects redefined graphs even when the old source is paused. The adapter also rejects nonfinite resolved poses before mutating existing presentation state.

This is NOT a hardware-global transaction. A selective kill or actual RHI failure during the final commit may detach some earlier sources before a later detach rejects. The adapter preserves still-tracked handles and returns an error for a safe retry; no promise of atomic multi-emitter GPU mutation. Source replacement needs enough spare source slots to stage old and new simultaneously.

`ComputeParticles::tryClear` requires a Metal gfx Device with an ACTIVE command buffer created by `beginFrame`, outside render/compute encoders. Three checked GPU reset writes must ALL be accepted before the CPU particle/slot bookkeeping is cleared. On partial failure, `gpuResetDirty_` blocks further GPU steps, births, source kills, manual/periodic checkpoint capture and stale drawing. The existing void `clear()` forwards to checked API for compatibility but must not be treated as proof of reset; inspect `reason()` or use `tryClear()`. Successful checked *encoding* is not hardware completion; `completedFrameSerial()` and device health still decide physical slot recycling.

Pooled `resetForRollback` clears GPU groups first and then CPU/source bindings, and reports false if any group cannot reset. This is best-effort across groups; already-cleared groups may remain cleared if a later group fails. Do not replay committed presentation ticks before a successful full reset at a later safe graphics frame.

Metal GPU editor preview `queueSpectraGpuPreviewFrames` accepts <=300 total queued steps only while frozen and active. Each rendered preview frame consumes at most one fixed 1/60-second advance; timeline display increments only after successful GPU step. Restart requests an actual checked pool reset before the next render pass. No backward seek, historical GPU snapshot playback, or production proof of CPU/GPU parity. Loop follows simulated time.

## Verification actually performed

- GitHub branch/file retrieval, source-interface inspection, repeated guarded content updates and re-fetch confirmation of changed source API symbols.
- **NOT RUN:** native CMake or Clang C++ build; Objective-C++/Metal compiler; CTest; editor launch/screenshot; GPU simulation, reset, device-loss and completed-frame hardware checks; attachment capacity failure injection; GPU/CPU parity or profiling.
- This is **SOURCE IMPLEMENTED, EXECUTION UNVERIFIED**. Do not infer passing tests or production safety from GitHub commit success.

## Next Stage 38

1. Improve multi-group rollback reset robustness with a per-group reset outcome and explicit device recovery semantics; avoid an implied atomic guarantee when one GPU group fails after others accepted writes.
2. Expand the preview timeline with accurate state/event history and GPU snapshot export only where full checkpoint freshness can be verified; otherwise preserve forward-only restrictions.
3. Add renderer-native ECS render-pose interpolation integration with real game example and source budget headroom monitoring.
4. When the user ends features-first implementation, compile C++/ObjC++/MSL, run SpectraFX CTest, Metal smoke and fault injection for partial reset, device loss, source bind/rebind/rollback, stopped visual time and GPU frame fences. Fix failures before any merge to main.
5. Keep a stage-specific handoff after each implementation block with exact SHAs, files, tests actually performed and an actionable next step. Preserve concurrent agent edits and never force push.
