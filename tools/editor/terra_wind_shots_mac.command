#!/bin/bash
# macOS: build aiverse_editor from this worktree on KINGSTON, open a copy of
# templates/wildfire, generate a TerraWeave landscape with the procedural
# Arbor forest + PHYSIS grass, and capture wind-mask screenshots through the
# editor agent link (tools/editor/terra_wind_shots.py).
# Output: .wind-qa/terra/*.png, log .wind-qa/terra-run.log (untracked).
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
mkdir -p "$ROOT/.wind-qa"
exec > >(tee "$ROOT/.wind-qa/terra-run.log") 2>&1
echo "== $(date) $ROOT"
OUT=/Volumes/KINGSTON/AIVERSE-builds/wind-masks
if [[ ! -d /Volumes/KINGSTON || ! -w /Volumes/KINGSTON ]]; then echo "KINGSTON must be mounted"; echo "== FAILED"; exit 2; fi
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
mkdir -p "$OUT/tmp"; export TMPDIR="$OUT/tmp"
echo "== build"
cmake "$OUT/native" > "$ROOT/.wind-qa/terra-configure.log" 2>&1 || { echo "== FAILED configure"; tail -20 "$ROOT/.wind-qa/terra-configure.log"; exit 1; }
cmake --build "$OUT/native" --target aiverse_editor aiverse_wind_rig_test aiverse_terraweave_model_test aiverse_terraweave_forest_scene_test aiverse_terraweave_erosion_zones_test aiverse_prisma_lens_water_tests aiverse_ecs_nereid_renderer_contract_test aiverse_shader_graph_tests --parallel 8 > "$ROOT/.wind-qa/terra-build.log" 2>&1; rc=$?
grep -E "error:|FAILED" "$ROOT/.wind-qa/terra-build.log" | head -40
[[ $rc == 0 ]] || { echo "== FAILED build rc=$rc"; exit 1; }
BIN="$(find "$OUT/native" -maxdepth 6 -type f -perm -u+x \( -name aiverse_editor -o -name AiverseEditor \) | head -1)"
echo "editor: $BIN"
ctest --test-dir "$OUT/native" -R "^(wind_rig|terraweave_model|terraweave_forest_scene|terraweave_erosion_zones|prisma_lens_water|ecs_nereid_renderer_contract_test|shader_graph)$" --verbose --output-on-failure | grep -E "tests passed|Failed|\*\*\*|Test #|erosion cm"
if [[ -n "${TERRA_PROJECT:-}" ]]; then PROJ="$TERRA_PROJECT"
else PROJ="$ROOT/.wind-qa/terra-project"; rm -rf "$PROJ"; cp -R "$ROOT/templates/wildfire" "$PROJ"; fi
rm -rf "$ROOT/.wind-qa/terra"
PORT=47931
"$BIN" --project "$PROJ" --engine "$ROOT" --listen $PORT > "$ROOT/.wind-qa/terra-editor.log" 2>&1 &
PID=$!
SCRIPT="${TERRA_SCRIPT:-terra_wind_shots.py}"; DEST="${TERRA_OUT:-terra}"
rm -rf "$ROOT/.wind-qa/$DEST"
python3 "$ROOT/tools/editor/$SCRIPT" $PORT "$ROOT/.wind-qa/$DEST" &
DRV=$!
for i in $(seq 1 ${TERRA_WAIT:-300}); do sleep 1; kill -0 $DRV 2>/dev/null || break; kill -0 $PID 2>/dev/null || { echo "editor exited"; break; }; done
kill $DRV 2>/dev/null; kill $PID 2>/dev/null
grep -iE "error|exception|failed" "$ROOT/.wind-qa/terra-editor.log" | head -20
ls -la "$ROOT/.wind-qa/$DEST" 2>/dev/null
echo "== DONE"
