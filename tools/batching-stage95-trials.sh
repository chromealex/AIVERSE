#!/usr/bin/env bash
# Stage 95: run three independent CPU-MDI tests and compare thread CPU vs wall.
# Must be executed in a clean isolated worktree with a built fixture.
set -euo pipefail
cd "$(dirname "$0")/.."
BIN="${AIVERSE_BATCH_BENCH_BIN:-build/batching-stage95/aiverse_material_batches}"
OUT="${1:-build/batching-stage95/trials}"
ITEMS="${2:-1024}"
if ! [[ "$ITEMS" =~ ^[0-9]+$ ]] || (( ITEMS < 64 || ITEMS > 100000 )); then
  echo "Invalid item count (64..100000)" >&2
  exit 2
fi
if [[ ! -x "$BIN" ]]; then
  echo "Missing benchmark executable: $BIN" >&2
  exit 2
fi
mkdir -p "$OUT"
for run in 1 2 3; do
  echo "===== Stage 95 forced CPU MDI run $run ====="
  name="$OUT/mdi-$run"
  env -u AIVERSE_BATCH_AUTO -u AIVERSE_BATCH_AUTO_COST_TRACE \
      -u AIVERSE_BATCH_AUTO_TRACE -u AIVERSE_ICB_PROFILE \
      -u AIVERSE_ICB_SLOW_TRACE -u AIVERSE_EXPERIMENTAL_METAL_ICB \
      -u AIVERSE_GPU_ENCODED_ICB -u AIVERSE_GPU_AUTHORED_DRAWS \
      AIVERSE_PALETTE_TEST_CASE=auto_stress \
      AIVERSE_BATCH_STRESS_ITEMS="$ITEMS" \
      AIVERSE_BATCH_STRESS_POLICY=mdi \
      AIVERSE_METAL_CPU_MDI=1 \
      AIVERSE_BATCH_CPU_TIMING=1 \
      AIVERSE_BATCH_THREAD_CPU_TIMING=1 \
      AIVERSE_BATCH_FRAME_TIMELINE=1 \
      AIVERSE_CAPTURE_SUCCESSFUL_MDI=1 \
      "$BIN" unused.obj "$name" 2>&1 | tee "$name.log"
  grep -Eq 'material palette CPU checks=[0-9]+ result=passed' "$name.log"
  grep -Eq 'actual MDI pixel parity frame=[0-9]+ changed_channels=0' "$name.log"
  grep -q '^changed_channels=0$' "$name.log"
  grep -Eq 'stress benchmark policy=mdi measured_mdi_frames=81 ' "$name.log"
  grep -q 'stress frame timeline samples=106' "$name.log"
  test -s "$name-frames.csv"
done
python3 tools/batching-stage95-analyze.py \
  "$OUT/mdi-1-frames.csv" "$OUT/mdi-2-frames.csv" "$OUT/mdi-3-frames.csv"
echo "Stage 95 PASS: three native runs, CPU thread/wall CSV: $OUT/stage95-thread-summary.csv"
