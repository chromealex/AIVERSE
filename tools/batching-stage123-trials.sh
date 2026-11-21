#!/usr/bin/env bash
# Stage 123: matched Direct / CPU-MDI 1K/2K/4K with exactly 64 indirect
# submissions per MDI frame. Larger scenes raise commands/submission.
set -euo pipefail
cd "$(dirname "$0")/.."
BIN="${AIVERSE_BATCH_BENCH_BIN:-build/batching-stage121/aiverse_material_batches}"
BASE="${AIVERSE_BATCH_CAPTURE_ROOT:-build/batching-stage121/stage123}"
if [[ ! -x "$BIN" ]]; then
  echo "Missing Stage121 CPU-MDI benchmark binary: $BIN" >&2
  exit 2
fi
mkdir -p "$BASE"
OUT="$BASE/capture-$(date -u +%Y%m%dT%H%M%SZ)-$$"
mkdir "$OUT"
{
  echo "stage=123"
  echo "utc_start=$(date -u +%Y%m%dT%H%M%SZ)"
  echo "git_head=$(git rev-parse HEAD)"
  echo "binary=$BIN"
  echo "sizes=1024,2048,4096"
  echo "caps=32,64,128"
  echo "mode_order=direct1,mdi1,mdi2,direct2"
  echo "expected_mdi_submissions_per_measured_frame=64"
  echo "gpu_metric=whole_completed_Metal_command_buffer_timeline"
  echo "gpu_model_counter_sampling=off"
  echo "warning=different_scene_sizes_not_pure_per_command_GPU_timings"
} > "$OUT/manifest.txt"
CURRENT_LOG=""
on_error() {
  status=$?
  trap - ERR
  echo "Stage123 FAILED (exit=$status) in $OUT" >&2
  if [[ -n "$CURRENT_LOG" && -f "$CURRENT_LOG" ]]; then
    echo "===== Last 75 lines from $CURRENT_LOG =====" >&2
    tail -n 75 "$CURRENT_LOG" >&2
  fi
  echo "utc_end=$(date -u +%Y%m%dT%H%M%SZ)" >> "$OUT/manifest.txt"
  echo "result=FAILED" >> "$OUT/manifest.txt"
  tar -czf "$OUT.tar.gz" -C "$BASE" "$(basename "$OUT")" || true
  echo "Stage123 failed archive: $OUT.tar.gz" >&2
  exit "$status"
}
trap on_error ERR
for spec in 1024:32 2048:64 4096:128; do
  items="${spec%%:*}"
  cap="${spec##*:}"
  for trial in direct1 mdi1 mdi2 direct2; do
    mode=direct
    if [[ "$trial" == mdi* ]]; then mode=mdi; fi
    name="$OUT/$items-$trial"
    CURRENT_LOG="$name.log"
    echo "Stage123 items=$items cap=$cap trial=$trial"
    env -u AIVERSE_BATCH_AUTO -u AIVERSE_BATCH_AUTO_COST_TRACE \
        -u AIVERSE_BATCH_AUTO_TRACE -u AIVERSE_ICB_PROFILE \
        -u AIVERSE_ICB_SLOW_TRACE -u AIVERSE_BATCH_MODEL_GPU_COUNTERS \
        -u AIVERSE_EXPERIMENTAL_METAL_ICB -u AIVERSE_GPU_ENCODED_ICB \
        -u AIVERSE_GPU_AUTHORED_DRAWS -u AIVERSE_GPU_ICB_RIGID \
        -u AIVERSE_METAL_TRACE_FRAME -u AIVERSE_METAL_TRACE_PATH \
        -u AIVERSE_METAL_MDI_MIN_COMMANDS -u AIVERSE_BATCH_BENCH_MODE \
        AIVERSE_PALETTE_TEST_CASE=auto_stress \
        AIVERSE_BATCH_STRESS_ITEMS="$items" \
        AIVERSE_BATCH_STRESS_POLICY="$mode" \
        AIVERSE_BATCH_DIAGNOSTIC_MODEL_PASS=full \
        AIVERSE_BATCH_DIAGNOSTIC_MDI_GROUP_CAP="$cap" \
        AIVERSE_METAL_CPU_MDI=1 AIVERSE_BATCH_CPU_TIMING=1 \
        AIVERSE_BATCH_FRAME_TIMELINE=1 AIVERSE_BATCH_GPU_TIMELINE=1 \
        AIVERSE_CAPTURE_SUCCESSFUL_MDI="$([[ "$mode" == mdi ]] && echo 1 || echo 0)" \
        "$BIN" unused.obj "$name" > "$name.log" 2>&1
    grep -Eq '^material palette CPU checks=[0-9]+ result=passed$' "$name.log"
    grep -Fqx "stress diagnostic CPU MDI group cap=$cap" "$name.log"
    grep -qx 'changed_channels=0' "$name.log"
    grep -Eq '^GPU command-buffer timeline samples=66 path=' "$name.log"
    grep -Eq '^stress frame timeline samples=106 path=' "$name.log"
    test -s "$name-gpu-frames.csv"
    test -s "$name-frames.csv"
    if [[ "$mode" == mdi ]]; then
      grep -Eq '^actual MDI pixel parity frame=[0-9]+ changed_channels=0$' "$name.log"
      grep -Eq '^stress benchmark policy=mdi measured_mdi_frames=81 measured_direct_frames=0 measured_mdi_submissions=5184$' "$name.log"
    else
      grep -Eq '^stress benchmark policy=direct measured_mdi_frames=0 measured_direct_frames=81 measured_mdi_submissions=0$' "$name.log"
    fi
    echo "PASS $items $trial"
    CURRENT_LOG=""
  done
done
python3 tools/batching-stage123-analyze.py "$OUT"
echo "utc_end=$(date -u +%Y%m%dT%H%M%SZ)" >> "$OUT/manifest.txt"
echo "result=PASS" >> "$OUT/manifest.txt"
tar -czf "$OUT.tar.gz" -C "$BASE" "$(basename "$OUT")"
echo "Stage 123 archive: $OUT.tar.gz"
