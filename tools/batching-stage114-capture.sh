#!/usr/bin/env bash
# Stage 114: programmatic Metal GPU frame captures for Direct vs CPU MDI.
# Optional first argument: 2048 (default), 4096, or both.
# The .gputrace must be opened in Xcode. This is NOT an FPS/GPU-timing test.
set -euo pipefail
cd "$(dirname "$0")/.."
BIN="${AIVERSE_BATCH_BENCH_BIN:-build/batching-stage114/aiverse_material_batches}"
BASE="${AIVERSE_BATCH_CAPTURE_ROOT:-build/batching-stage114/traces}"
SIZE="${1:-2048}"
case "$SIZE" in
  2048) ITEMS=(2048);;
  4096) ITEMS=(4096);;
  both) ITEMS=(2048 4096);;
  *) echo "Usage: batching-stage114-capture.sh [2048|4096|both]" >&2; exit 2;;
esac
if [[ ! -x "$BIN" ]]; then
  echo "Stage 114 fixture executable missing: $BIN" >&2; exit 2
fi
mkdir -p "$BASE"
OUT="$BASE/capture-$(date -u +%Y%m%dT%H%M%SZ)-$$"
mkdir "$OUT"
{
  echo "stage=114"
  echo "utc_start=$(date -u +%Y%m%dT%H%M%SZ)"
  echo "git_head=$(git rev-parse HEAD)"
  echo "binary=$BIN"
  echo "items=$SIZE"
  echo "capture_frame_metal_serial=131"
  echo "model_scoped_gpu_counters=disabled"
  echo "type=Metal GPU Frame Capture, not benchmark"
} > "$OUT/manifest.txt"
CURRENT_LOG=""
on_failure() {
  status=$?
  trap - ERR
  echo "Stage 114 FAILED (code=$status), directory: $OUT" >&2
  if [[ -n "$CURRENT_LOG" && -f "$CURRENT_LOG" ]]; then
    tail -n 60 "$CURRENT_LOG" >&2
  fi
  echo "result=FAILED" >> "$OUT/manifest.txt"
  exit "$status"
}
trap on_failure ERR
for items in "${ITEMS[@]}"; do
  for mode in direct mdi; do
    name="$OUT/$items-$mode"
    trace="$PWD/$name.gputrace"
    CURRENT_LOG="$name.log"
    echo "Stage 114 capturing $items $mode: $trace"
    env -u AIVERSE_BATCH_AUTO -u AIVERSE_BATCH_AUTO_TRACE \
        -u AIVERSE_BATCH_AUTO_COST_TRACE -u AIVERSE_ICB_PROFILE \
        -u AIVERSE_ICB_SLOW_TRACE -u AIVERSE_BATCH_MODEL_GPU_COUNTERS \
        -u AIVERSE_EXPERIMENTAL_METAL_ICB -u AIVERSE_GPU_ENCODED_ICB \
        -u AIVERSE_GPU_AUTHORED_DRAWS -u AIVERSE_GPU_ICB_RIGID \
        MTL_CAPTURE_ENABLED=1 \
        AIVERSE_METAL_TRACE_FRAME=131 \
        AIVERSE_METAL_TRACE_PATH="$trace" \
        AIVERSE_PALETTE_TEST_CASE=auto_stress \
        AIVERSE_BATCH_STRESS_ITEMS="$items" \
        AIVERSE_BATCH_STRESS_POLICY="$mode" AIVERSE_METAL_CPU_MDI=1 \
        AIVERSE_BATCH_CPU_TIMING=1 AIVERSE_BATCH_FRAME_TIMELINE=1 \
        AIVERSE_CAPTURE_SUCCESSFUL_MDI="$([[ "$mode" == mdi ]] && echo 1 || echo 0)" \
        "$BIN" unused.obj "$name" > "$name.log" 2>&1
    grep -Eq '^\[metal-gputrace\] START frame=131 path=' "$name.log"
    grep -Eq '^\[metal-gputrace\] STOP frame=131 path=' "$name.log"
    grep -Eq '^material palette CPU checks=[0-9]+ result=passed$' "$name.log"
    grep -qx 'changed_channels=0' "$name.log"
    if [[ "$mode" == mdi ]]; then
      grep -Eq '^actual MDI pixel parity frame=[0-9]+ changed_channels=0$' "$name.log"
      grep -Eq '^stress benchmark policy=mdi measured_mdi_frames=81 measured_direct_frames=0 measured_mdi_submissions=[1-9][0-9]*$' "$name.log"
    else
      grep -Eq '^stress benchmark policy=direct measured_mdi_frames=0 measured_direct_frames=81 measured_mdi_submissions=0$' "$name.log"
    fi
    # A trace may be a package/directory. Test -e rather than -s.
    if [[ ! -e "$trace" ]]; then
      echo "GPU trace output was not generated: $trace" >&2
      false
    fi
    echo "PASS: $items $mode trace exists: $trace"
    echo "trace_$items-$mode=$trace" >> "$OUT/manifest.txt"
    CURRENT_LOG=""
  done
done
echo "utc_end=$(date -u +%Y%m%dT%H%M%SZ)" >> "$OUT/manifest.txt"
echo "result=PASS" >> "$OUT/manifest.txt"
echo "Stage 114 traces: $OUT"
echo "Open the .gputrace files directly in Xcode to inspect ICB and GPU work."
