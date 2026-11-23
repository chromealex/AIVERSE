# SpectraFX Stage 27 — replay parameter/burst ordering and RNG audit (2026-11-23)

## Provenance
Repo chromealex/AIVERSE; branch feature/spectrafx-html5-data-interfaces-20261123.
Implementation commits c5f05c4b78e9ea40e9a720cdd6457163baccd374 and 5f22130d5fa33be4de1fd7b6b8929c527f909fda. No main merge.

## Changed code
apps/studio-native/StudioSpectraPreview.h:
- Stage 26 replayPrepare already validates all scalar inputs before mutating state.
- prepare() now clears runtime on pending restart, applies queued named parameters first, then emits the initial burst. Failed parameter values are retained for inspection/retry instead of clearing the whole map.
- A separate restartBurstPending_ state preserves the initial burst across the restart/parameter phase, and loop boundaries schedule the burst explicitly.
- stop/clear reset pending burst state.

## RNG audit
engine/fx/SpectraGraphRuntime.cpp explicitly rejects GPU v1 random ranges and spread/random direction. No verified shared deterministic seed API was found in the reviewed GraphRuntime public interface. Do not add a seed option to replay JSON or claim deterministic CPU/GPU replay without tracing each supported backend and its PRNG state. Existing graph/runtime paths may use implicit randomness elsewhere; full audit remains open.

## Tests actually executed
NONE. No compiler, CTest, GPU hardware, UI or pixel QA on the changed SHA. Code must be validated before merge.

## Risks
- Pending restart is consumed before parameter application; on failure the burst is deferred, but the current retry/error flow needs native tests.
- The runtime's setNamedProgramParameter(...,true) semantics and initial spawn uniform visibility need actual CPU/Metal verification.
- Stage 26 seek completion does not guarantee GPU capture fence completion; capture provenance is not atomic.
- No deterministic seed or full GPU/CPU parity claim.

## Next steps
1. Run focused AppleClang + Studio tests and TS typecheck, then real Metal A/B screenshot acceptance.
2. Add regression test: replayPrepare with named scalar must influence the first burst; invalid scalar must not mutate replay state.
3. Audit GraphRuntime, CPU particle solver and GPU VM PRNG paths before introducing seed.
4. Add render-owner exact capture provenance and fence-aware completion.
5. Update agent skill/roadmap and devlog with genuine editor screenshot after successful QA.
