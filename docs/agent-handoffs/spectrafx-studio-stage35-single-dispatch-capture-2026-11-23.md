# SpectraFX Stage 35 — single-dispatch replay capture guard (2026-11-23)

Repository: chromealex/AIVERSE
Branch: feature/spectrafx-html5-data-interfaces-20261123
Commits: 4553d0f3f751c8601d179cc4ad75ff9052b204d5, dbbd312c19b3ebd1e8a4211cc36c484baad61582.
Not merged to main.

## Changed files and API
- apps/studio-native/StudioHost.cpp: captureSpectraFrame validates requireReplayReady input on IPC thread, but now reads replayReady and registers captureNextFrame within the SAME onScene render-owner dispatch. The prior design checked readiness in a separate dispatch, permitting an intervening native frame. Capture provenance records replayReady at readback scheduling.
- apps/studio-web/src/types.ts: optional replayReady field in native capture provenance.

## What this guarantees
The replay-ready check and capture callback registration cannot be interleaved by another onScene command. The check still does not freeze the future rendered frame, ensure GPU fence completion, or guarantee exact tick/pose of the resulting JPEG. Provenance is explicitly request-time.

## Tests actually run
NONE, per user's feature-first instruction. No C++ compilation, TypeScript build, GPU capture or manual Studio QA.

## Limitations
- No exact frame serial or GPU fence id. gfx::Device::captureNextFrame currently returns RGBA through a later callback with dimensions only.
- No GPU shader RNG seed; GPU allocation seed only permutes free-slot ordering.
- The UI and MCP share this capture guard but only clients requesting requireReplayReady benefit from it.
- Full native Replay Controller transaction for stage/camera/seed/capture remains incomplete.

## Next steps
1. Introduce a capture ticket/serial at the actual gfx::Device frame submission point, if supported by the RHI, and bind that to completion callback.
2. Extend MCP capture parameters to include requireReplayReady and return provenance.
3. Finish Graph Editor/skills parity, then comprehensive tests after feature completion.
