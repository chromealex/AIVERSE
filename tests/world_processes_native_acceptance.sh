#!/usr/bin/env bash
# Stage 127: real native host, not a substitute for GPU/production acceptance.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BUILD="${1:-build/world-processes-native}"
JOBS="${2:-4}"
if [[ ! "$JOBS" =~ ^[1-9][0-9]*$ ]]; then
    echo "Expected positive build parallelism" >&2; exit 2
fi
mkdir -p "$BUILD"
BUILD="$(cd "$BUILD" && pwd)"
LOG="$BUILD/world-processes-native-evidence/$(date -u +%Y%m%dT%H%M%SZ)-$$"
mkdir -p "$LOG"
echo "Evidence: $LOG"
git rev-parse HEAD > "$LOG/head.txt"
git status --porcelain > "$LOG/worktree.txt"
uname -a > "$LOG/platform.txt"
cmake --version > "$LOG/cmake-version.txt"
targets=(
    aiverse_world_processes_inspector_test
    aiverse_world_processes_authoring_test
    aiverse_world_processes_map_hydration_test
    aiverse_world_processes_linked_test
    aiverse_world_processes_gamecore_native_test
    aiverse_world_processes_planet_host_test
    aiverse_physics_scene_tests
    aiverse_physics_tests
    aiverse_physis_box3d_test
)
tests=(
    world_processes_inspector_test
    world_processes_authoring_test
    world_processes_map_hydration_test
    world_processes_linked_test
    world_processes_gamecore_native_test
    world_processes_planet_host_test
    physics_scene
    physics_runtime
    physis_box3d
)
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Debug \
    -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_ENABLE_PHYSICS=ON \
    -DAIVERSE_BUILD_EDITOR=OFF -DAIVERSE_BUILD_STUDIO_HTML5=OFF \
    -DAIVERSE_BUILD_TOOLS=OFF -DAIVERSE_BUILD_CLIENT=OFF \
    -DAIVERSE_BUILD_HEADLESS=OFF -DAIVERSE_BUILD_SERVER=OFF \
    -DAIVERSE_BUILD_PHYSICS_DEMO=OFF 2>&1 | tee "$LOG/configure.log"
cmake --build "$BUILD" --config Debug --parallel "$JOBS" \
    --target "${targets[@]}" 2>&1 | tee "$LOG/build.log"
# Require every expected name independently: a broad regex alone can pass
# with the optional physics fixture missing. Continue after individual failures.
status=0
for name in "${tests[@]}"; do
    if ctest --test-dir "$BUILD" -C Debug --no-tests=error \
        --output-on-failure -R "^$name$" \
        --output-junit "$LOG/$name.xml" 2>&1 | tee "$LOG/$name.log"; then
        printf 'PASS %s\n' "$name" | tee -a "$LOG/summary.txt"
    else
        printf 'FAIL %s\n' "$name" | tee -a "$LOG/summary.txt"
        status=1
    fi
done
exit "$status"
