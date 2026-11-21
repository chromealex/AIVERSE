#!/usr/bin/env bash
# Stage 119: isolate model work by changing TEST ONLY shadow/opaque draw masks.
# This is a diagnostic workload comparison, not a per-pass timestamp profiler.
set -euo pipefail
cd "$(dirname "$0")/.."
BIN="${AIVERSE_BATCH_BENCH_BIN:-build/batching-stage119/aiverse_material_batches}"
BASE="${AIVERSE_BATCH_CAPTURE_ROOT:-build/batching-stage119/captures}"
if [[ ! -x "$BIN" ]]; then echo "Benchmark executable not found: $BIN" >&2; exit 2; fi
mkdir -p "$BASE"
OUT="$BASE/capture-$(date -u +%Y%m%dT%H%M%SZ)-$$"
mkdir "$OUT"
{
 echo "stage=119"
 echo "utc_start=$(date -u +%Y%m%dT%H%M%SZ)"
 echo "git_head=$(git rev-parse HEAD)"
 echo "binary=$BIN"
 echo "items=2048"
 echo "phases=full,shadow,opaque"
 echo "modes=direct,mdi"
 echo "gpu=whole_command_buffer_timestamps"
 echo "gpu_model_boundary_counter_sampling=disabled"
 echo "warning=changed_workloads_not_exact_individual_pass_timing"
} > "$OUT/manifest.txt"
CURRENT_LOG=""
on_error() {
 status=$?
 trap - ERR
 echo "Stage 119 FAILED (exit $status); folder $OUT" >&2
 if [[ -n "$CURRENT_LOG" && -f "$CURRENT_LOG" ]]; then tail -n 70 "$CURRENT_LOG" >&2; fi
 echo "result=FAILED" >> "$OUT/manifest.txt"
 tar -czf "$OUT.tar.gz" -C "$BASE" "$(basename "$OUT")" || true
 echo "Stage 119 diagnostic archive: $OUT.tar.gz" >&2
 exit "$status"
}
trap on_error ERR
for phase in full shadow opaque; do
 for mode in direct mdi; do
  name="$OUT/2048-$phase-$mode"
  CURRENT_LOG="$name.log"
  echo "Stage 119: 2048 $phase $mode"
  env -u AIVERSE_BATCH_AUTO -u AIVERSE_BATCH_AUTO_COST_TRACE \
      -u AIVERSE_BATCH_AUTO_TRACE -u AIVERSE_ICB_PROFILE \
      -u AIVERSE_ICB_SLOW_TRACE -u AIVERSE_BATCH_MODEL_GPU_COUNTERS \
      -u AIVERSE_EXPERIMENTAL_METAL_ICB -u AIVERSE_GPU_ENCODED_ICB \
      -u AIVERSE_GPU_AUTHORED_DRAWS -u AIVERSE_GPU_ICB_RIGID \
      -u AIVERSE_METAL_TRACE_FRAME -u AIVERSE_METAL_TRACE_PATH \
      AIVERSE_PALETTE_TEST_CASE=auto_stress \
      AIVERSE_BATCH_STRESS_ITEMS=2048 \
      AIVERSE_BATCH_STRESS_POLICY="$mode" \
      AIVERSE_BATCH_DIAGNOSTIC_MODEL_PASS="$phase" \
      AIVERSE_METAL_CPU_MDI=1 AIVERSE_BATCH_CPU_TIMING=1 \
      AIVERSE_BATCH_FRAME_TIMELINE=1 AIVERSE_BATCH_GPU_TIMELINE=1 \
      AIVERSE_CAPTURE_SUCCESSFUL_MDI="$([[ "$mode" == mdi ]] && echo 1 || echo 0)" \
      "$BIN" unused.obj "$name" > "$name.log" 2>&1
  grep -Eq '^material palette CPU checks=[0-9]+ result=passed$' "$name.log"
  grep -Fq "stress diagnostic model pass=$phase" "$name.log"
  grep -Eq '^GPU command-buffer timeline samples=66 path=' "$name.log"
  grep -qx 'changed_channels=0' "$name.log"
  test -s "$name-frames.csv"
  test -s "$name-gpu-frames.csv"
  if [[ "$mode" == mdi ]]; then
    grep -Eq '^actual MDI pixel parity frame=[0-9]+ changed_channels=0$' "$name.log"
    grep -Eq '^stress benchmark policy=mdi measured_mdi_frames=81 measured_direct_frames=0 measured_mdi_submissions=[1-9][0-9]*$' "$name.log"
  else
    grep -Eq '^stress benchmark policy=direct measured_mdi_frames=0 measured_direct_frames=81 measured_mdi_submissions=0$' "$name.log"
  fi
  echo "PASS: 2048 $phase $mode"
  CURRENT_LOG=""
 done
done
python3 tools/batching-stage119-analyze.py "$OUT"
echo "result=PASS" >> "$OUT/manifest.txt"
echo "utc_end=$(date -u +%Y%m%dT%H%M%SZ)" >> "$OUT/manifest.txt"
tar -czf "$OUT.tar.gz" -C "$BASE" "$(basename "$OUT")"
echo "Stage 119 archive: $OUT.tar.gz"
