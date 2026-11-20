#!/usr/bin/env bash
# Stage129: fixed 2048 model nodes / 4086 MDI commands per frame.
# 16 independent processes: mesh subdivisions 1/4 x inherited pipeline
# OFF/ON x Direct/MDI. Default renderer and shaders remain untouched.
set -euo pipefail
cd "$(dirname "$0")/.."
BIN="${AIVERSE_BATCH_BENCH_BIN:-build/batching-stage129/aiverse_material_batches}"
BASE="${AIVERSE_BATCH_CAPTURE_ROOT:-build/batching-stage129/captures}"
if [[ ! -x "$BIN" ]]; then
 echo "Missing rebuilt Stage129 benchmark executable: $BIN" >&2
 exit 2
fi
mkdir -p "$BASE"
OUT="$BASE/capture-$(date -u +%Y%m%dT%H%M%SZ)-$$"
mkdir "$OUT"
{
 echo "stage=129"
 echo "utc_start=$(date -u +%Y%m%dT%H%M%SZ)"
 echo "git_head=$(git rev-parse HEAD)"
 echo "binary=$BIN"
 echo "items=2048"
 echo "mesh_subdivisions=1,4"
 echo "triangles_per_primitive=1,16"
 echo "processes=16"
 echo "fixed_MDI_topology=18_submissions_4086_commands"
 echo "geometry_pool_required=1"
 echo "geometry_pool_limit_bytes=16777216"
 echo "icb_inherit_pipeline_off=0"
 echo "icb_inherit_pipeline_on=1"
 echo "order=off-direct1,off-mdi1,on-direct1,on-mdi1,on-mdi2,on-direct2,off-mdi2,off-direct2"
 echo "mesh_order=interleaved_alternating_1-4_4-1_per_mode"
 echo "gpu=whole_completed_Metal_command_buffer_timestamps"
 echo "note=geometry_tessellation_and_pipeline_inheritance_explicit_ICB_buffers"
} > "$OUT/manifest.txt"
CURRENT_LOG=""
on_error() {
 code=$?
 trap - ERR
 echo "Stage129 FAILED (exit=$code) in $OUT" >&2
 if [[ -n "$CURRENT_LOG" && -f "$CURRENT_LOG" ]]; then tail -n 75 "$CURRENT_LOG" >&2; fi
 echo "result=FAILED" >> "$OUT/manifest.txt"
 tar -czf "$OUT.tar.gz" -C "$BASE" "$(basename "$OUT")" || true
 echo "Stage129 partial archive: $OUT.tar.gz" >&2
 exit "$code"
}
trap on_error ERR
# The rebuilt benchmark must implement the new geometry, not merely accept
# a stale stage127 binary with the environment knob silently ignored.
CURRENT_LOG="$OUT/mesh-unit.log"
"$BIN" --unit > "$CURRENT_LOG" 2>&1
grep -Fqx 'mesh subdivision CPU checks=passed' "$CURRENT_LOG"
CURRENT_LOG=""
# Interleave N=1 and N=4 processes so thermal drift is not confounded
# with a complete low-geometry block followed by a high-geometry block.
mkdir "$OUT/mesh1" "$OUT/mesh4"
caseIndex=0
for entry in off:direct:1 off:mdi:1 on:direct:1 on:mdi:1 on:mdi:2 on:direct:2 off:mdi:2 off:direct:2; do
  if (( caseIndex % 2 == 0 )); then
    meshes=(1 4)
  else
    meshes=(4 1)
  fi
  for subdivisions in "${meshes[@]}"; do
  IFS=: read -r quality mode rep <<< "$entry"
  inherit_pipeline=0
  [[ "$quality" == on ]] && inherit_pipeline=1
  name="$OUT/mesh$subdivisions/2048-$quality-$mode$rep"
  CURRENT_LOG="$name.log"
  echo "Stage129 mesh=$subdivisions $quality $mode repeat=$rep"
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
      AIVERSE_PALETTE_TEST_CASE=auto_stress \
      AIVERSE_BATCH_STRESS_ITEMS=2048 \
      AIVERSE_AUTOLOD=off \
      AIVERSE_BATCH_DIAGNOSTIC_MESH_SUBDIVISIONS="$subdivisions" \
      AIVERSE_BATCH_STRESS_POLICY="$mode" \
      AIVERSE_BATCH_DIAGNOSTIC_MODEL_PASS=full \
      AIVERSE_BATCH_DIAGNOSTIC_MDI_GROUP_CAP=0 \
      AIVERSE_METAL_CPU_MDI_INHERIT_PIPELINE="$inherit_pipeline" \
      AIVERSE_METAL_CPU_MDI=1 AIVERSE_BATCH_CPU_TIMING=1 \
      AIVERSE_BATCH_FRAME_TIMELINE=1 AIVERSE_BATCH_GPU_TIMELINE=1 \
      AIVERSE_CAPTURE_SUCCESSFUL_MDI="$([[ "$mode" == mdi ]] && echo 1 || echo 0)" \
      "$BIN" unused.obj "$name" > "$name.log" 2>&1
  grep -Eq '^material palette CPU checks=[0-9]+ result=passed$' "$name.log"
  grep -Fqx "stress diagnostic mesh subdivisions=$subdivisions triangles_per_primitive=$((subdivisions * subdivisions)) vertices_per_primitive=$((3 * subdivisions * subdivisions))" "$name.log"
  grep -Eq '^stress diagnostic geometry pooled=1 vertex_bytes=[0-9]+ index_bytes=[0-9]+$' "$name.log"
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
  echo "PASS: mesh=$subdivisions $quality $mode #$rep"
  CURRENT_LOG=""
  done
  caseIndex=$((caseIndex + 1))
done
for subdivisions in 1 4; do
  python3 tools/batching-stage127-analyze.py "$OUT/mesh$subdivisions"
done
python3 tools/batching-stage129-analyze.py "$OUT"
python3 tools/batching-stage132-analyze.py "$OUT"
echo "result=PASS" >> "$OUT/manifest.txt"
echo "utc_end=$(date -u +%Y%m%dT%H%M%SZ)" >> "$OUT/manifest.txt"
tar -czf "$OUT.tar.gz" -C "$BASE" "$(basename "$OUT")"
echo "Stage129 archive: $OUT.tar.gz"
