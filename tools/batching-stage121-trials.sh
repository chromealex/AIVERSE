#!/usr/bin/env bash
# Stage 121: test-only CPU MDI compatible-run caps 32/64/128/default.
# Cross-order D,32,64,128,default,default,128,64,32,D, 2048 items.
set -euo pipefail
cd "$(dirname "$0")/.."
BIN="${AIVERSE_BATCH_BENCH_BIN:-build/batching-stage121/aiverse_material_batches}"
BASE="${AIVERSE_BATCH_CAPTURE_ROOT:-build/batching-stage121/captures}"
if [[ ! -x "$BIN" ]]; then echo "Missing test binary: $BIN" >&2; exit 2; fi
mkdir -p "$BASE"
OUT="$BASE/capture-$(date -u +%Y%m%dT%H%M%SZ)-$$"
mkdir "$OUT"
{
 echo "stage=121"
 echo "utc_start=$(date -u +%Y%m%dT%H%M%SZ)"
 echo "git_head=$(git rev-parse HEAD)"
 echo "binary=$BIN"
 echo "items=2048"
 echo "order=direct1,cap32a,cap64a,cap128a,defaulta,defaultb,cap128b,cap64b,cap32b,direct2"
 echo "gpu_metric=whole_completed_Metal_command_buffer_timeline"
 echo "gpu_counter_sampling=disabled"
 echo "warning=diagnostic_ICB_group_cap_changes_number_of_submissions"
} > "$OUT/manifest.txt"
CURRENT_LOG=""
on_error() {
  status=$?
  trap - ERR
  echo "Stage 121 FAILED (code=$status), output=$OUT" >&2
  if [[ -n "$CURRENT_LOG" && -f "$CURRENT_LOG" ]]; then
    tail -n 70 "$CURRENT_LOG" >&2
  fi
  echo "result=FAILED" >> "$OUT/manifest.txt"
  tar -czf "$OUT.tar.gz" -C "$BASE" "$(basename "$OUT")" || true
  echo "Stage 121 diagnostic archive: $OUT.tar.gz" >&2
  exit "$status"
}
trap on_error ERR
for label in direct1 cap32a cap64a cap128a defaulta defaultb cap128b cap64b cap32b direct2; do
  mode=mdi
  cap=0
  if [[ "$label" == direct* ]]; then
    mode=direct
  elif [[ "$label" == cap* ]]; then
    cap="${label#cap}"
    cap="${cap%?}"
  fi
  name="$OUT/2048-$label"
  CURRENT_LOG="$name.log"
  echo "Stage 121: $label mode=$mode group_cap=$cap"
  env -u AIVERSE_BATCH_AUTO -u AIVERSE_BATCH_AUTO_COST_TRACE \
      -u AIVERSE_BATCH_AUTO_TRACE -u AIVERSE_ICB_PROFILE \
      -u AIVERSE_ICB_SLOW_TRACE -u AIVERSE_BATCH_MODEL_GPU_COUNTERS \
      -u AIVERSE_EXPERIMENTAL_METAL_ICB -u AIVERSE_GPU_ENCODED_ICB \
      -u AIVERSE_GPU_AUTHORED_DRAWS -u AIVERSE_GPU_ICB_RIGID \
      -u AIVERSE_METAL_TRACE_FRAME -u AIVERSE_METAL_TRACE_PATH \
      -u AIVERSE_METAL_MDI_MIN_COMMANDS -u AIVERSE_BATCH_BENCH_MODE \
      AIVERSE_PALETTE_TEST_CASE=auto_stress \
      AIVERSE_BATCH_STRESS_ITEMS=2048 AIVERSE_BATCH_STRESS_POLICY="$mode" \
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
  test -s "$name-frames.csv"
  test -s "$name-gpu-frames.csv"
  if [[ "$mode" == mdi ]]; then
    grep -Eq '^actual MDI pixel parity frame=[0-9]+ changed_channels=0$' "$name.log"
    grep -Eq '^stress benchmark policy=mdi measured_mdi_frames=81 measured_direct_frames=0 measured_mdi_submissions=[1-9][0-9]*$' "$name.log"
  else
    grep -Eq '^stress benchmark policy=direct measured_mdi_frames=0 measured_direct_frames=81 measured_mdi_submissions=0$' "$name.log"
  fi
  echo "PASS: $label"
  CURRENT_LOG=""
done
python3 tools/batching-stage121-analyze.py "$OUT"
echo "result=PASS" >> "$OUT/manifest.txt"
echo "utc_end=$(date -u +%Y%m%dT%H%M%SZ)" >> "$OUT/manifest.txt"
tar -czf "$OUT.tar.gz" -C "$BASE" "$(basename "$OUT")"
echo "Stage 121 archive: $OUT.tar.gz"
