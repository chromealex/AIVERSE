#!/usr/bin/env bash
# Stage 111: opt-in Metal draw-boundary GPU counter ticks + whole CB control.
set -euo pipefail
cd "$(dirname "$0")/.."
BIN="${AIVERSE_BATCH_BENCH_BIN:-build/batching-stage111/aiverse_material_batches}"
BASE="${AIVERSE_BATCH_CAPTURE_ROOT:-build/batching-stage111/captures}"
if [[ ! -x "$BIN" ]]; then echo "Missing benchmark binary: $BIN" >&2; exit 2; fi
mkdir -p "$BASE"
OUT="$BASE/capture-$(date -u +%Y%m%dT%H%M%SZ)-$$"
mkdir "$OUT"
CURRENT_LOG=""
failure_report() {
  local status="$?"
  trap - ERR
  echo "Stage 111 FAILED: exit=$status" >&2
  echo "Capture directory: $OUT" >&2
  if [[ -n "$CURRENT_LOG" && -f "$CURRENT_LOG" ]]; then
    echo "==== Last 70 lines of failing native log ====" >&2
    tail -n 70 "$CURRENT_LOG" >&2
  fi
  echo "utc_end=$(date -u +%Y%m%dT%H%M%SZ)" >> "$OUT/manifest.txt"
  echo "result=FAILED" >> "$OUT/manifest.txt"
  tar -czf "$OUT.tar.gz" -C "$BASE" "$(basename "$OUT")" || true
  echo "Stage 111 failure archive: $OUT.tar.gz" >&2
  exit "$status"
}
trap failure_report ERR
{
 echo "stage=111"
 echo "utc_start=$(date -u +%Y%m%dT%H%M%SZ)"
 echo "git_head=$(git rev-parse HEAD)"
 echo "binary=$BIN"
 echo "order=2048-direct,2048-mdi,4096-direct,4096-mdi"
 echo "scoped_gpu_metric=raw_timestamp_counter_ticks"
 echo "gpu_cb_metric=GPUStartTime_to_GPUEndTime_ms"
 echo "gpu_clock_calibrated=no"
} > "$OUT/manifest.txt"
for items in 2048 4096; do
 for mode in direct mdi; do
  name="$OUT/$items-$mode"
  CURRENT_LOG="$name.log"
  echo "Stage 111: $items unique primitives; $mode"
  if env -u AIVERSE_BATCH_AUTO -u AIVERSE_BATCH_AUTO_COST_TRACE \
      -u AIVERSE_BATCH_AUTO_TRACE -u AIVERSE_ICB_PROFILE \
      -u AIVERSE_ICB_SLOW_TRACE -u AIVERSE_EXPERIMENTAL_METAL_ICB \
      -u AIVERSE_GPU_ENCODED_ICB -u AIVERSE_GPU_AUTHORED_DRAWS \
      -u AIVERSE_GPU_ICB_RIGID -u AIVERSE_BATCH_BENCH_MODE \
      AIVERSE_PALETTE_TEST_CASE=auto_stress \
      AIVERSE_BATCH_STRESS_ITEMS="$items" \
      AIVERSE_BATCH_STRESS_POLICY="$mode" AIVERSE_METAL_CPU_MDI=1 \
      AIVERSE_BATCH_CPU_TIMING=1 AIVERSE_BATCH_FRAME_TIMELINE=1 \
      AIVERSE_BATCH_GPU_TIMELINE=1 AIVERSE_BATCH_MODEL_GPU_COUNTERS=1 \
      AIVERSE_CAPTURE_SUCCESSFUL_MDI="$([[ "$mode" == mdi ]] && echo 1 || echo 0)" \
      "$BIN" unused.obj "$name" > "$name.log" 2>&1; then
    :
  else
    native_status=$?
    if [[ "$native_status" == 77 ]] &&
       grep -q 'GPU model-pass draw-boundary timestamp counters UNSUPPORTED' "$name.log"; then
      echo "Stage 111 UNSUPPORTED: this Metal device has no draw-boundary timestamp counter capability" >&2
      echo "utc_end=$(date -u +%Y%m%dT%H%M%SZ)" >> "$OUT/manifest.txt"
      echo "result=UNSUPPORTED" >> "$OUT/manifest.txt"
      echo "capability=MTLCounterSamplingPointAtDrawBoundary" >> "$OUT/manifest.txt"
      tar -czf "$OUT.tar.gz" -C "$BASE" "$(basename "$OUT")"
      echo "Stage 111 unsupported archive: $OUT.tar.gz"
      exit 0
    fi
    echo "Stage 111 native fixture failed (status=$native_status)" >&2
    false
  fi
  grep -Eq '^material palette CPU checks=[0-9]+ result=passed$' "$name.log"
  grep -qx 'changed_channels=0' "$name.log"
  grep -Eq '^GPU command-buffer timeline samples=66 path=' "$name.log"
  grep -Eq '^GPU model counter timeline samples=66 path=' "$name.log"
  test -s "$name-gpu-frames.csv"
  test -s "$name-model-gpu-counters.csv"
  test -s "$name-frames.csv"
  if [[ "$mode" == mdi ]]; then
   grep -Eq '^actual MDI pixel parity frame=[0-9]+ changed_channels=0$' "$name.log"
   grep -Eq '^stress benchmark policy=mdi measured_mdi_frames=81 measured_direct_frames=0 measured_mdi_submissions=[1-9][0-9]*$' "$name.log"
  else
   grep -Eq '^stress benchmark policy=direct measured_mdi_frames=0 measured_direct_frames=81 measured_mdi_submissions=0$' "$name.log"
  fi
  echo "PASS $items $mode GPU counters + CPU + parity"
  CURRENT_LOG=""
 done
done
python3 tools/batching-stage111-analyze.py "$OUT"
echo "utc_end=$(date -u +%Y%m%dT%H%M%SZ)" >> "$OUT/manifest.txt"
echo "result=PASS" >> "$OUT/manifest.txt"
tar -czf "$OUT.tar.gz" -C "$BASE" "$(basename "$OUT")"
echo "Stage 111 archive: $OUT.tar.gz"
