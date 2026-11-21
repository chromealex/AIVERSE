#!/bin/bash
# macOS: build HTML5 Studio from this worktree on KINGSTON, run wind tests,
# then capture scene-only wind-mask screenshots of a generated fixture.
# Output: .wind-qa/shots/*.ppm, log .wind-qa/run.log (worktree-local, untracked).
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
mkdir -p "$ROOT/.wind-qa"
LOG="$ROOT/.wind-qa/run.log"
exec > >(tee "$LOG") 2>&1
echo "== $(date) $ROOT"
OUT=/Volumes/KINGSTON/AIVERSE-builds/wind-masks
if [[ ! -d /Volumes/KINGSTON || ! -w /Volumes/KINGSTON ]]; then echo "KINGSTON must be mounted (no internal-disk fallback)"; echo "== FAILED"; exit 2; fi
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
mkdir -p "$OUT/tmp"; export TMPDIR="$OUT/tmp"
GEN=""; command -v ninja >/dev/null && GEN="-G Ninja"
echo "== web"
cd "$ROOT/apps/studio-web" || exit 1
[[ -e node_modules ]] || ln -s "$ROOT/../../apps/studio-web/node_modules" node_modules
./node_modules/.bin/vite build --outDir "$OUT/web" --emptyOutDir || { echo "== FAILED web"; exit 1; }
cd "$ROOT"
echo "== configure"
cmake -S "$ROOT" -B "$OUT/native" $GEN -DCMAKE_BUILD_TYPE=Release -DAIVERSE_BUILD_EDITOR=ON \
  -DAIVERSE_BUILD_STUDIO_HTML5=ON -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_STUDIO_WEB_DIST="$OUT/web" || { echo "== FAILED configure"; exit 1; }
echo "== build"
cmake --build "$OUT/native" --target aiverse_studio aiverse_wind_rig_test aiverse_studio_session_tests --parallel 8 \
  > "$ROOT/.wind-qa/build.log" 2>&1; rc=$?
grep -E "error:|FAILED" "$ROOT/.wind-qa/build.log" | head -40
[[ $rc == 0 ]] || { echo "== FAILED build rc=$rc"; exit 1; }
echo "== tests"
ctest --test-dir "$OUT/native" -R '^(wind_rig|studio_session)$' --output-on-failure
echo "== shots"
python3 "$ROOT/tools/studio/make_wind_fixture.py" "$ROOT/.wind-qa/project" >/dev/null
APP="$(find "$OUT/native" -maxdepth 5 -name AiverseStudioHTML5.app -type d | head -1)"
echo "app: $APP"
rm -rf "$ROOT/.wind-qa/shots"
AIVERSE_STUDIO_WIND_SHOTS="$ROOT/.wind-qa/shots" AIVERSE_STUDIO_SHOTS_EXIT=1 \
AIVERSE_STUDIO_SHOT_CAMERA="12 4.5 12 17 0.6 0.22" \
  "$APP/Contents/MacOS/AiverseStudioHTML5" --project "$ROOT/.wind-qa/project" > "$ROOT/.wind-qa/studio.log" 2>&1 &
PID=$!
for i in $(seq 1 180); do sleep 1; kill -0 $PID 2>/dev/null || break; done
kill $PID 2>/dev/null
grep -E "wind-shots|Scene failed|error" "$ROOT/.wind-qa/studio.log" | head -20
ls -la "$ROOT/.wind-qa/shots" 2>/dev/null
echo "== DONE"
