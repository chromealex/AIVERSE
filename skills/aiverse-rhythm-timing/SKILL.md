---
name: aiverse-rhythm-timing
description: Judge timestamped AIVERSE tap and hold inputs with tempo changes, latency calibration and deterministic combo scoring in native modules. Use for rhythm timing, not audio playback or beat detection.
---

# Rhythm timing

Read `libraries/include/aiverselib/strategy/Rhythm.h`. All time is integer microseconds.
`TempoMap` uses PPQ and microseconds per quarter. `pulseTime` accumulates segment
numerators before one division, avoiding accumulated rounding across tempo changes.

- Author a sorted `RhythmChart` (1..32 lanes, up to 100000 notes, 24-hour horizon).
  Notes in a lane cannot overlap. Equal start/end means tap; otherwise hold.
- `beginRhythm(chart, calibration)` stores the fixed signed latency correction.
  `rhythmInput` subtracts it once. Feed authenticated input timestamps from the host's
  audio clock; never trust an arbitrary client timestamp for competitive scoring.
- Submit inputs chronologically before advancing past their corrected timestamp.
  Reject late/duplicate commands. `advanceRhythm` accepts already-corrected time.
  Audio buffer size and frame delta must not determine judgement windows.
- A perfect note scores 1000, good scores 500, miss resets combo. A hold remembers its
  head grade, misses on an early release, tolerates release inside the final good window,
  and completes at its end while held. Release the lane before another press.
  Deadline events use timestamp/note order so frame batching preserves combo history.
- Link `AIVERSE::lib_rhythm_timing` and persist all `RhythmState` via `strategy/Persistence.h`
  in NativeState. Bind chart revision/module VERSION. Restore held keys and pending
  grades; do not recompute judgements from presentation animation.
- Follow `examples/native/strategy` command 9 for a headless hold/restore example.
  Its clock is synthetic: actual audio/input clock bridging remains host work.
  Run strategy tests and `python3 tests/libraries/strategy_native.py`; compare batched
  versus fine-step advancement, calibrated inputs, repeated orders and restore mid-hold.

No audio playback, device latency measurement UI, streaming timestamp transport, note rendering,
beat detection or hardware validation is implied. Chart-specific scoring can be added as a
versioned policy rather than changing existing saved scores silently.

Build selection: `AIVERSE::lib_rhythm_timing`; native OBJECT modules call
`aiverse_native_use_libraries(target rhythm-timing)`. Only this header library and its
declared dependencies are selected. See `skills/catalog.json` for dependency and responsibility routing.
