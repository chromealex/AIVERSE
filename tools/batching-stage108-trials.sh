#!/usr/bin/env bash
# Stage 108: separate process matched Metal GPU command-buffer timing, 2K/4K.
set -euo pipefail
cd "$(dirname "$0")/.."
BIN="${AIVERSE_BATCH_BENCH_BIN:-build/batching-stage108/aiverse_material_batches}"
BASE="${AIVERSE_BATCH_CAPTURE_ROOT:-build/batching-stage108/captures}"
if [[ ! -x "$BIN" ]]; then echo "Benchmark binary missing: $BIN" >&2; exit 2; fi
mkdir -p "$BASE"
OUT="$BASE/capture-$(date -u +%Y%m%dT%H%M%SZ)-$$"
mkdir "$OUT"
{
  echo "stage=108"
  echo "utc_start=$(date -u +%Y%m%dT%H%M%SZ)"
  echo "git_head=$(git rev-parse HEAD)"
  echo "binary=$BIN"
  echo "order=2048-direct,2048-mdi,4096-direct,4096-mdi"
  echo "gpu_metric=whole_command_buffer_completion_timestamps"
  echo "gpu_model_pass_only=no"
} > "$OUT/manifest.txt"
for items in 2048 4096; do
  for mode in direct mdi; do
    name="$OUT/$items-$mode"
    echo "Stage 108: $items unique primitives; $mode GPU command-buffer timing"
    env -u AIVERSE_BATCH_AUTO -u AIVERSE_BATCH_AUTO_COST_TRACE \
        -u AIVERSE_BATCH_AUTO_TRACE -u AIVERSE_ICB_PROFILE \
        -u AIVERSE_ICB_SLOW_TRACE -u AIVERSE_EXPERIMENTAL_METAL_ICB \
        -u AIVERSE_GPU_ENCODED_ICB -u AIVERSE_GPU_AUTHORED_DRAWS \
        -u AIVERSE_GPU_ICB_RIGID -u AIVERSE_BATCH_BENCH_MODE \
        AIVERSE_PALETTE_TEST_CASE=auto_stress \
        AIVERSE_BATCH_STRESS_ITEMS="$items" \
        AIVERSE_BATCH_STRESS_POLICY="$mode" AIVERSE_METAL_CPU_MDI=1 \
        AIVERSE_BATCH_CPU_TIMING=1 AIVERSE_BATCH_FRAME_TIMELINE=1 \
        AIVERSE_BATCH_GPU_TIMELINE=1 \
        AIVERSE_CAPTURE_SUCCESSFUL_MDI="$([[ "$mode" == mdi ]] && echo 1 || echo 0)" \
        "$BIN" unused.obj "$name" > "$name.log" 2>&1
    grep -Eq '^material palette CPU checks=[0-9]+ result=passed$' "$name.log"
    grep -qx 'changed_channels=0' "$name.log"
    grep -Eq '^GPU command-buffer timeline samples=66 path=' "$name.log"
    test -s "$name-gpu-frames.csv"
    test -s "$name-frames.csv"
    if [[ "$mode" == mdi ]]; then
      grep -Eq '^actual MDI pixel parity frame=[0-9]+ changed_channels=0$' "$name.log"
      grep -Eq '^stress benchmark policy=mdi measured_mdi_frames=81 measured_direct_frames=0 measured_mdi_submissions=[1-9][0-9]*$' "$name.log"
    else
      grep -Eq '^stress benchmark policy=direct measured_mdi_frames=0 measured_direct_frames=81 measured_mdi_submissions=0$' "$name.log"
    fi
    echo "PASS $items $mode GPU+CPU and parity"
  done
done
python3 tools/batching-stage108-analyze.py "$OUT"
echo "utc_end=$(date -u +%Y%m%dT%H%M%SZ)" >> "$OUT/manifest.txt"
echo "result=PASS" >> "$OUT/manifest.txt"
tar -czf "$OUT.tar.gz" -C "$BASE" "$(basename "$OUT")"
echo "Stage 108 archive: $OUT.tar.gz"
