#!/usr/bin/env bash
# Matched native Metal CPU wall timing: 2K/4K unique primitives, Direct/MDI.
# Each size uses D-M-M-D process order to reduce single-direction drift.
set -euo pipefail
cd "$(dirname "$0")/.."
BIN="${AIVERSE_BATCH_BENCH_BIN:-build/batching-stage98/aiverse_material_batches}"
BASE="${AIVERSE_BATCH_CAPTURE_ROOT:-build/batching-stage98/stage104}"
if [[ ! -x "$BIN" ]]; then
  echo "Missing executable: $BIN" >&2
  exit 2
fi
mkdir -p "$BASE"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
OUT="$BASE/capture-$STAMP-$$"
mkdir "$OUT"
{
  echo "stage=104"
  echo "utc_start=$STAMP"
  echo "git_head=$(git rev-parse HEAD 2>/dev/null || echo unknown)"
  echo "binary=$BIN"
  echo "sizes=2048,4096"
  echo "order=direct1,mdi1,mdi2,direct2"
  echo "timings=CPU_renderer_render_wall_and_Mach_thread"
  echo "gpu_timestamps=not_measured"
} > "$OUT/manifest.txt"
for items in 2048 4096; do
  for label in direct1 mdi1 mdi2 direct2; do
    mode=direct
    if [[ "$label" == mdi* ]]; then mode=mdi; fi
    name="$OUT/$items-$label"
    echo "Stage 104: $items unique primitives; $label ($mode)"
    flags=(AIVERSE_PALETTE_TEST_CASE=auto_stress
      AIVERSE_BATCH_STRESS_ITEMS="$items"
      AIVERSE_BATCH_STRESS_POLICY="$mode"
      AIVERSE_METAL_CPU_MDI=1
      AIVERSE_BATCH_CPU_TIMING=1
      AIVERSE_BATCH_THREAD_CPU_TIMING=1
      AIVERSE_BATCH_FRAME_TIMELINE=1)
    if [[ "$mode" == mdi ]]; then
      flags+=(AIVERSE_CAPTURE_SUCCESSFUL_MDI=1)
    else
      flags+=(AIVERSE_CAPTURE_SUCCESSFUL_MDI=0)
    fi
    env -u AIVERSE_BATCH_AUTO -u AIVERSE_BATCH_AUTO_COST_TRACE \
        -u AIVERSE_BATCH_AUTO_TRACE -u AIVERSE_ICB_PROFILE \
        -u AIVERSE_ICB_SLOW_TRACE -u AIVERSE_EXPERIMENTAL_METAL_ICB \
        -u AIVERSE_GPU_ENCODED_ICB -u AIVERSE_GPU_AUTHORED_DRAWS \
        -u AIVERSE_GPU_ICB_RIGID -u AIVERSE_BATCH_BENCH_MODE \
        "${flags[@]}" "$BIN" unused.obj "$name" > "$name.log" 2>&1
    grep -Eq '^material palette CPU checks=[0-9]+ result=passed$' "$name.log"
    grep -qx 'changed_channels=0' "$name.log"
    grep -q 'stress frame timeline samples=106' "$name.log"
    test -s "$name-frames.csv"
    if [[ "$mode" == mdi ]]; then
      grep -Eq '^actual MDI pixel parity frame=[0-9]+ changed_channels=0$' "$name.log"
      grep -Eq '^stress benchmark policy=mdi measured_mdi_frames=81 measured_direct_frames=0 measured_mdi_submissions=[1-9][0-9]*$' "$name.log"
    else
      grep -Eq '^stress benchmark policy=direct measured_mdi_frames=0 measured_direct_frames=81 measured_mdi_submissions=0$' "$name.log"
    fi
    echo "PASS: $items $label"
  done
done
python3 tools/batching-stage104-analyze.py "$OUT"
echo "utc_end=$(date -u +%Y%m%dT%H%M%SZ)" >> "$OUT/manifest.txt"
echo "result=PASS" >> "$OUT/manifest.txt"
tar -czf "$OUT.tar.gz" -C "$BASE" "$(basename "$OUT")"
echo "Stage 104 report: $OUT/stage104-summary.csv"
echo "Stage 104 archive: $OUT.tar.gz"
