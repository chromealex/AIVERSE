#!/usr/bin/env bash
# Unique, self-contained Stage 102 CPU-MDI captures with Mach CPU timing.
# Run from any shell; script must live under repo/tools/.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"
BIN="${AIVERSE_BATCH_BENCH_BIN:-build/batching-stage98/aiverse_material_batches}"
MAX_RUNS="${1:-8}"
if ! [[ "$MAX_RUNS" =~ ^[0-9]+$ ]] || (( MAX_RUNS < 1 || MAX_RUNS > 20 )); then
  echo "Run count must be 1..20" >&2
  exit 2
fi
if [[ ! -x "$BIN" ]]; then
  echo "Benchmark executable missing: $BIN" >&2
  exit 2
fi
BASE="${AIVERSE_BATCH_CAPTURE_ROOT:-build/batching-stage98/stage102}"
mkdir -p "$BASE"
# Refuse to reuse old run directories; this avoids confusing fresh and old CSVs.
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
OUT="$BASE/capture-$STAMP-$$"
mkdir "$OUT"
GIT_SHA="$(git rev-parse HEAD 2>/dev/null || echo unknown)"
{
  echo "stage=102"
  echo "utc_start=$STAMP"
  echo "git_head=$GIT_SHA"
  echo "binary=$BIN"
  echo "max_runs=$MAX_RUNS"
  echo "items=1024"
  echo "slow_threshold_ms=1.2"
  echo "slow_run_min_frames=10"
} > "$OUT/manifest.txt"
printf 'run,mdi_frames,slow_frames,wall_median_ms,mach_median_ms,offcpu_median_ms,backend_median_ms,status\n' > "$OUT/summary.csv"
found=0
for (( run=1; run<=MAX_RUNS; run++ )); do
  name="$OUT/mdi-$run"
  echo "===== Stage 102 run $run/$MAX_RUNS, output=$OUT ====="
  # CPU-only path, no logging in a measured frame, and fixed MDI decisions.
  env -u AIVERSE_BATCH_AUTO -u AIVERSE_BATCH_AUTO_COST_TRACE \
      -u AIVERSE_BATCH_AUTO_TRACE -u AIVERSE_ICB_PROFILE \
      -u AIVERSE_ICB_SLOW_TRACE -u AIVERSE_EXPERIMENTAL_METAL_ICB \
      -u AIVERSE_GPU_ENCODED_ICB -u AIVERSE_GPU_AUTHORED_DRAWS \
      AIVERSE_PALETTE_TEST_CASE=auto_stress \
      AIVERSE_BATCH_STRESS_ITEMS=1024 AIVERSE_BATCH_STRESS_POLICY=mdi \
      AIVERSE_METAL_CPU_MDI=1 AIVERSE_BATCH_CPU_TIMING=1 \
      AIVERSE_BATCH_THREAD_CPU_TIMING=1 AIVERSE_BATCH_FRAME_TIMELINE=1 \
      AIVERSE_CAPTURE_SUCCESSFUL_MDI=1 \
      "$BIN" unused.obj "$name" > "$name.log" 2>&1
  grep -Eq 'material palette CPU checks=[0-9]+ result=passed' "$name.log"
  grep -Eq 'actual MDI pixel parity frame=[0-9]+ changed_channels=0' "$name.log"
  grep -qx 'changed_channels=0' "$name.log"
  grep -Eq 'stress benchmark policy=mdi measured_mdi_frames=81 measured_direct_frames=0 measured_mdi_submissions=810' "$name.log"
  grep -q 'stress frame timeline samples=106' "$name.log"
  test -s "$name-frames.csv"
  python3 - "$name-frames.csv" "$run" "$OUT/summary.csv" <<'PY'
import csv, math, statistics, sys
path, trial, summary = sys.argv[1:]
with open(path, newline="") as source:
    data = list(csv.DictReader(source))
if [int(r["frame"]) for r in data] != list(range(90, 196)):
    raise SystemExit("Invalid CSV frame sequence (expected 90..195)")
data = data[25:]
if len(data) != 81 or any(int(r["mdi_submissions"]) != 10 or int(r["mdi_commands"]) != 2048 for r in data):
    raise SystemExit("Invalid MDI workload")
def values(field):
    result = [float(r[field]) for r in data]
    if any(not math.isfinite(v) or v < 0 for v in result):
        raise SystemExit("Invalid or unsupported "+field)
    return result
wall = values("render_cpu_ms")
mach = values("render_mach_thread_cpu_ms")
backend = [a+b for a,b in zip(values("shadow_backend_ms"),values("opaque_backend_ms"))]
off = [max(0.,a-b) for a,b in zip(wall,mach)]
slow = sum(w>1.2 for w in wall)
status = "REPRODUCED" if slow>=10 else "FAST"
with open(summary, "a", newline="") as target:
    writer=csv.writer(target)
    writer.writerow([trial, 81, slow, *[f"{statistics.median(v):.6f}" for v in (wall,mach,off,backend)],status])
print(f"Trial {trial}: slow={slow}/81 wall_med={statistics.median(wall):.4f} "
      f"mach_med={statistics.median(mach):.4f} offcpu_med={statistics.median(off):.4f} status={status}")
PY
  if grep -q "^$run,81,[0-9]*,.*REPRODUCED$" "$OUT/summary.csv"; then
    found=1
    echo "first_slow_csv=mdi-$run-frames.csv" >> "$OUT/manifest.txt"
    break
  fi
done
if (( found )); then
  echo "result=REPRODUCED" >> "$OUT/manifest.txt"
else
  echo "result=INCONCLUSIVE_NO_SLOW_RUN" >> "$OUT/manifest.txt"
fi
echo "utc_end=$(date -u +%Y%m%dT%H%M%SZ)" >> "$OUT/manifest.txt"
# One easy-to-share bundle, retaining all untouched CSVs, fixture logs and metadata.
ARCHIVE="$OUT.tar.gz"
tar -czf "$ARCHIVE" -C "$BASE" "$(basename "$OUT")"
echo "Stage 102 summary: $OUT/summary.csv"
echo "Stage 102 archive: $ARCHIVE"
echo "Stage 102 result: $(grep '^result=' "$OUT/manifest.txt")"
