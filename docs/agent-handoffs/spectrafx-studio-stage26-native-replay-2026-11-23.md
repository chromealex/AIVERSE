# SpectraFX Stage 26 — native replay preparation transaction (2026-11-23)

## Provenance
Repository chromealex/AIVERSE; branch feature/spectrafx-html5-data-interfaces-20261123.
Implementation commits: 5be419baef878ac0d3f37c2d87996296449ec67b, 6d0f539d92403c2eba9c2bd31635c3f309e6d7bf, 6ad33254e81593eb48928264441f9308bb395949. Not merged to main.

## Changed files / API
- apps/studio-native/StudioSpectraPreview.h: new replayPrepare action validates integer tick 0..3600 and at most eight named scalar parameters against active GraphRuntime schema/ranges before changing playback. On acceptance it pauses, clears pending burst/step state, schedules native restart, queues scalars and target tick for normal render-owner frame processing. No GPU calls in IPC handler.
- apps/studio-web/src/types.ts: typed replayPrepare with parameters record.
- apps/studio-web/src/panels/SpectraNativePreview.tsx: replace separate restart/parameter/seek requests with one replayPrepare; existing UI still restores stage/playback/camera separately and polls native tick convergence.

## Tests actually executed
NONE. No native compiler, TS typecheck, CTest or hardware validation on these commits. This is a source-only change.

## Boundaries and limitations
- Validation is atomic only for tick and scalar inputs before mutating the preview; camera/stage/playback commands remain separate. No rollback across those commands.
- No deterministic RNG seed exposed. No guarantee CPU/GPU parity, identical particle state, or exact GPU frame fence provenance.
- Native GraphRuntime parameters are applied in prepare() after pendingRestart_ burst; this ordering may mean initial burst samples old defaults. Audit/fix before claiming deterministic parameter replay.
- The existing status tick can reach target while queued GPU work is not yet completed; frame capture still needs fence-aware exact provenance.
- Project switches and native viewport teardown require integration tests; do not merge untested work to main.

## Next steps
1. Run focused exact-SHA AppleClang/Metal + TypeScript/Vite tests and correct any errors.
2. Ensure parameter values are applied before the restart burst, with regression tests.
3. Add a render-owner completion marker and native capture provenance on actual submitted frame.
4. Audit GraphRuntime seed control; implement explicit seed only if CPU and GPU code paths support it.
5. Update agent skill, AGENTS roadmap and devlog after verified native screenshots.
