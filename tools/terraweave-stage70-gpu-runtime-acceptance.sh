#!/usr/bin/env bash
# TerraWeave Stage70 targeted Mac GPU + ecology evidence.
# Separate from the previously accepted unfiltered 271/271 CTest.
# Requires actual Metal GPU execution, not CTest's exit-77 SKIP result.
# Does not merge branches, clean worktrees, or weaken CMake test flags.
set -Eeuo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
if [[ "$(uname -s)" != Darwin ]]; then
    echo '[terra70] FAIL: this gate requires a real macOS Metal device; Linux Vulkan/GLES QA is separate' >&2
    exit 2
fi
if [[ -n "$(git status --porcelain)" ]]; then
    echo '[terra70] FAIL: checkout is dirty; no proof for an exact source SHA' >&2
    exit 2
fi
sha="$(git rev-parse HEAD)"
if [[ -n "${AIVERSE_TERRA70_REQUIRED_SHA:-}" &&
      "$sha" != "$AIVERSE_TERRA70_REQUIRED_SHA" ]]; then
    echo "[terra70] FAIL: unexpected HEAD $sha" >&2
    exit 2
fi
# The accepted 271-test source, including the owner fixes, must be a
# real ancestor. This detects a stale or wrong isolated worktree.
if ! git merge-base --is-ancestor \
       b17e775a922d9dc50981011c8f6e25d85fc7adec HEAD; then
    echo '[terra70] FAIL: accepted Stage70 source is missing from checkout' >&2
    exit 2
fi
build="${AIVERSE_TERRA70_BUILD_DIR:-build/terraweave-main-acceptance}"
jobs="${AIVERSE_TERRA70_JOBS:-4}"
case "$jobs" in ''|*[!0-9]*) echo '[terra70] invalid job count' >&2; exit 2;; esac
if (( jobs < 1 || jobs > 32 )); then
    echo '[terra70] jobs must be 1..32' >&2
    exit 2
fi
mkdir -p "$build"
build="$(cd "$build" && pwd)"
reports="$build/stage70-focused-qa/${sha:0:12}"
mkdir -p "$reports"
{
    echo "repo=$root"
    echo "git_sha=$sha"
    echo "main_sha=$(git rev-parse refs/remotes/origin/main 2>/dev/null || echo unknown)"
    echo "platform=$(uname -s)"
    echo "build=$build"
    echo 'scope=targeted Stage70 ecosystem + four hardware MetalGPU tests + actual PPM frames'
} | tee "$reports/provenance.txt"
echo "[terra70] report=$reports"
# Pipeline status must be that of the producer, not tee.
cmake -S "$root" -B "$build" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DAIVERSE_BUILD_TESTS=ON \
    -DAIVERSE_BUILD_EDITOR=ON \
    -DAIVERSE_BUILD_CLIENT=OFF \
    -DAIVERSE_BUILD_HEADLESS=ON \
    -DAIVERSE_BUILD_TOOLS=ON \
    -DAIVERSE_ENABLE_PHYSICS=ON \
    -DAIVERSE_BUILD_PHYSICS_DEMO=OFF \
    -DAIVERSE_PLACE_FAST_DEBUG=ON \
    -DAIVERSE_FBX=OFF \
    2>&1 | tee "$reports/configure.log"

targets=(
    aiverse_terraweave_stage70_sustained_test
    aiverse_terraweave_micro_nutrient_test
    aiverse_terraweave_forest_scene_test
    aiverse_physis_terraweave_fall_bridge_test
    aiverse_physis_nereid_contacts_test
    aiverse_spectrafx_metal_gpu_test
    aiverse_spectrafx_metal_billboard_test
    aiverse_spectrafx_metal_rhi_test
    aiverse_spectrafx_metal_scene_test
)
cmake --build "$build" --parallel "$jobs" --target "${targets[@]}" \
    2>&1 | tee "$reports/build.log"
ctest --test-dir "$build" -N 2>&1 | tee "$reports/inventory.log"
required=(
    terraweave_stage70_sustained
    terraweave_micro_nutrient
    terraweave_forest_scene
    physis_terraweave_fall_bridge
    physis_nereid_contacts
    spectrafx_metal_gpu
    spectrafx_metal_billboard
    spectrafx_metal_rhi
    spectrafx_metal_scene
)
for test_name in "${required[@]}"; do
    if ! grep -Eq "Test[[:space:]]*#[0-9]+:[[:space:]]*${test_name}[[:space:]]*$" \
         "$reports/inventory.log"; then
        echo "[terra70] FAIL: missing required test $test_name" >&2
        exit 1
    fi
done
echo '[terra70] PASS: all nine required native/Metal CTests registered' \
    | tee "$reports/inventory-gate.txt"

ctest --test-dir "$build" --no-tests=error \
    -R '^(terraweave_stage70_sustained|terraweave_micro_nutrient|terraweave_forest_scene|physis_terraweave_fall_bridge|physis_nereid_contacts)$' \
    --output-on-failure --output-junit "$reports/runtime.xml" \
    2>&1 | tee "$reports/runtime.log"

# The scene test writes seven *real* P6 frames into this directory.
# The parser rejects any missing/skipped/failing Metal test even if CTest
# considers SKIP_RETURN_CODE=77 an otherwise green overall run.
export AIVERSE_SPECTRAFX_SCENE_CAPTURE="$reports/spectrafx-scene"
ctest --test-dir "$build" --no-tests=error \
    -R '^spectrafx_metal_(gpu|billboard|rhi|scene)$' \
    -V --output-on-failure --output-junit "$reports/metal.xml" \
    2>&1 | tee "$reports/metal.log"
python3 "$root/tools/spectrafx/verify_metal_ctest.py" \
    "$reports/metal.log" --require-all \
    2>&1 | tee "$reports/metal-evidence.log"
python3 "$root/tools/spectrafx/review_frames.py" \
    --prefix "$AIVERSE_SPECTRAFX_SCENE_CAPTURE" \
    --output-prefix "$reports/spectrafx-visual-review" --strict \
    2>&1 | tee "$reports/visual-evidence.log"

printf 'PASS: targeted Stage70 runtime and strict real MetalGPU/PPM checks; sha=%s; logs=%s\n' \
    "$sha" "$reports" | tee "$reports/result.txt"
echo '[terra70] NOTE: TerraWeave forest GPU screenshots, Vulkan/GLES, and prolonged real four-owner erosion are separate gates.'
