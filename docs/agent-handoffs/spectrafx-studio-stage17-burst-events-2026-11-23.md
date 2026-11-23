# SpectraStudio Stage 17 — repeatable native burst events and deferred clear

Date: 2026-11-23
Repo: chromealex/AIVERSE
Branch: feature/spectrafx-html5-data-interfaces-20261123
Draft PR: #115 (stacked on draft PR #113)
Code commit SHAs:
- e644f8ac106b45be280805734a01566e162ed288 — render-thread burst/clear event queue
- 3e3f8197f1e1e933c543d1bf2c6bd18bb2488639 — MCP operation schema
- 8f63907e852487a327da45a1bda81609e2e38ddc — typed HTML5 bridge fields
- 091b5669805e6a9df42e59cf926c525fb22c4a6a — editor controls
The handoff and future docs commits advance branch HEAD.
Main has not been modified.

## Feature

Authors can now test multiple one-shot emission events on the same
already compiled native Graph v2, without recompiling or re-saving the asset.

`apps/studio-native/StudioSpectraPreview.h`:

- `{"action":"burst","count":1}` (default count = 1) queues 1–8
  manual bursts from the current compiled runtime. A total of at most
  32 bursts can be queued across calls before the next frame; further
  requests return a clear error and preserve prior state. The queue
  executes only inside `StudioSpectraPreview::prepare` after the
  native device frame starts and before the draw pass, using real
  `spectra::GraphRuntime::burstAt`. No browser simulation and no
  particle GPU work on a UI callback thread.
- `{"action":"clear"}` pauses visual time, cancels a pending seek,
  queued bursts and pending automatic restart, and schedules
  `GraphRuntime::clear` at the next native frame's safe phase.
  It does not unload/recompile the Graph v2 or erase the authored
  game project, graph asset, draft or published effect.
- A pending manual burst may be requested immediately after Load
  (the normal initial burst still occurs). These events are
  independent of the Play/Pause clock: bursts emitted while paused
  will not age until simulation advances.
- Native status reports `bursts` (successfully triggered since
  load/restart/clear), `queuedBursts` and `clearing`. Stop/load/
  restart resets relevant pending state and counters.
- On a rejected native emitter burst the command records a runtime
  error rather than claiming success. The effect never changes
  authoritative ECS or damage/collision state.

`apps/editor/core/SpectraGraphOps.cpp`:

- `fx_graph_preview` adds `burst` and `clear` actions with a
  bounded `count` integer schema (1–8). The existing authenticated
  MCP Workspace tool and `studio.spectraPreview` share a single
  `StudioHost::runSpectraPreview` native render-thread path.

`apps/studio-web/src/types.ts` and
`apps/studio-web/src/panels/SpectraNativePreview.tsx`:

- Native IPC unions and response fields include burst event/counters.
- The SpectraFX Graph **Particle Preview** toolbar now includes a
  1×/2×/4×/8× burst count chooser, **Trigger Burst** and
  **Clear Particles** buttons, and a completed/queued burst counter.
  Both require an actual loaded native effect.

## Test/QA status

- **DEFERRED** — user requested implementing features first.
  No Stage 17 native C++/Apple Metal, Windows Vulkan, Node22/TypeScript,
  Vite, CTest, MCP live-call, screenshot or GPU/CPU parity check has
  been executed/observed for these Stage 17 changes.
- Prior Mac 2/2 CTest PASS belongs to earlier Scene-only preview;
  it is NOT proof that these burst controls work.
- The runtime contains an existing GPU fallback/retirement contract
  not modified in this stage. Do not treat a queued burst count as a
  GPU-submission-completion measurement or GPU parity evidence.
- Native preview retains the 1024-capacity emitter option. Many
  bursts may saturate it by design; this tool does not increase
  runtime particle capacity.
- Preview UI/controls are intentionally separate from user graph
  persistence and any game genre modules.

## Next feature / handoff

Implement isolated screenshot/frame capture and readable performance
diagnostics (actual native GPU frame, bounded asynchronous data,
fences, size limits), OR continue advanced graph wiring/undo tooling.
Do not substitute a fake React image for actual GPU output.
Once the user explicitly switches to acceptance, perform exact-SHA
Node/Vite + full native Studio/Metal CTests and then update these
handoffs with actual tested SHAs and results.
