#!/usr/bin/env bash
# AIVERSE MotionWeave final validation runner.
# Stage 137: authored only. Run AFTER the feature-freeze checklist is complete.
# Usage: bash tools/animation/validate-final.sh
# Optional: AIVERSE_ANIMATION_JOBS=8 AIVERSE_ANIMATION_SANITIZERS=1 bash ...
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

EXPECTED_BRANCH="feature/universal-ecs-animation-pipeline"
ACTUAL_BRANCH="$(git branch --show-current)"
if [[ "$ACTUAL_BRANCH" != "$EXPECTED_BRANCH" && "${AIVERSE_ANIMATION_ALLOW_OTHER_BRANCH:-0}" != "1" ]]; then
    echo "Refusing validation on '$ACTUAL_BRANCH': expected '$EXPECTED_BRANCH'." >&2
    echo "Set AIVERSE_ANIMATION_ALLOW_OTHER_BRANCH=1 only for an explicitly reviewed integration branch." >&2
    exit 2
fi
if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "This native validation also needs a separate macOS/Metal run; current OS: $(uname -s)." >&2
fi

JOBS="${AIVERSE_ANIMATION_JOBS:-8}"
if ! [[ "$JOBS" =~ ^[0-9]+$ ]] || (( JOBS < 1 )); then
    echo "AIVERSE_ANIMATION_JOBS must be a positive integer" >&2
    exit 2
fi

STAMP="$(date +%Y%m%d-%H%M%S)"
OUT="${AIVERSE_ANIMATION_VALIDATION_OUT:-$ROOT/build/animation-final-$STAMP}"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
LOG="$OUT/logs"
mkdir -p "$LOG"
FAILED=0

{
    echo "AIVERSE MotionWeave final validation"
    echo "Timestamp: $STAMP"
    echo "Branch: $ACTUAL_BRANCH"
    echo "HEAD: $(git rev-parse HEAD)"
    echo "OS: $(uname -srm)"
    echo "Dirty files (not modified by runner):"
    git status --short
    echo
    cmake --version | head -1
    command -v xcodebuild >/dev/null && xcodebuild -version || true
} > "$OUT/manifest.txt" 2>&1

step() {
    local name="$1"
    shift
    echo "=== $name ==="
    if "$@" 2>&1 | tee "$LOG/$name.log"; then
        printf 'PASS  %s\n' "$name" >> "$OUT/summary.txt"
        return 0
    else
        local status=$?
        printf 'FAIL  %s (exit %s)\n' "$name" "$status" >> "$OUT/summary.txt"
        FAILED=$((FAILED + 1))
        return 1
    fi
}

cat > "$OUT/visual-parity-checklist.md" <<'CHECKLIST'
# Manual GPU and presentation parity (NOT automated)
Fill in PASS/FAIL and attach screenshots/video + hardware/driver details.
These cases CANNOT be inferred from CPU CTest results.

- [ ] macOS Metal: RTS animated models, mounts/attachments and camera transitions
- [ ] macOS Metal: FPS player and remote actor animation, baked crowds, clip continuity
- [ ] macOS Metal: editor preview, animation graph hot reload, retarget/LOD transitions
- [ ] Device reset / epoch and failed retirement fence with real GPU resources
- [ ] Model reimport while source generation changes (no stale skin matrices)
- [ ] Real graphics resource failures: partial vertex/index/image/morph allocation
- [ ] GL/Vulkan visual parity on supported host(s)
- [ ] CPU and GPU profiling with representative 1,000/5,000 actor workloads
CHECKLIST

BASE="$OUT/native"
COMMON=(
    -DAIVERSE_BUILD_TESTS=ON
    -DAIVERSE_BUILD_TOOLS=ON
    -DAIVERSE_BUILD_CLIENT=ON
    -DAIVERSE_BUILD_EDITOR=ON
    -DAIVERSE_ENABLE_PHYSICS=ON
)

if step "01-configure" cmake -S "$ROOT" -B "$BASE" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo "${COMMON[@]}"; then
    if step "02-build-all" cmake --build "$BASE" --parallel "$JOBS"; then
        step "03-ctest-all" ctest --test-dir "$BASE" \
            --output-on-failure --parallel "$JOBS" || true
        if [[ -x "$BASE/aiverse_motionweave_bench" ]]; then
            step "04-bench-1000" "$BASE/aiverse_motionweave_bench" \
                --actors 1000 --frames 120 || true
            step "05-bench-5000-ik" "$BASE/aiverse_motionweave_bench" \
                --actors 5000 --frames 120 --ik || true
        else
            echo "FAIL  04/05-bench (target missing)" >> "$OUT/summary.txt"
            FAILED=$((FAILED + 1))
        fi
    else
        echo "SKIP  CTest/bench: build failed" >> "$OUT/summary.txt"
    fi
else
    echo "SKIP  build/CTest/bench: configure failed" >> "$OUT/summary.txt"
fi

# A second CTest configuration is necessary for sanitizer instrumentation.
# These still run from ONE invocation of this script, and only after feature freeze.
if [[ "${AIVERSE_ANIMATION_SANITIZERS:-1}" == "1" ]]; then
    for kind in asan tsan; do
        case "$kind" in
            asan) flags="-fsanitize=address,undefined -fno-omit-frame-pointer" ;;
            tsan) flags="-fsanitize=thread -fno-omit-frame-pointer" ;;
        esac
        dir="$OUT/$kind"
        if step "10-$kind-configure" cmake -S "$ROOT" -B "$dir" \
            -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_C_FLAGS="$flags" \
            -DCMAKE_CXX_FLAGS="$flags" \
            -DCMAKE_EXE_LINKER_FLAGS="$flags" \
            "${COMMON[@]}"; then
            if step "11-$kind-build" cmake --build "$dir" --parallel "$JOBS"; then
                step "12-$kind-ctest" ctest --test-dir "$dir" \
                    --output-on-failure --parallel "$JOBS" || true
            else
                echo "SKIP  $kind CTest: build failed" >> "$OUT/summary.txt"
            fi
        else
            echo "SKIP  $kind build/CTest: configure failed" >> "$OUT/summary.txt"
        fi
    done
else
    echo "SKIP  ASan/UBSan and TSan (AIVERSE_ANIMATION_SANITIZERS=0)" >> "$OUT/summary.txt"
fi

{
    echo
    echo "Native summary:"
    cat "$OUT/summary.txt"
    echo
    echo "Manual visual/device checks: NOT RUN; see $OUT/visual-parity-checklist.md"
    echo "Logs and HEAD manifest: $OUT"
} | tee "$OUT/final-result.txt"

if (( FAILED > 0 )); then
    exit 1
fi
exit 0
