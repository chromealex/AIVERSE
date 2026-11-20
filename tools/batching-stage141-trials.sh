#!/usr/bin/env bash
# Stage141: empty ICB slot playback OFF vs ON at fixed allocation/reset.
# Fixed pooled N4 / 4x raster footprint. 12 independent processes (3 repeats).
set -euo pipefail
cd "$(dirname "$0")/.."
BIN="${AIVERSE_BATCH_BENCH_BIN:-build/batching-stage141/aiverse_material_batches}"
BASE="${AIVERSE_BATCH_CAPTURE_ROOT:-build/batching-stage141/captures}"
if [[ ! -x "$BIN" ]]; then
 echo "Missing rebuilt Stage141 benchmark executable: $BIN" >&2
 exit 2
fi
mkdir -p "$BASE"
OUT="$BASE/capture-$(date -u +%Y%m%dT%H%M%SZ)-$$"
mkdir "$OUT"
{
 echo "stage=141"
 echo "utc_start=$(date -u +%Y%m%dT%H%M%SZ)"
 echo "git_head=$(git rev-parse HEAD)"
 echo "binary=$BIN"
 echo "items=2048"
 echo "processes=12"
 echo "mesh_subdivisions=4"
 echo "triangle_scale=4"
 echo "geometry_pool_required=1"
 echo "geometry_pool_vertex_bytes=5898240"
 echo "geometry_pool_index_bytes=393216"
 echo "icb_inherit_pipeline=0"
 echo "icb_empty_tail_modes=off,on"
 echo "icb_reset_tail_slots=128"
 echo "icb_on_extra_executed_slots=128"
 echo "valid_draw_commands_per_frame=4086"
 echo "mdi_submissions_per_frame=18"
 echo "icb_inherit_pipeline_off=0"
 echo "icb_inherit_pipeline_on=0"
 echo "order=off-direct1,off-mdi1,on-direct1,on-mdi1,on-mdi2,on-direct2,off-mdi2,off-direct2,off-direct3,off-mdi3,on-direct3,on-mdi3"
 echo "gpu=whole_completed_Metal_command_buffer_timestamps"
 echo "note=both_modes_allocate_and_reset_plus_128_empty_slots_only_gpu_execution_range_differs"
} > "$OUT/manifest.txt"
# Keep Mac source/toolchain/binary provenance with its native GPU capture.
if [[ -n "${AIVERSE_BATCH_NATIVE_PROVENANCE_FILE:-}" ]]; then
  source_file="$AIVERSE_BATCH_NATIVE_PROVENANCE_FILE"
  [[ -s "$source_file" ]] || { echo "Missing native provenance" >&2; exit 2; }
  grep -Fqx "repo_head=$(git rev-parse HEAD)" "$source_file" || {
    echo "Native provenance HEAD mismatch" >&2; exit 2;
  }
  grep -Fqx 'stage=141' "$source_file" || {
    echo "Native provenance stage mismatch" >&2; exit 2;
  }
  grep -Eq '^binary_sha256=[0-9a-f]{64}$' "$source_file" || {
    echo "Native provenance binary SHA-256 missing" >&2; exit 2;
  }
  cp "$source_file" "$OUT/native-provenance.txt"
  echo "native_provenance=native-provenance.txt" >> "$OUT/manifest.txt"
fi
on_error() {
 code=$?
 trap - ERR
 echo "Stage141 FAILED (exit=$code) in $OUT" >&2
 if [[ -n "$CURRENT_LOG" && -f "$CURRENT_LOG" ]]; then tail -n 75 "$CURRENT_LOG" >&2; fi
 echo "result=FAILED" >> "$OUT/manifest.txt"
 tar -czf "$OUT.tar.gz" -C "$BASE" "$(basename "$OUT")" || true
 echo "Stage141 partial archive: $OUT.tar.gz" >&2
 exit "$code"
}
trap on_error ERR
CURRENT_LOG="$OUT/mesh-unit.log"
"$BIN" --unit > "$CURRENT_LOG" 2>&1
grep -Fqx 'mesh subdivision CPU checks=passed' "$CURRENT_LOG"
grep -Fqx 'triangle coverage CPU checks=passed' "$CURRENT_LOG"
CURRENT_LOG=""
for entry in off:direct:1 off:mdi:1 on:direct:1 on:mdi:1 on:mdi:2 on:direct:2 off:mdi:2 off:direct:2 off:direct:3 off:mdi:3 on:direct:3 on:mdi:3; do
  IFS=: read -r quality mode rep <<< "$entry"
  empty_tail=0
  [[ "$quality" == on ]] && empty_tail=128
  name="$OUT/2048-$quality-$mode$rep"
  CURRENT_LOG="$name.log"
  echo "Stage141 $quality $mode repeat=$rep"
  env -u AIVERSE_BATCH_AUTO -u AIVERSE_BATCH_AUTO_COST_TRACE \
      -u AIVERSE_BATCH_AUTO_TRACE -u AIVERSE_ICB_PROFILE \
      -u AIVERSE_ICB_SLOW_TRACE -u AIVERSE_BATCH_MODEL_GPU_COUNTERS \
      -u AIVERSE_EXPERIMENTAL_METAL_ICB -u AIVERSE_GPU_ENCODED_ICB \
      -u AIVERSE_GPU_AUTHORED_DRAWS -u AIVERSE_GPU_ICB_RIGID \
      -u AIVERSE_METAL_TRACE_FRAME -u AIVERSE_METAL_TRACE_PATH \
      -u AIVERSE_METAL_MDI_MIN_COMMANDS -u AIVERSE_BATCH_BENCH_MODE \
      -u AIVERSE_MATERIALS -u AIVERSE_PALETTE_TEST_QUALITY \
      -u AIVERSE_GEOMETRY_POOL -u AIVERSE_RENDER_SAFE \
      -u AIVERSE_AUTO_RIGID_MERGE -u AIVERSE_AUTO_SHADOW_BATCHES \
      -u AIVERSE_AUTO_MATERIAL_BATCHES \
      -u AIVERSE_METAL_CPU_MDI_SKIP_DEPTH_FRAGMENT_UNIFORMS \
      AIVERSE_PALETTE_TEST_CASE=auto_stress \
      AIVERSE_BATCH_STRESS_ITEMS=2048 \
      AIVERSE_AUTOLOD=off \
      AIVERSE_BATCH_DIAGNOSTIC_MESH_SUBDIVISIONS=4 \
      AIVERSE_BATCH_DIAGNOSTIC_TRIANGLE_SCALE=4 \
      AIVERSE_BATCH_STRESS_POLICY="$mode" \
      AIVERSE_BATCH_DIAGNOSTIC_MODEL_PASS=full \
      AIVERSE_BATCH_DIAGNOSTIC_MDI_GROUP_CAP=0 \
      AIVERSE_METAL_CPU_MDI_INHERIT_PIPELINE=0 \
      AIVERSE_METAL_CPU_MDI_EMPTY_TAIL="$empty_tail" \
      AIVERSE_METAL_CPU_MDI=1 AIVERSE_BATCH_CPU_TIMING=1 \
      AIVERSE_BATCH_FRAME_TIMELINE=1 AIVERSE_BATCH_GPU_TIMELINE=1 \
      AIVERSE_CAPTURE_SUCCESSFUL_MDI="$([[ "$mode" == mdi ]] && echo 1 || echo 0)" \
      "$BIN" unused.obj "$name" > "$name.log" 2>&1
  grep -Eq '^material palette CPU checks=[0-9]+ result=passed$' "$name.log"
  grep -Fqx 'stress diagnostic mesh subdivisions=4 triangles_per_primitive=16 vertices_per_primitive=48' "$name.log"
  grep -Fqx 'stress diagnostic triangle_scale=4' "$name.log"
  grep -Fqx 'stress diagnostic geometry pooled=1 vertex_bytes=5898240 index_bytes=393216' "$name.log"
  grep -Fqx 'stress diagnostic CPU MDI group cap=0' "$name.log"
  grep -Fqx 'stress diagnostic model pass=full mask=3' "$name.log"
  grep -qx 'changed_channels=0' "$name.log"
  grep -Eq '^GPU command-buffer timeline samples=66 path=' "$name.log"
  grep -Eq '^stress frame timeline samples=106 path=' "$name.log"
  test -s "$name-gpu-frames.csv"
  test -s "$name-frames.csv"
  if [[ "$mode" == mdi ]]; then
    grep -Eq "^\\[metal-icb-empty-tail\\] mode=$quality valid=[1-9][0-9]* reset=[1-9][0-9]* execute=[1-9][0-9]*$" "$name.log"
    grep -Eq '^actual MDI pixel parity frame=[0-9]+ changed_channels=0$' "$name.log"
    grep -Eq '^stress benchmark policy=mdi measured_mdi_frames=81 measured_direct_frames=0 measured_mdi_submissions=1458$' "$name.log"
  else
    grep -Eq '^stress benchmark policy=direct measured_mdi_frames=0 measured_direct_frames=81 measured_mdi_submissions=0$' "$name.log"
  fi
  echo "PASS: $quality $mode #$rep"
  CURRENT_LOG=""
done
python3 tools/batching-stage141-analyze.py "$OUT"
echo "result=PASS" >> "$OUT/manifest.txt"
echo "utc_end=$(date -u +%Y%m%dT%H%M%SZ)" >> "$OUT/manifest.txt"
tar -czf "$OUT.tar.gz" -C "$BASE" "$(basename "$OUT")"
echo "Stage141 archive: $OUT.tar.gz"
