#!/usr/bin/env bash
# AIVERSE: one entry point for all registered tests.
# Usage: ./run-tests.sh [--quick] [--jobs N] [--build-dir DIR]
# Keep compatible with macOS's default Bash 3.2.
set -Eeuo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$ROOT/build/all-tests"
JOBS=4
CTEST_JOBS=2
RUN_EXTRA=1
SKIP_BUILD=0
CTEST_FILTER=""
MANIFEST="$ROOT/tests/extra-test-suites.txt"

usage() {
    cat <<'HELP'
AIVERSE complete test runner (from any directory)
  ./run-tests.sh                  Configure, build, run all CTest + extra suites
  ./run-tests.sh --quick          Configure, build and run all CTest only
  ./run-tests.sh --no-build       Reuse previously configured/built binaries
  ./run-tests.sh --filter REGEX   Only matching CTest (extra suites skipped)
  ./run-tests.sh --build-dir DIR  Choose build directory (default: build/all-tests)
  ./run-tests.sh --jobs N         Build parallelism (default: 4)
  ./run-tests.sh --ctest-jobs N   CTest parallelism (default: 2)
  ./run-tests.sh --help           This help

Every add_test() in CMake is discovered automatically. Standalone tests must
be listed in tests/extra-test-suites.txt. A failure exits nonzero; full logs
and a summary are written to <build-dir>/test-reports/<timestamp>/.
HELP
}

die() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 2
}

while (( $# > 0 )); do
    case "$1" in
        --help|-h) usage; exit 0 ;;
        --quick) RUN_EXTRA=0; shift ;;
        --full) RUN_EXTRA=1; shift ;;
        --no-build) SKIP_BUILD=1; shift ;;
        --filter)
            (( $# >= 2 )) || die "--filter needs a CTest regex"
            CTEST_FILTER="$2"; RUN_EXTRA=0; shift 2 ;;
        --build-dir)
            (( $# >= 2 )) || die "--build-dir needs a path"
            BUILD_DIR="$2"; shift 2 ;;
        --jobs)
            (( $# >= 2 )) || die "--jobs needs a number"
            JOBS="$2"; shift 2 ;;
        --ctest-jobs)
            (( $# >= 2 )) || die "--ctest-jobs needs a number"
            CTEST_JOBS="$2"; shift 2 ;;
        *) die "Unknown option: $1 (try --help)" ;;
    esac
done

[[ "$JOBS" =~ ^[1-9][0-9]*$ ]] || die "--jobs must be positive"
[[ "$CTEST_JOBS" =~ ^[1-9][0-9]*$ ]] || die "--ctest-jobs must be positive"
[[ -n "$BUILD_DIR" ]] || die "--build-dir cannot be empty"
case "$BUILD_DIR" in
    /*) ;;
    *) BUILD_DIR="$ROOT/$BUILD_DIR" ;;
esac

command -v ctest >/dev/null 2>&1 || die "ctest is missing: install CMake"
if (( ! SKIP_BUILD )); then
    command -v cmake >/dev/null 2>&1 || die "cmake is missing"
else
    [[ -f "$BUILD_DIR/CMakeCache.txt" ]] || die "No configured build in $BUILD_DIR"
    # An optimized CMake test build typically defines NDEBUG, hiding asserts.
    if ! grep -Eq '^CMAKE_BUILD_TYPE:[^=]*=Debug$' "$BUILD_DIR/CMakeCache.txt" &&
       ! grep -Eq '^CMAKE_CONFIGURATION_TYPES:[^=]*=([^;]*;)*Debug(;|$)' "$BUILD_DIR/CMakeCache.txt"; then
        die "--no-build requires a Debug CMake cache (asserts enabled); rerun without --no-build"
    fi
fi

LOG_DIR="$BUILD_DIR/test-reports/$(date -u +%Y%m%d-%H%M%S)-$$"
mkdir -p "$LOG_DIR"
SUMMARY="$LOG_DIR/summary.txt"
: > "$SUMMARY"
FAILED=()
PASSED=()

if command -v git >/dev/null 2>&1; then
    HEAD_SHA="$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || true)"
else
    HEAD_SHA="unknown"
fi
printf 'AIVERSE tests | commit=%s | build=%s | report=%s\n' "$HEAD_SHA" "$BUILD_DIR" "$LOG_DIR" | tee -a "$SUMMARY"

stage() {
    local name="$1"
    shift
    local logfile="$LOG_DIR/$name.log"
    printf '\n========== %s ==========\n' "$name"
    printf 'Command:'
    printf ' %q' "$@"
    printf '\n'
    if (cd "$ROOT" && "$@") 2>&1 | tee "$logfile"; then
        printf 'PASS %s\n' "$name" | tee -a "$SUMMARY"
        PASSED+=("$name")
        return 0
    else
        local rc=$?
        printf 'FAIL %s (exit %s)\n' "$name" "$rc" | tee -a "$SUMMARY"
        FAILED+=("$name")
        return "$rc"
    fi
}

if (( ! SKIP_BUILD )); then
    # A dedicated build with all desktop test dependencies enabled.
    # Building the default target also produces test executables, preventing
    # CTest "Not Run" from missing binaries after an incomplete target build.
    if ! stage configure cmake -S "$ROOT" -B "$BUILD_DIR" \
        -DCMAKE_BUILD_TYPE=Debug \
        -DAIVERSE_BUILD_TESTS=ON \
        -DAIVERSE_BUILD_EDITOR=ON \
        -DAIVERSE_BUILD_TOOLS=ON \
        -DAIVERSE_BUILD_HEADLESS=ON \
        -DAIVERSE_BUILD_SERVER=ON \
        -DAIVERSE_BUILD_CLIENT=OFF; then
        printf '\nConfigure failed; tests were NOT RUN. See %s/configure.log\n' "$LOG_DIR" >&2
        exit 1
    fi
    if ! stage build cmake --build "$BUILD_DIR" --config Debug --parallel "$JOBS"; then
        printf '\nBuild failed; tests were NOT RUN. First compiler errors:\n' >&2
        grep -n -m 12 -E 'error:|fatal error:|Undefined symbols|undefined reference|FAILED:|ld: ' \
            "$LOG_DIR/build.log" >&2 || true
        printf 'Full log: %s/build.log\n' "$LOG_DIR" >&2
        exit 1
    fi
fi

if [[ -n "$CTEST_FILTER" ]]; then
    stage ctest ctest --test-dir "$BUILD_DIR" --config Debug \
        --no-tests=error --output-on-failure --parallel "$CTEST_JOBS" \
        --output-junit "$LOG_DIR/ctest.xml" -R "$CTEST_FILTER" || true
else
    stage ctest ctest --test-dir "$BUILD_DIR" --config Debug \
        --no-tests=error --output-on-failure --parallel "$CTEST_JOBS" \
        --output-junit "$LOG_DIR/ctest.xml" || true
fi

if (( RUN_EXTRA )); then
    [[ -f "$MANIFEST" ]] || die "Missing extra suite registry: $MANIFEST"
    suite_count=0
    while IFS='|' read -r name runner relative args || [[ -n "$name" ]]; do
        # Bash read strips LF but preserves a terminal CR from a CRLF manifest.
        # Strip it before interpreting optional script arguments.
        args="${args%$'\r'}"
        case "$name" in ""|\#*) continue ;; esac
        if ! [[ "$name" =~ ^[a-zA-Z0-9][a-zA-Z0-9_-]*$ ]]; then
            die "Invalid suite name in $MANIFEST: $name"
        fi
        if [[ "$relative" == /* || "$relative" == *..* || ! -f "$ROOT/$relative" ]]; then
            die "Invalid or absent suite script: $relative"
        fi
        case "$runner" in
            bash|python3) ;;
            *) die "Unsupported suite runner for $name: $runner" ;;
        esac
        case "$args" in *'|'*) die "Extra pipe in suite $name" ;; esac
        suite_count=$((suite_count + 1))
        suite_args=()
        if [[ -n "$args" ]]; then
            read -r -a suite_args <<< "$args"
        fi
        stage "extra-$name" "$runner" "$ROOT/$relative" "${suite_args[@]}" || true
    done < "$MANIFEST"
    if (( suite_count == 0 )); then
        printf 'ERROR: no extra suites registered\n' | tee -a "$SUMMARY" >&2
        FAILED+=("extra-empty-registry")
    fi
fi

printf '\n========== SUMMARY ==========\n' | tee -a "$SUMMARY"
printf 'Passed stages: %s | Failed stages: %s\n' "${#PASSED[@]}" "${#FAILED[@]}" | tee -a "$SUMMARY"
printf 'Logs: %s\n' "$LOG_DIR" | tee -a "$SUMMARY"
if (( ${#FAILED[@]} )); then
    printf 'Failed: %s\n' "${FAILED[*]}" | tee -a "$SUMMARY" >&2
    exit 1
fi
printf 'PASS: all selected test stages\n' | tee -a "$SUMMARY"
