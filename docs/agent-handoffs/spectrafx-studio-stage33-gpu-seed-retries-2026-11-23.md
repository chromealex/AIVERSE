# SpectraFX Stage 33 — bounded GPU seed retry state (2026-11-23)

Repo chromealex/AIVERSE; branch feature/spectrafx-html5-data-interfaces-20261123; no main merge.
Commits f08a66766402976ffc743cad25b76c3e59d54473, 81b00b3f1d3e8d2d293407f03ba1f1a0de3cfdaf, 2062226d285d66d979d0930b718e57c0a15ed0b9, 392f748df0b40850ed1e34ff3bdff0329056097f.

## Implementation
- apps/studio-native/StudioSpectraPreview.h: Metal allocation seed reset retries are capped at 8 native prepare frames; status now exposes gpuSeedPending, gpuSeedFailed and gpuSeedRetries. Terminal failure stops rescheduling and preserves the native error. A new replayPrepare seed resets the counter. Clear/load and lifecycle paths attempt to clear stale retry state.
- apps/studio-web/src/types.ts: typed optional retry/failure status.
- apps/studio-web/src/panels/SpectraNativePreview.tsx: transient GPU seed errors no longer immediately terminate the replay convergence poll; terminal failures do.

## Tests actually run
NONE. User asked to complete all features before tests. No build, typecheck, CTest, Metal or interactive QA performed.

## Limitations
- GPU allocation seed only permutes Metal free slots; it does not seed shader PRNG and does not establish CPU/GPU visual equivalence.
- Retry budget prevents an endless native frame loop on persistent device failure, but cannot fix device loss.
- Replay/capture is not yet atomic or fence-exact. Further audit needed to ensure lifecycle reset paths are exhaustive.
- C++/TypeScript code has not been compiled; source changes are not verified.

## Next
Implement real GPU shader PRNG only after tracing shader VM random operations; otherwise explicitly surface unsupported capability. Then finish replay/capture transaction, MCP, Graph Editor, skills and handoffs. Run comprehensive tests only after features are complete.
