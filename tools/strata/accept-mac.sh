#!/bin/bash
# Copy of the 2026-11-22 STRATA acceptance driver (docs/strata.md, "Hardware acceptance").
# Paths are KINGSTON defaults; run from a .command on the Mac.
# STRATA acceptance on the Mac (native arm64 Release): build, CTests, Metal fixtures, castle STRATA on/off.
set -u
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
SRC="$(cd "$(dirname "$0")/../.." && pwd)"
W="${STRATA_ACCEPT_ROOT:-/Volumes/KINGSTON/AIVERSE-builds/strata-accept}"
B="${STRATA_BUILD_DIR:-$W/arm64}"
L=$W/logs; mkdir -p "$L" "$W/tmp"; export TMPDIR="$W/tmp"
ST=$L/status.txt
say(){ echo "$(date +%H:%M:%S) $*" | tee -a "$ST"; }
STEPS="${STEPS:-build ctest metal castle}"
[[ -w /Volumes/KINGSTON ]] || { say "FAIL KINGSTON not writable"; exit 2; }
say "start steps=$STEPS host=$(uname -m) cpu=$(sysctl -n machdep.cpu.brand_string) load=$(sysctl -n vm.loadavg)"
TESTS="aiverse_strata_auto_select_tests aiverse_strata_builder_tests aiverse_strata_seams_tests aiverse_strata_model_adapter_tests aiverse_strata_source_seams_tests aiverse_strata_view_tests aiverse_strata_lod_benchmark aiverse_strata_gpu_scene_tests aiverse_strata_gpu_culling_tests aiverse_strata_vulkan_kernel_tests aiverse_strata_render_routing_tests aiverse_strata_portable_tests aiverse_strata_debug_view_tests aiverse_strata_index_residency_tests aiverse_strata_index_pages_tests aiverse_strata_disk_pages_tests aiverse_strata_streaming_coordinator_tests"
METAL="aiverse_strata_metal_parity aiverse_strata_metal_temporal aiverse_strata_metal_raster_parity aiverse_strata_metal_depth_multiview_parity aiverse_strata_metal_reflection_material_parity aiverse_strata_metal_portable_parity"
if [[ " $STEPS " == *" build "* ]]; then
  npm --prefix "$SRC/apps/studio-web" install --no-audit --no-fund --package-lock=false > "$L/npm.log" 2>&1 &&
    npm --prefix "$SRC/apps/studio-web" run build -- --outDir "$W/studio-web" > "$L/web.log" 2>&1 || { say "FAIL HTML Studio assets"; exit 1; }
  say "configure arm64 Release"
  cmake -S "$SRC" -B "$B" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DAIVERSE_BUILD_EDITOR=OFF -DAIVERSE_BUILD_STUDIO_HTML5=ON -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_STUDIO_WEB_DIST="$W/studio-web" -DAIVERSE_STRATA_METAL_GPU_TESTS=ON -DAIVERSE_VULKAN=ON > "$L/configure.log" 2>&1 || { say "FAIL configure"; tail -30 "$L/configure.log" >> "$ST"; exit 1; }
  say "build"
  cmake --build "$B" --target aiverse_studio aiverse_strata_editor_test $TESTS $METAL --parallel 12 > "$L/build.log" 2>&1; rc=$?
  grep -E "error:|warning:" "$L/build.log" | sort -u | head -40 > "$L/build-diag.txt"
  [[ $rc == 0 ]] || { say "FAIL build rc=$rc"; head -20 "$L/build-diag.txt" >> "$ST"; exit 1; }
  say "build ok, warnings=$(grep -c warning: "$L/build-diag.txt")"
fi
ED="$B/apps/studio-native/AiverseStudioHTML5.app/Contents/MacOS/AiverseStudioHTML5"
for t in aiverse_strata_portable_tests aiverse_strata_metal_portable_parity; do f="$(find "$B" -maxdepth 4 -type f -name $t | head -1)"; say "$t: $(file "$f" | sed 's/.*: //')"; done
if [[ " $STEPS " == *" ctest "* ]]; then
  say "ctest strata"
  ctest --test-dir "$B" -R "^strata_" --output-on-failure --output-junit "$L/ctest.xml" > "$L/ctest.log" 2>&1
  say "ctest: $(grep -E 'tests passed|tests failed' "$L/ctest.log")"
  grep -E "Failed|SKIP|Not Run" "$L/ctest.log" | head >> "$ST"
  "$(find "$B" -maxdepth 4 -type f -name aiverse_strata_vulkan_kernel_tests | head -1)" > "$L/vulkan-runtime.log" 2>&1; say "vulkan kernel runtime rc=$? $(tail -1 "$L/vulkan-runtime.log")"
  for i in 1 2 3; do "$(find "$B" -maxdepth 4 -type f -name aiverse_strata_lod_benchmark | head -1)" > "$L/lod-bench-$i.txt" 2>&1; done
fi
runlimited(){ # seconds cmd...
  local s=$1; shift; "$@" & local p=$!; for ((k=0;k<s;k++)); do sleep 1; kill -0 $p 2>/dev/null || { wait $p; return $?; }; done; kill $p; return 124; }
if [[ " $STEPS " == *" metal "* ]]; then
  for t in $METAL; do
    f="$(find "$B" -maxdepth 4 -type f -name $t | head -1)"
    ( cd "$L" && runlimited 180 "$f" ) > "$L/$t.log" 2>&1; rc=$?
    say "$t rc=$rc $(grep -E 'status=|PASS|FAIL' "$L/$t.log" | tail -2 | tr '\n' ' ' | cut -c1-200)"
  done
fi
if [[ " $STEPS " == *" castle "* ]]; then
  [[ -x "$ED" ]] || { say "FAIL missing HTML Studio executable: $ED"; exit 1; }
  P=$W/alpine-castle
  if [[ ! -d $P ]]; then
    fixture="${STRATA_CASTLE_FIXTURE:-$SRC/.wind-qa/alpine-review}"
    [[ -f "$fixture/static-castle.json" && -f "$fixture/maps/valley.json" && -f "$fixture/game.json" ]] || { say "FAIL missing Alpine castle project at $fixture (set STRATA_CASTLE_FIXTURE)"; exit 1; }
    cp -R "$fixture" "$P" || exit 1
    # castle-only variant of the same scene (no foliage instances in the geometry scene)
    python3 -c '
import json,sys,os
p=sys.argv[1]; s=json.load(open(os.path.join(p,"static-castle.json")))
s["instances"]=[i for i in s["instances"] if "castle" in i["model"]]
json.dump(s,open(os.path.join(p,"static-castle-only.json"),"w"),indent=1)
' "$P" || exit 1
  fi
  PORT=47961
  export STRATA_STUDIO_CAPTURE=1 AIVERSE_ENGINE_DIR="$SRC" AIVERSE_AGENT_LINK_DIR="$W/agent-link"
  export LOD_CONNECT_WAIT=1200 AIVERSE_LOD_DIAGNOSTICS=1 CASTLE_SHOT="123,3.76,48" LOD_MAP="maps/valley.json" TERRA_PROJECT="$P"
  for run in ${CASTLE_RUNS:-S1 L1 S2 L2 OS1 OL1 OS2 OL2}; do
    # another acceptance pipeline may still drive an editor or package Android: wait
    while pgrep -f "listen $PORT" >/dev/null || pgrep -f "aiversebuild --project" >/dev/null; do sleep 5; done
    unset AIVERSE_STRATA; [[ $run == *L* ]] && export AIVERSE_STRATA=off
    GS=static-castle.json; [[ $run == O* ]] && GS=static-castle-only.json
    python3 -c '
import sys,re,json
p,gs=sys.argv[1],sys.argv[2]; t=open(p).read()
t=re.sub(r"\"geometryScene\": \"[^\"]*\"",json.dumps("geometryScene")+": "+json.dumps(gs),t); open(p,"w").write(t)
' "$P/game.json" "$GS" || exit 1
    O=$W/runs/castle-$run; rm -rf "$O"; mkdir -p "$O"
    say "castle $run strata=${AIVERSE_STRATA:-default} scene=$(grep -o 'static-castle[a-z-]*.json' "$P/game.json") load=$(sysctl -n vm.loadavg)"
    "$ED" --project "$P" --listen $PORT > "$O/editor.log" 2>&1 & EP=$!
    python3 "$SRC/tools/editor/lod_diag.py" $PORT "$O" > "$O/diag.log" 2>&1 & DP=$!
    for i in $(seq 1 1800); do sleep 1; kill -0 $DP 2>/dev/null || break; kill -0 $EP 2>/dev/null || { say "editor exited"; break; }; done
    kill "$DP" 2>/dev/null
    wait "$DP"; rc=$?
    kill "$EP" 2>/dev/null; sleep 3; kill -9 "$EP" 2>/dev/null; wait "$EP" 2>/dev/null
    [[ $rc == 0 ]] || { say "FAIL castle $run driver rc=$rc"; exit 1; }
    say "castle $run done: $(grep -c STATS "$O/diag.log") stats, $(ls "$O"/*.png 2>/dev/null | wc -l) png"
  done
fi
say "DONE"
