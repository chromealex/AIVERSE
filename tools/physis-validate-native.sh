#!/usr/bin/env bash
# PHYSIS: focused native validation with reproducible reports.
# Compatible with macOS's default Bash 3.2.
set -Eeuo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MODE=full
JOBS=4
ENABLE_PHYSICS=ON
BUILD_DIR=""

usage() {
    cat <<'HELP'
PHYSIS native validation (run from any directory)
  bash tools/physis-validate-native.sh [--full|--integration|--standalone]
       [--jobs N] [--no-physics] [--build-dir DIR]

  --full          Root AIVERSE CMake, build every headless PHYSIS target,
                  then run all ^physis_ CTests (default)
  --integration   Root AIVERSE CMake, build only admin journal, remote edit,
                  field runtime and three journal tests; run just those CTests
  --standalone    Only the 3 independent POSIX journal CTests
  --no-physics    Disable optional Box3D in full/integration mode
  --build-dir DIR Use a different build directory; never wipes it
  --jobs N        Build parallelism (default 4)
  --help          Show this help

Reports are saved under <build-dir>/physis-reports/<UTC-timestamp>-<pid>/.
Any failed configure, build, or CTest returns nonzero and keeps full logs.
HELP
}

fail() { printf 'ERROR: %s\n' "$*" >&2; exit 2; }
while (( $# )); do
    case "$1" in
        --full) MODE=full; shift ;;
        --integration) MODE=integration; shift ;;
        --standalone) MODE=standalone; shift ;;
        --jobs) (( $# >= 2 )) || fail '--jobs requires a number'; JOBS="$2"; shift 2 ;;
        --no-physics) ENABLE_PHYSICS=OFF; shift ;;
        --build-dir) (( $# >= 2 )) || fail '--build-dir requires a path'; BUILD_DIR="$2"; shift 2 ;;
        --help|-h) usage; exit 0 ;;
        *) fail "unknown option: $1" ;;
    esac
done
[[ "$JOBS" =~ ^[1-9][0-9]*$ ]] || fail '--jobs must be a positive integer'
if [[ "$MODE" == standalone && "$ENABLE_PHYSICS" == OFF ]]; then
    fail '--no-physics is relevant only with --full/--integration'
fi
command -v cmake >/dev/null 2>&1 || fail 'cmake is missing'
command -v ctest >/dev/null 2>&1 || fail 'ctest is missing'
if [[ -z "$BUILD_DIR" ]]; then
    BUILD_DIR="$ROOT/build/physis-validation-$MODE"
elif [[ "$BUILD_DIR" != /* ]]; then
    BUILD_DIR="$ROOT/$BUILD_DIR"
fi
mkdir -p "$BUILD_DIR"
REPORT="$BUILD_DIR/physis-reports/$(date -u +%Y%m%d-%H%M%S)-$$"
mkdir -p "$REPORT"
SUMMARY="$REPORT/summary.txt"
HEAD="unknown"
if command -v git >/dev/null 2>&1; then
    HEAD="$(git -C "$ROOT" rev-parse --verify HEAD 2>/dev/null || true)"
    [[ -n "$HEAD" ]] || HEAD=unknown
fi
printf 'PHYSIS | mode=%s | commit=%s | build=%s\n' "$MODE" "$HEAD" "$BUILD_DIR" | tee "$SUMMARY"

stage() {
    local name="$1"
    shift
    local log="$REPORT/$name.log"
    printf '\n=== %s ===\n' "$name"
    printf 'Command:'
    printf ' %q' "$@"
    printf '\n'
    if ( "$@" 2>&1 | tee "$log" ); then
        printf 'PASS %s\n' "$name" | tee -a "$SUMMARY"
    else
        local rc=$?
        printf 'FAIL %s (exit %s)\n' "$name" "$rc" | tee -a "$SUMMARY"
        printf 'Full log: %s\n' "$log" >&2
        return "$rc"
    fi
}

if [[ "$MODE" == standalone ]]; then
    SOURCE="$ROOT/tests/physis_journal_standalone"
    [[ -f "$SOURCE/CMakeLists.txt" ]] || fail 'standalone CMakeLists.txt missing'
    stage configure cmake -S "$SOURCE" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug
    stage build cmake --build "$BUILD_DIR" --parallel "$JOBS"
    stage ctest ctest --test-dir "$BUILD_DIR" --output-on-failure --no-tests=error
else
    stage configure cmake -S "$ROOT" -B "$BUILD_DIR" \
        -DCMAKE_BUILD_TYPE=Debug \
        -DAIVERSE_BUILD_TESTS=ON \
        -DAIVERSE_ENABLE_PHYSICS="$ENABLE_PHYSICS" \
        -DAIVERSE_BUILD_EDITOR=OFF \
        -DAIVERSE_BUILD_TOOLS=OFF \
        -DAIVERSE_BUILD_CLIENT=OFF \
        -DAIVERSE_BUILD_HEADLESS=OFF \
        -DAIVERSE_BUILD_SERVER=OFF \
        -DAIVERSE_BUILD_PHYSICS_DEMO=OFF
    TARGETS=(
        aiverse_physis_field_runtime_test
        aiverse_physis_remote_edit_test
        aiverse_physis_admin_journal_test
        aiverse_physis_journal_posix_io_test
        aiverse_physis_journal_locked_io_test
        aiverse_physis_journal_crash_recovery_test
    )
    if [[ "$MODE" == full ]]; then
        TARGETS=(
        aiverse_physis_core_test
        aiverse_physis_climate_test
        aiverse_physis_environment_test
        aiverse_physis_disasters_test
        aiverse_physis_integration_test
        aiverse_physis_wind_test
        aiverse_physis_gravity_test
        aiverse_physis_storm_test
        aiverse_physis_forces_test
        aiverse_physis_presentation_test
        aiverse_physis_diagnostics_test
        aiverse_physis_spatial_index_test
        aiverse_physis_editor_view_test
        aiverse_physis_scene_geometry_test
        aiverse_physis_project_config_test
        aiverse_physis_field_assets_test
        aiverse_physis_field_runtime_test
        aiverse_physis_live_edit_test
        aiverse_physis_remote_edit_test
        aiverse_physis_admin_journal_test
        aiverse_physis_journal_posix_io_test
        aiverse_physis_journal_locked_io_test
        aiverse_physis_journal_crash_recovery_test
        )
        if [[ "$ENABLE_PHYSICS" == ON ]]; then
            TARGETS+=(aiverse_physis_box3d_test)
        fi
    fi
    stage build cmake --build "$BUILD_DIR" --parallel "$JOBS" --target "${TARGETS[@]}"
    if [[ "$MODE" == integration ]]; then
        stage ctest ctest --test-dir "$BUILD_DIR" --output-on-failure --no-tests=error \
          -R '^physis_(admin_journal|remote_edit|field_runtime|journal_posix_io|journal_locked_io|journal_crash_recovery)$'
    else
        stage ctest ctest --test-dir "$BUILD_DIR" --output-on-failure --no-tests=error -R '^physis_'
    fi
fi
printf '\nPHYSIS validation PASS | report=%s\n' "$REPORT" | tee -a "$SUMMARY"
