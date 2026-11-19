#!/usr/bin/env bash
# One native acceptance pass AFTER finishing PHYSIS + TerraWeave features.
# macOS Bash 3.2 compatible. Call with: bash tools/physis-terraweave-final-validation.sh
set -Eeuo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BUILD="${1:-$ROOT/build/physis-terraweave-onepass}"
case "$BUILD" in /*) ;; *) BUILD="$ROOT/$BUILD" ;; esac
JOBS="${AIVERSE_TEST_JOBS:-4}"
case "$JOBS" in ""|*[!0-9]*) echo "Invalid AIVERSE_TEST_JOBS" >&2; exit 2 ;; esac
if (( JOBS < 1 )); then echo "AIVERSE_TEST_JOBS must be positive" >&2; exit 2; fi
if [[ -n "$(git status --porcelain --untracked-files=no)" ]]; then
    echo "ERROR: tracked working tree is dirty; commit/stash deliberately before acceptance." >&2
    exit 2
fi

mkdir -p "$BUILD"
REPORT="$BUILD/onepass-reports/$(date -u +%Y%m%d-%H%M%S)-$$"
mkdir -p "$REPORT"
HEAD="$(git rev-parse HEAD)"
printf 'AIVERSE PHYSIS+TerraWeave ONE-PASS acceptance\ncommit=%s\nreport=%s\n' "$HEAD" "$REPORT" | tee "$REPORT/summary.txt"
printf 'merge-parents=%s\n' "$(git show -s --format='%P' HEAD)" | tee -a "$REPORT/summary.txt"
trap 'rc=$?; printf "FAILED exit=%s; logs=%s\\n" "$rc" "$REPORT" >&2; printf "Disk remaining at failure:\\n" >&2; df -h "$BUILD" >&2 || true; printf "Current acceptance build size:\\n" >&2; du -sh "$BUILD" >&2 || true' ERR

echo '[0/4] Record disk capacity before full build (read-only)'
{
  df -h "$BUILD"
  du -sh "$BUILD"
} 2>&1 | tee "$REPORT/disk-before.txt"

echo '[1/4] Configure ALL desktop test targets (Box3D required)'
cmake -S "$ROOT" -B "$BUILD" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_ENABLE_PHYSICS=ON \
  -DAIVERSE_PLACE_FAST_DEBUG=ON \
  -DAIVERSE_BUILD_EDITOR=ON \
  -DAIVERSE_BUILD_TOOLS=ON \
  -DAIVERSE_BUILD_HEADLESS=ON \
  -DAIVERSE_BUILD_SERVER=ON \
  -DAIVERSE_BUILD_CLIENT=OFF \
  2>&1 | tee "$REPORT/configure.log"

echo '[2/4] Build ALL registered test executables exactly once'
cmake --build "$BUILD" --config Debug --parallel "$JOBS" \
  2>&1 | tee "$REPORT/build.log"

echo '[3/4] Check production cross-system tests really exist'
ctest --test-dir "$BUILD" --config Debug -N \
  -R '^physis_terraweave_fall_bridge$' \
  2>&1 | tee "$REPORT/bridge-target.txt"
grep -Eq 'Test[[:space:]]*#[0-9]+: physis_terraweave_fall_bridge' \
  "$REPORT/bridge-target.txt" || {
    echo 'ERROR: PHYSIS/TerraWeave real Box3D CTest not registered.' >&2
    exit 3
  }
ctest --test-dir "$BUILD" --config Debug -N -R '^terraweave' \
  2>&1 | tee "$REPORT/terra-targets.txt"
grep -Eq 'Test[[:space:]]*#[0-9]+: terraweave' \
  "$REPORT/terra-targets.txt" || {
    echo 'ERROR: canonical TerraWeave CTests not registered.' >&2
    exit 3
  }

echo '[4/4] Run the FULL unfiltered CTest suite ONE TIME'
# CTest normally holds individual test stdout until completion/failure.
# Opt-in verbose mode forwards the new Place progress checkpoints live,
# without changing its 5ms budget, scene, 4G clients or simulation ticks.
if [[ "${AIVERSE_CTEST_VERBOSE:-0}" == "1" ]]; then
  echo "CTest live verbose output enabled (AIVERSE_CTEST_VERBOSE=1)"
  ctest --test-dir "$BUILD" --config Debug \
    --no-tests=error --verbose --parallel "$JOBS" \
    --output-junit "$REPORT/ctest.xml" \
    2>&1 | tee "$REPORT/ctest.log"
else
  ctest --test-dir "$BUILD" --config Debug \
    --no-tests=error --output-on-failure --parallel "$JOBS" \
    --output-junit "$REPORT/ctest.xml" \
    2>&1 | tee "$REPORT/ctest.log"
fi

df -h "$BUILD" | tee "$REPORT/disk-after.txt"
printf 'PASS: ALL AIVERSE CTest selected by default, including PHYSIS/TerraWeave/Box3D\ncommit=%s\nlogs=%s\n' "$HEAD" "$REPORT" | tee -a "$REPORT/summary.txt"
