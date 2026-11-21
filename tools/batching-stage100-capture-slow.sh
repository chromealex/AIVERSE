#!/usr/bin/env bash
# Bounded replay: stop at first reproducibly slow forced CPU-MDI process.
set -euo pipefail
cd "$(dirname "$0")/.."
BIN="${AIVERSE_BATCH_BENCH_BIN:-build/batching-stage98/aiverse_material_batches}"
OUT="${1:-build/batching-stage98/slow-capture}"
MAX_RUNS="${2:-8}"
if ! [[ "$MAX_RUNS" =~ ^[0-9]+$ ]] || (( MAX_RUNS < 1 || MAX_RUNS > 20 )); then
  echo "MAX_RUNS must be 1..20" >&2; exit 2
fi
if [[ ! -x "$BIN" ]]; then echo "Executable missing: $BIN" >&2; exit 2; fi
mkdir -p "$OUT"
printf 'run,mdi_frames,slow_frames,wall_median_ms,mach_median_ms,offcpu_median_ms,backend_median_ms\n' > "$OUT/summary.csv"
found=0
for (( run=1; run<=MAX_RUNS; run++ )); do
  name="$OUT/mdi-$run"
  echo "Stage 100 forced CPU MDI run $run/$MAX_RUNS"
  env -u AIVERSE_BATCH_AUTO -u AIVERSE_BATCH_AUTO_COST_TRACE \
      -u AIVERSE_BATCH_AUTO_TRACE -u AIVERSE_ICB_PROFILE \
      -u AIVERSE_ICB_SLOW_TRACE -u AIVERSE_EXPERIMENTAL_METAL_ICB \
      -u AIVERSE_GPU_ENCODED_ICB -u AIVERSE_GPU_AUTHORED_DRAWS \
      AIVERSE_PALETTE_TEST_CASE=auto_stress \
      AIVERSE_BATCH_STRESS_ITEMS=1024 AIVERSE_BATCH_STRESS_POLICY=mdi \
      AIVERSE_METAL_CPU_MDI=1 AIVERSE_BATCH_CPU_TIMING=1 \
      AIVERSE_BATCH_THREAD_CPU_TIMING=1 AIVERSE_BATCH_FRAME_TIMELINE=1 \
      AIVERSE_CAPTURE_SUCCESSFUL_MDI=1 \
      "$BIN" unused.obj "$name" 2>&1 | tee "$name.log"
  grep -Eq 'material palette CPU checks=[0-9]+ result=passed' "$name.log"
  grep -Eq 'actual MDI pixel parity frame=[0-9]+ changed_channels=0' "$name.log"
  grep -qx 'changed_channels=0' "$name.log"
  grep -Eq 'stress benchmark policy=mdi measured_mdi_frames=81 measured_direct_frames=0 measured_mdi_submissions=810' "$name.log"
  test -s "$name-frames.csv"
  slow=$(python3 - "$name-frames.csv" "$run" "$OUT/summary.csv" <<'PY'
import csv, math, statistics, sys
with open(sys.argv[1], newline='') as src:
    rows = list(csv.DictReader(src))
if [int(r["frame"]) for r in rows] != list(range(90, 196)):
    raise SystemExit("Invalid frame range")
rows = [r for r in rows if 115 <= int(r["frame"]) <= 195]
if len(rows) != 81 or any(int(r["mdi_submissions"]) != 10 or int(r["mdi_commands"]) != 2048 for r in rows):
    raise SystemExit("Unexpected MDI submissions/commands")
def col(key):
    values = [float(r[key]) for r in rows]
    if any(not math.isfinite(v) or v < 0 for v in values):
        raise SystemExit("Invalid "+key)
    return values
wall, mach = col("render_cpu_ms"), col("render_mach_thread_cpu_ms")
backend = [float(r["shadow_backend_ms"]) + float(r["opaque_backend_ms"]) for r in rows]
offcpu = [max(0., w - t) for w,t in zip(wall, mach)]
slow = sum(w > 1.2 for w in wall)
with open(sys.argv[3], "a") as out:
    out.write(f"{sys.argv[2]},81,{slow},{statistics.median(wall):.6f},{statistics.median(mach):.6f},{statistics.median(offcpu):.6f},{statistics.median(backend):.6f}\n")
print(slow)
PY
)
  echo "Slow MDI frames: $slow/81 (threshold 1.2 ms)"
  if (( slow >= 10 )); then
    echo "REPRODUCED: $name-frames.csv (10+ slow frames)"
    found=1
    break
  fi
done
if (( ! found )); then
  echo "INCONCLUSIVE: no significant slow run within $MAX_RUNS processes"
fi
echo "Summary: $OUT/summary.csv"
