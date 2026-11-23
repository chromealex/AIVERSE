# SpectraFX Stage 38 — discoverable MCP replay/capture tools (2026-11-23)

Repository chromealex/AIVERSE. Branch feature/spectrafx-html5-data-interfaces-20261123. Not merged into main.
Commits 7002ea658682056a7e40d223568e31fb706a66aa, 1c34e5db1b7787303535f926daa58e312c33149d.

## Implementation and API
- apps/editor/core/Workspace.cpp: registers three Workspace ops, discoverable by toolList and therefore MCP: fx_replay_prepare, fx_replay_status and fx_replay_capture.
- All three delegate to Workspace::spectraPreview(), which is wired by apps/studio-native/StudioHost.cpp to runSpectraPreview() and native render-owner dispatch. No separate unsynchronized GPU control path was created.
- fx_replay_prepare: tick (0..3600), optional bounded scalar parameters, optional mutually exclusive CPU seed or Metal allocation seed. The native replayPrepare validates actual values.
- fx_replay_status: reports native readiness, tick, backend, pending/retries/failure.
- fx_replay_capture: action capture/captureStatus, optional maxSide, requireReplayReady; capture and ready check use one render-owner dispatch, JPEG provenance includes frameExact=false.
- skills/visuals/aiverse-spectrafx/SKILL.md: instructions for agents on discovery, command order and headless limits.

## Tests actually run
NONE. User requested feature completion before tests. No compilation, CTest, MCP handshake, Metal execution or UI QA. Tool registration and signatures are source changes only.

## Limitations
- Headless MCP can discover the operations but cannot execute GPU work without the native Studio viewport.
- The native JPEG callback has no verified exact GPU frame/fence ID. Request-time observations must not be presented as exact.
- GPU allocation seed is not GPU shader RNG and not CPU/GPU parity.
- Schema may be accepted by MCP discovery but requires deferred actual runtime validation.
- Graph Editor UI completeness and exact GPU frame capture remain outstanding.

## Next
Finish remaining Graph Editor UX/asset linking and GPU RNG capability audit, update skill catalog if required, then run full native/TS/MCP acceptance once features are complete.
