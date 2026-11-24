#!/usr/bin/env bash
# Compare real-game RTS rollback/checksums with RTS transform jobs OFF vs ON.
# Optional --full also runs the full game and network unit test targets.
set -euo pipefail
cd "$(dirname "$0")/.."
TPL=${AIVERSE_TEMPLATES_DIR:-$(python3 tools/pkg/fetch_templates.py)}
FULL=0
for arg in "$@"; do
  case "$arg" in
    --full) FULL=1 ;;
    *) echo "Unknown argument: $arg" >&2; exit 2 ;;
  esac
done
ROOT="${AIVERSE_ECS_BUILD_ROOT:-${TMPDIR:-/tmp}/aiverse-ecs-gameplay}"
mkdir -p "$ROOT"
COMMON=(
  -DCMAKE_BUILD_TYPE=Release
  -DAIVERSE_BUILD_CLIENT=OFF
  -DAIVERSE_BUILD_EDITOR=OFF
  -DAIVERSE_BUILD_TOOLS=OFF
  -DAIVERSE_BUILD_SERVER=OFF
  -DAIVERSE_BUILD_HEADLESS=ON
  -DAIVERSE_BUILD_FPS_CLIENT=OFF
  -DAIVERSE_BUILD_TESTS=ON
)
for mode in off on; do
  build="$ROOT/build-$mode"
  flag=OFF
  [[ "$mode" == on ]] && flag=ON
  cmake -S . -B "$build" "${COMMON[@]}" "-DAIVERSE_ECS_EXPERIMENTAL_RTS_TRANSFORM_JOBS=$flag"
  targets=(aiverse_headless aiverse_fps_tests)
  if [[ "$FULL" == 1 ]]; then
    targets+=(aiverse_tests aiverse_net_tests
              aiverse_ecs_native_persistence_test
              aiverse_ecs_native_rts_integration_test)
  fi
  cmake --build "$build" --parallel "${AIVERSE_ECS_BUILD_JOBS:-4}" --target "${targets[@]}"
  echo "=== RTS headless: $mode ==="
  "$build/aiverse_headless" --project "$TPL/skirmish" --seed 3 --max-seconds 10 \
    --verify-restore-at 2 --verify-restore-ticks 40 \
    --tick-times "$ROOT/rtssim-$mode.tick-us" \
    --checksum-log "$ROOT/rtssim-$mode.checksums" > "$ROOT/rtssim-$mode.log" 2>&1 || {
    cat "$ROOT/rtssim-$mode.log"; exit 1;
  }
  grep "Restore OK" "$ROOT/rtssim-$mode.log" || {
    echo "Restore verification missing in $mode" >&2
    cat "$ROOT/rtssim-$mode.log"; exit 1;
  }
  echo "=== RTS headless COW: $mode ==="
  "$build/aiverse_headless" --project "$TPL/skirmish" --seed 3 --max-seconds 10 \
    --cow-snapshots --verify-restore-at 2 --verify-restore-ticks 40 \
    --checksum-log "$ROOT/rtssim-$mode.cow.checksums" > "$ROOT/rtssim-$mode.cow.log" 2>&1 || {
    cat "$ROOT/rtssim-$mode.cow.log"; exit 1;
  }
  grep "Restore OK" "$ROOT/rtssim-$mode.cow.log" || {
    echo "COW restore verification missing in $mode" >&2
    cat "$ROOT/rtssim-$mode.cow.log"; exit 1;
  }
  diff -u "$ROOT/rtssim-$mode.checksums" "$ROOT/rtssim-$mode.cow.checksums" || {
    echo "FAIL: COW vs deep RTS checksum mismatch in $mode" >&2; exit 1;
  }
  echo "=== FPS tests: $mode ==="
  ctest --test-dir "$build" --no-tests=error -R "^(fps|fps_transport)$" --output-on-failure
  "$build/engine/actor/aiverse_fps_tests" examples/fps/arena > "$ROOT/fps-$mode.log" 2>&1 || {
    cat "$ROOT/fps-$mode.log"; exit 1;
  }
  grep -E '^FPS_(MOTOR|COMBAT)_PARITY_HASH=' "$ROOT/fps-$mode.log" > "$ROOT/fps-$mode.hash" || {
    echo "FPS motor parity fixture did not execute in $mode" >&2; exit 1;
  }
  if [[ "$(wc -l < "$ROOT/fps-$mode.hash" | tr -d ' ')" != 2 ]]; then
    echo "FPS movement and combat parity hashes are both required in $mode" >&2
    exit 1
  fi
  fps_motor_ms="$(sed -n 's/^FPS_MOTOR_TICKS_MS=//p' "$ROOT/fps-$mode.log" | tail -1)"
  if ! [[ "$fps_motor_ms" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
    echo "Missing FPS motor timing in $mode" >&2; exit 1
  fi
  echo "FPS motor $mode: 120-tick-ms=$fps_motor_ms"
  if [[ "$FULL" == 1 ]]; then
    echo "=== RTS and network tests: $mode ==="
    ctest --test-dir "$build" --no-tests=error \
      -R "^(aiverse_(tests|net_tests)|ecs_native_(persistence|rts_integration)_test)$" \
      --output-on-failure
  fi
done
if ! grep -Eq "^[0-9]+ [0-9a-fA-F]{16}$" "$ROOT/rtssim-off.checksums"; then
  echo "No RTS checksum checkpoints collected" >&2
  exit 1
fi
if ! diff -u "$ROOT/fps-off.hash" "$ROOT/fps-on.hash"; then
  echo "FAIL: FPS checkpoints differ between the baseline and opt-in builds" >&2
  exit 1
fi
if ! diff -u "$ROOT/rtssim-off.checksums" "$ROOT/rtssim-on.checksums"; then
  echo "FAIL: baseline and opt-in RTS checksums differ" >&2
  exit 1
fi
for mode in off on; do
  echo -n "RTS $mode mean step: "
  awk 'NF==2 && $1 ~ /^[0-9]+$/ && $2 ~ /^[0-9]+$/ {sum+=$2; count++}
       END {if(count) printf "%.3f ms/tick (%d ticks)\n",sum/count/1000,count;
            else {print "no measurements";exit 1}}' "$ROOT/rtssim-$mode.tick-us"
done
echo "PASS RTS and FPS per-tick checksum parity plus snapshot restore in baseline/opt-in builds"
echo "Logs: $ROOT"
