#!/usr/bin/env bash
# Stage70 forest: REAL Linux Vulkan + OpenGL ES 3 parity acceptance.
# No simulated output, no Vulkan no-driver PASS, no desktop GL fallback.
# On Ubuntu install: cmake c++ make ninja-build libx11-dev libegl-dev
# libgles-dev libvulkan-dev mesa-vulkan-drivers xvfb xauth.
set -Eeuo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

if [[ "$(uname -s)" != Linux ]]; then
    echo '[terra-linux] FAIL: this script requires a Linux GPU runtime' >&2
    exit 2
fi
if [[ -n "$(git status --porcelain)" ]]; then
    echo '[terra-linux] FAIL: source checkout dirty; preserve exact SHA provenance' >&2
    exit 2
fi
head="$(git rev-parse HEAD)"
if [[ -n "${AIVERSE_TERRA_LINUX_REQUIRED_SHA:-}" &&
      "$head" != "$AIVERSE_TERRA_LINUX_REQUIRED_SHA" ]]; then
    echo "[terra-linux] FAIL: wrong HEAD $head" >&2
    exit 2
fi
if ! git merge-base --is-ancestor \
        0133eb21b211af62d205952db77c50589ae55a13 HEAD; then
    echo '[terra-linux] FAIL: tested TerraWeave forest Metal baseline missing' >&2
    exit 2
fi

jobs="${AIVERSE_TERRA_LINUX_JOBS:-4}"
case "$jobs" in ''|*[!0-9]*) echo '[terra-linux] invalid build jobs' >&2;exit 2;;esac
if (( jobs < 1 || jobs > 32 )); then
    echo '[terra-linux] build jobs outside 1..32' >&2
    exit 2
fi
build="${AIVERSE_TERRA_LINUX_BUILD_DIR:-build/terraweave-linux-gpu}"
mkdir -p "$build"
build="$(cd "$build" && pwd)"
report="$build/acceptance/${head:0:12}"
mkdir -p "$report"
{
    printf 'git_sha=%s\n' "$head"
    printf 'repo=%s\n' "$root"
    printf 'platform=%s\n' "$(uname -s)"
    printf 'main_sha=%s\n' "$(git rev-parse refs/remotes/origin/main 2>/dev/null || echo unknown)"
    printf 'build=%s\n' "$build"
} | tee "$report/provenance.txt"

cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=Debug \
    -DAIVERSE_VULKAN=ON -DAIVERSE_BUILD_TESTS=ON \
    -DAIVERSE_BUILD_EDITOR=OFF -DAIVERSE_BUILD_CLIENT=OFF \
    -DAIVERSE_BUILD_SERVER=OFF -DAIVERSE_BUILD_HEADLESS=OFF \
    -DAIVERSE_BUILD_TOOLS=OFF -DAIVERSE_ENABLE_PHYSICS=OFF \
    2>&1 | tee "$report/configure.log"

if ! grep -Eq '^AIVERSE_VULKAN:BOOL=ON$' "$build/CMakeCache.txt";then
    echo '[terra-linux] FAIL: Vulkan backend not compiled' >&2
    exit 1
fi
cmake --build "$build" --parallel "$jobs" \
    --target aiverse_terraweave_linux_forest_backends \
    2>&1 | tee "$report/build.log"

ctest --test-dir "$build" -N \
    -R '^terraweave_(vulkan|gles3)_forest$' \
    2>&1 | tee "$report/inventory.log"
for test in terraweave_vulkan_forest terraweave_gles3_forest; do
    if ! grep -Eq "Test[[:space:]]*#[0-9]+:[[:space:]]*${test}[[:space:]]*$" \
            "$report/inventory.log"; then
        echo "[terra-linux] FAIL: required backend test not registered: $test" >&2
        exit 1
    fi
done

export AIVERSE_TERRA_LINUX_CAPTURE="$report/vulkan"
ctest --test-dir "$build" --no-tests=error \
    -R '^terraweave_vulkan_forest$' -V --output-on-failure \
    --output-junit "$report/vulkan.xml" \
    2>&1 | tee "$report/vulkan.log"
if ! grep -Eq 'PASS \[terra-linux\] Vulkan real Renderer3D' \
        "$report/vulkan.log"; then
    echo '[terra-linux] FAIL: Vulkan hardware/software driver did not render' >&2
    exit 1
fi

command -v xvfb-run >/dev/null || {
    echo '[terra-linux] FAIL: xvfb-run required for EGL/GLES pbuffer' >&2
    exit 2
}
export AIVERSE_TERRA_LINUX_CAPTURE="$report/gles3"
xvfb-run -a -s '-screen 0 1024x768x24' \
    ctest --test-dir "$build" --no-tests=error \
    -R '^terraweave_gles3_forest$' -V --output-on-failure \
    --output-junit "$report/gles3.xml" \
    2>&1 | tee "$report/gles3.log"
if ! grep -Eq 'PASS \[terra-linux\] GLES3 real Renderer3D' \
        "$report/gles3.log"; then
    echo '[terra-linux] FAIL: GLES3 driver did not render' >&2
    exit 1
fi

for backend in vulkan gles3;do
    for frame in baseline branch-lod impostor;do
        if [[ ! -s "$report/${backend}-${frame}.ppm" ]];then
            echo "[terra-linux] FAIL: missing real ${backend} PPM ${frame}" >&2
            exit 1
        fi
    done
done
printf 'PASS: Vulkan + GLES3 real forest Renderer3D GPU/PPM; sha=%s; report=%s\n' \
    "$head" "$report" | tee "$report/result.txt"
echo '[terra-linux] NOTE: host/Mesa results are not a substitute for Android/device GPU testing or complete cross-platform visual parity.'
