# AIVERSE — Stage 135: unblock Stage134 native benchmark after Stage129 synthetic assertion

**Date:** 2026-11-20  
**Repository:** `chromealex/AIVERSE`  
**Branch:** `perf/batching-stage134-native-acceptance-20261120`  
**Parent PR:** [#82](https://github.com/chromealex/AIVERSE/pull/82) Draft, based on `perf/batching-stage133-archive-verification-20261120`.  
**Original Stage134 HEAD:** `8125bc43ef206ca970c768da06b72cd533d8cfdc`.  
**Fix commit:** `7b0413aefa6f6562fe956e672ec3a50f469238d8`. This document is a subsequent commit.

## Real Mac Studio validation supplied by owner

The owner executed `bash tools/batching-stage134-native-mac.sh` in the dedicated worktree `~/Projects/AIVERSE-batching-stage134`. The native AppleClang/Objective-C++ CMake build completed successfully through `[100%] Built target aiverse_material_batches`.

The launcher **stopped before any 16-process GPU performance trials** at the synthetic Python self-test:

```text
File "tools/batching-stage129-analyze.py", line 126, in self_test
  assert abs(scaling[1]["mdi_premium_delta_ms"] - .090) < 1e-9
AssertionError
```

This is NOT a C++ compile failure, Metal ICB crash, actual MDI raster-parity failure or new GPU regression.

## Diagnosis and fix

Stage129's synthetic OFF/ON controls are:
- N=1, OFF: Direct=0.570, MDI=0.820; ON: Direct=0.569, MDI=0.780 (ms)
- N=4, OFF: Direct=0.800, MDI=1.150; ON: Direct=0.790, MDI=1.090 (ms)

OFF expected MDI-premium scaling = `(1.150-0.800)-(0.820-0.570) = 0.100ms`; that assertion was correct.

ON expected MDI-premium scaling = `(1.090-0.790)-(0.780-0.569) = 0.089ms`; the test incorrectly expected `0.090ms` (difference `0.001ms` well above the `1e-9` tolerance).

**Only change:** `tools/batching-stage129-analyze.py`, one assertion constant `.090` → `.089`. No algorithm, CSV format, renderer, shader, Metal, Vulkan/GLES, AUTO/MDI opt-in defaults, or runner process logic changed.

## Tests actually performed

- The owner **successfully compiled** the Stage134 `aiverse_material_batches` native Mac fixture (the exact build output is in the chat).
- The assistant independently reproduced both OFF and ON arithmetic with Python `float` and `Decimal`; the exact expected values were `0.100` and `0.089` ms, and independent checks returned PASS.
- The assistant checked the checked-in Stage129 Python source from GitHub, identified and patched the exact offending assertion in GitHub commit `7b0413a...`.

**Not performed or confirmed:** full checked-in Stage129 `--self-test` rerun after fixing; Stage132/133 self-tests on the Mac; 16 native Metal benchmark processes; PPM equality on real GPU; real GPU command-buffer timing; Stage134 CI PASS; full CTest or Vulkan/GLES compilation. These remain acceptance gates; do not mark this stage as end-to-end validated until the owner completes them.

## Resume safely without C++ recompilation

The existing Stage134 worktree has a completed benchmark executable and only Python source changed in this fix. It is a **dedicated worktree**. Check for tracked local modifications before updating it, to avoid losing edits. Do not reset any other checkout, active integration branch or unfinished merge.

```bash
WT="$HOME/Projects/AIVERSE-batching-stage134"
cd "$WT" || exit 1
git status --short
# Continue only if there are NO tracked changes that need preserving.
git fetch origin perf/batching-stage134-native-acceptance-20261120 || exit 1
git switch --detach origin/perf/batching-stage134-native-acceptance-20261120 || exit 1
python3 tools/batching-stage129-analyze.py --self-test || exit 1
bash tools/batching-stage134-native-mac.sh
```

CMake incremental build may recheck sources but the one-line Python-only change does not require recompiling `aiverse_material_batches`. The launcher will record the updated exact HEAD and existing/rebuilt binary SHA-256 and then perform Stage129/132/133 checks and 16 benchmark processes. A genuinely new compiler error or test failure should be reported with its first relevant lines; never assume success from the already-completed compile. Avoid running the launcher with manually patched dirty tracked source: its clean-worktree preflight intentionally refuses that.

On completion share `Stage129 archive: ...tar.gz` and/or summary files; do not interpret performance before confirmed raw GPU timestamps/MDI topology/parity and Stage132 reliability analysis.

## Next agent

1. Observe CI for latest full Stage134 HEAD, not earlier cancelled/queued runs.
2. Verify the owner gets `PASS: Stage129 analyzer synthetic self-test`, then check Stage132 and Stage133 self-tests and actual native archive.
3. If more Python self-test failures occur, reproduce calculation, write minimal correction on the **current** base without altering production defaults; commit a separate handoff.
4. Once valid Mac GPU results exist, determine MDI GPU premium and consider a specifically guarded Metal ICB optimization, preserving AGX protections and Vulkan/GLES functionality.
