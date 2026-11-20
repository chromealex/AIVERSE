#!/usr/bin/env bash
# Stage137: one-command, isolated Mac Studio acceptance of CPU Metal MDI A/B.
# Invoke in a NEW detached worktree based on the Stage137 branch.
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"
if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "Stage137 requires native macOS/Metal; no GPU pass claims on this host" >&2
  exit 2
fi
if ! git diff --quiet || ! git diff --cached --quiet; then
  echo "Stage137 refuses to benchmark with tracked source modifications" >&2
  exit 2
fi
EXPECTED_BRANCH="perf/batching-stage137-raster-coverage-20261120"
HEAD="$(git rev-parse HEAD)"
if ! git merge-base --is-ancestor "origin/$EXPECTED_BRANCH" HEAD 2>/dev/null; then
  echo "Stage137: fetch and create a fresh detached worktree at origin/$EXPECTED_BRANCH" >&2
  echo "Current HEAD=$HEAD" >&2
  exit 2
fi
BUILD="${AIVERSE_BATCH_BUILD_DIR:-$ROOT/build/batching-stage137}"
CAPTURES="${AIVERSE_BATCH_CAPTURE_ROOT:-$BUILD/captures}"
JOBS="${AIVERSE_BATCH_BUILD_JOBS:-8}"
if ! [[ "$JOBS" =~ ^[1-9][0-9]*$ ]] || (( JOBS > 32 )); then
  echo "AIVERSE_BATCH_BUILD_JOBS must be 1..32" >&2
  exit 2
fi
if [[ "$BUILD" != /* || "$CAPTURES" != /* ]]; then
  echo "Build and capture paths must be absolute" >&2
  exit 2
fi
mkdir -p "$BUILD" "$CAPTURES"

# Save this file alongside the capture to establish the host/compiler/binary
# for an archive; it is diagnostic metadata, not proof of GPU timings.
PROVENANCE="$BUILD/native-provenance.txt"
{
  printf 'stage=137\n'
  printf 'repo_head=%s\n' "$HEAD"
  printf 'repo_branch=%s\n' "$(git branch --show-current)"
  printf 'uname=%s\n' "$(uname -a)"
  printf 'macos=%s\n' "$(sw_vers -productVersion)"
  printf 'mac_model=%s\n' "$(sysctl -n hw.model)"
  printf 'cmake=%s\n' "$(cmake --version | sed -n '1p')"
  printf 'clang=%s\n' "$(xcrun clang --version | sed -n '1p')"
} > "$PROVENANCE"

cmake -S "$ROOT" -B "$BUILD" \
  -DAIVERSE_BUILD_HDR_TESTS=ON -DAIVERSE_BUILD_TESTS=OFF \
  -DAIVERSE_BUILD_EDITOR=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" --target aiverse_material_batches --parallel "$JOBS"
BIN="$BUILD/aiverse_material_batches"
if [[ ! -x "$BIN" ]]; then
  echo "Stage137 missing rebuilt executable $BIN" >&2
  exit 2
fi
printf 'binary_sha256=%s\n' "$(shasum -a 256 "$BIN" | awk '{print $1}')" >> "$PROVENANCE"

bash -n tools/batching-stage137-trials.sh
bash -n tools/batching-stage137-native-mac.sh
python3 -m py_compile tools/batching-stage127-analyze.py \
  tools/batching-stage137-analyze.py
python3 tools/batching-stage137-analyze.py --self-test
"$BIN" --unit

AIVERSE_BATCH_BENCH_BIN="$BIN" \
AIVERSE_BATCH_CAPTURE_ROOT="$CAPTURES" \
AIVERSE_BATCH_NATIVE_PROVENANCE_FILE="$PROVENANCE" \
  bash tools/batching-stage137-trials.sh
printf 'Stage137 native acceptance: 16-process coverage capture completed; inspect Stage137 timing and pixel area controls\n'
