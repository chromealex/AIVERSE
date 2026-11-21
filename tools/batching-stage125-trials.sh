#!/usr/bin/env bash
# Stage125: existing MaterialQuality Low=0, High=2. 2048 authored
# primitives, no production code changes, 8 mirrored independent processes.
set -euo pipefail
cd "$(dirname "$0")/.."
BIN="${AIVERSE_BATCH_BENCH_BIN:-build/batching-stage121/aiverse_material_batches}"
BASE="${AIVERSE_BATCH_CAPTURE_ROOT:-build/batching-stage121/stage125}"
if [[ ! -x "$BIN" ]]; then
 echo "Missing Stage121 benchmark executable: $BIN" >&2
 exit 2
fi
mkdir -p "$BASE"
OUT="$BASE/capture-$(date -u +%Y%m%dT%H%M%SZ)-$$"
mkdir "$OUT"
{
 echo "stage=125"
 echo "utc_start=$(date -u +%Y%m%dT%H%M%SZ)"
 echo "git_head=$(git rev-parse HEAD)"
 echo "binary=$BIN"
 echo "items=2048"
 echo "material_quality_low_enum=0"
 echo "material_quality_high_enum=2"
 echo "order=low-direct1,low-mdi1,high-direct1,high-mdi1,high-mdi2,high-direct2,low-mdi2,low-direct2"
 echo "gpu=whole_completed_Metal_command_buffer_timestamps"
 echo "note=no_shader_instruction_count_profiling"
} > "$OUT/manifest.txt"
CURRENT_LOG=""
on_error() {
 code=$?
 trap - ERR
 echo "Stage125 FAILED (exit=$code) in $OUT" >&2
 if [[ -n "$CURRENT_LOG" && -f "$CURRENT_LOG" ]]; then tail -n 75 "$CURRENT_LOG" >&2; fi
 echo "result=FAILED" >> "$OUT/manifest.txt"
 tar -czf "$OUT.tar.gz" -C "$BASE" "$(basename "$OUT")" || true
 echo "Stage125 partial archive: $OUT.tar.gz" >&2
 exit "$code"
}
trap on_error ERR
for entry in low:direct:1 low:mdi:1 high:direct:1 high:mdi:1 high:mdi:2 high:direct:2 low:mdi:2 low:direct:2; do
  IFS=: read -r quality mode rep <<< "$entry"
  quality_enum=0
  [[ "$quality" == high ]] && quality_enum=2
  name="$OUT/2048-$quality-$mode$rep"
  CURRENT_LOG="$name.log"
  echo "Stage125 $quality $mode repeat=$rep"
  env -u AIVERSE_BATCH_AUTO -u AIVERSE_BATCH_AUTO_COST_TRACE \
      -u AIVERSE_BATCH_AUTO_TRACE -u AIVERSE_ICB_PROFILE \
      -u AIVERSE_ICB_SLOW_TRACE -u AIVERSE_BATCH_MODEL_GPU_COUNTERS \
      -u AIVERSE_EXPERIMENTAL_METAL_ICB -u AIVERSE_GPU_ENCODED_ICB \
      -u AIVERSE_GPU_AUTHORED_DRAWS -u AIVERSE_GPU_ICB_RIGID \
      -u AIVERSE_METAL_TRACE_FRAME -u AIVERSE_METAL_TRACE_PATH \
      -u AIVERSE_METAL_MDI_MIN_COMMANDS -u AIVERSE_BATCH_BENCH_MODE \
      -u AIVERSE_MATERIALS \
      AIVERSE_PALETTE_TEST_CASE=auto_stress \
      AIVERSE_BATCH_STRESS_ITEMS=2048 \
      AIVERSE_BATCH_STRESS_POLICY="$mode" \
      AIVERSE_BATCH_DIAGNOSTIC_MODEL_PASS=full \
      AIVERSE_BATCH_DIAGNOSTIC_MDI_GROUP_CAP=0 \
      AIVERSE_PALETTE_TEST_QUALITY="$quality_enum" \
      AIVERSE_METAL_CPU_MDI=1 AIVERSE_BATCH_CPU_TIMING=1 \
      AIVERSE_BATCH_FRAME_TIMELINE=1 AIVERSE_BATCH_GPU_TIMELINE=1 \
      AIVERSE_CAPTURE_SUCCESSFUL_MDI="$([[ "$mode" == mdi ]] && echo 1 || echo 0)" \
      "$BIN" unused.obj "$name" > "$name.log" 2>&1
  grep -Eq '^material palette CPU checks=[0-9]+ result=passed$' "$name.log"
  grep -Fqx 'stress diagnostic CPU MDI group cap=0' "$name.log"
  grep -Fqx 'stress diagnostic model pass=full mask=3' "$name.log"
  grep -qx 'changed_channels=0' "$name.log"
  grep -Eq '^GPU command-buffer timeline samples=66 path=' "$name.log"
  grep -Eq '^stress frame timeline samples=106 path=' "$name.log"
  test -s "$name-gpu-frames.csv"
  test -s "$name-frames.csv"
  if [[ "$mode" == mdi ]]; then
    grep -Eq '^actual MDI pixel parity frame=[0-9]+ changed_channels=0$' "$name.log"
    grep -Eq '^stress benchmark policy=mdi measured_mdi_frames=81 measured_direct_frames=0 measured_mdi_submissions=1458$' "$name.log"
  else
    grep -Eq '^stress benchmark policy=direct measured_mdi_frames=0 measured_direct_frames=81 measured_mdi_submissions=0$' "$name.log"
  fi
  echo "PASS: $quality $mode #$rep"
  CURRENT_LOG=""
done
python3 tools/batching-stage125-analyze.py "$OUT"
echo "result=PASS" >> "$OUT/manifest.txt"
echo "utc_end=$(date -u +%Y%m%dT%H%M%SZ)" >> "$OUT/manifest.txt"
tar -czf "$OUT.tar.gz" -C "$BASE" "$(basename "$OUT")"
echo "Stage125 archive: $OUT.tar.gz"
