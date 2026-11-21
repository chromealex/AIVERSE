#!/usr/bin/env bash
# Stage 86: three clean AUTO timeline trials interleaved with forced CPU MDI controls.
# CPU wall-time only; GPU duration/FPS is not measured by this fixture.
set -euo pipefail
cd "$(dirname "$0")/.."
BIN="${AIVERSE_BATCH_BENCH_BIN:-build/batching-stage82/aiverse_material_batches}"
ITEMS="${1:-1024}"
OUT="${2:-build/batching-stage82/stage86-trials}"
if ! [[ "$ITEMS" =~ ^[0-9]+$ ]] || (( ITEMS < 64 || ITEMS > 100000 )); then
  echo "ERROR: items must be 64..100000" >&2
  exit 2
fi
if [[ ! -x "$BIN" ]]; then
  echo "ERROR: binary missing: $BIN (build aiverse_material_batches first)" >&2
  exit 2
fi
mkdir -p "$OUT"
run_case() {
  local label="$1" mode="$2"
  local prefix="$OUT/$label" log="$OUT/$label.log"
  echo "===== Stage 86 $label ($mode) ====="
  local -a flags=(AIVERSE_PALETTE_TEST_CASE=auto_stress
    AIVERSE_BATCH_STRESS_ITEMS="$ITEMS"
    AIVERSE_BATCH_CPU_TIMING=1
    AIVERSE_BATCH_FRAME_TIMELINE=1
    AIVERSE_CAPTURE_SUCCESSFUL_MDI=1
    AIVERSE_BATCH_STRESS_POLICY="$mode")
  if [[ "$mode" == auto ]]; then
    flags+=(AIVERSE_BATCH_AUTO=1)
  else
    flags+=(AIVERSE_METAL_CPU_MDI=1)
  fi
  env -u AIVERSE_BATCH_AUTO -u AIVERSE_BATCH_AUTO_COST_TRACE \
    -u AIVERSE_BATCH_AUTO_TRACE -u AIVERSE_ICB_PROFILE \
    -u AIVERSE_EXPERIMENTAL_METAL_ICB -u AIVERSE_GPU_ENCODED_ICB \
    -u AIVERSE_GPU_AUTHORED_DRAWS -u AIVERSE_GPU_ICB_RIGID \
    -u AIVERSE_BATCH_BENCH_MODE \
    "${flags[@]}" "$BIN" unused.obj "$prefix" 2>&1 | tee "$log"
  grep -Eq 'material palette CPU checks=[0-9]+ result=passed' "$log"
  grep -Eq 'actual MDI pixel parity frame=[0-9]+ changed_channels=0' "$log"
  grep -q 'changed_channels=0' "$log"
  grep -q 'stress frame timeline samples=106' "$log"
  test -s "$prefix-frames.csv"
  if [[ "$mode" == mdi ]]; then
    grep -Eq 'stress benchmark policy=mdi measured_mdi_frames=[1-9][0-9]*' "$log"
  fi
}
# Interleave forced-MDI reference runs to distinguish policy-specific
# degradations from a changing device state.
run_case auto1 auto
run_case mdi1 mdi
run_case auto2 auto
run_case mdi2 mdi
run_case auto3 auto
python3 tools/batching-stage86-analyze.py "$OUT" "$ITEMS"
echo "Stage 86 PASS: 3 AUTO + 2 MDI runs; report: $OUT/summary.csv"
