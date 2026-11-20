#!/usr/bin/env bash
# One focused TerraWeave forest hardware frame acceptance on macOS.
# Uses the same production Metal gfx::Device and Renderer3D as the editor.
# No full CTest rerun, no reset/clean/pull, and no synthetic GPU screenshots.
set -Eeuo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
if [[ "$(uname -s)" != Darwin ]]; then
    echo '[terra-forest] FAIL: real Apple Metal GPU is required' >&2
    exit 2
fi
if [[ -n "$(git status --porcelain)" ]]; then
    echo '[terra-forest] FAIL: dirty checkout, results need exact clean Git SHA' >&2
    exit 2
fi
sha="$(git rev-parse HEAD)"
if [[ -n "${AIVERSE_FOREST_REQUIRED_SHA:-}" &&
      "$sha" != "$AIVERSE_FOREST_REQUIRED_SHA" ]]; then
    echo "[terra-forest] FAIL: unexpected HEAD $sha" >&2
    exit 2
fi
if ! git merge-base --is-ancestor \
     b1bb27c8f1e9f803ff49af29ea7dc0769b9cb264 HEAD; then
    echo '[terra-forest] FAIL: required Stage70/strict Metal source missing' >&2
    exit 2
fi

build="${AIVERSE_FOREST_BUILD_DIR:-build/terraweave-main-acceptance}"
jobs="${AIVERSE_FOREST_JOBS:-4}"
case "$jobs" in ''|*[!0-9]*) echo '[terra-forest] invalid build jobs' >&2; exit 2 ;; esac
if (( jobs < 1 || jobs > 32 )); then
    echo '[terra-forest] build jobs must be 1..32' >&2
    exit 2
fi
mkdir -p "$build"
build="$(cd "$build" && pwd)"
report="$build/forest-metal-qa/${sha:0:12}"
mkdir -p "$report"
{
  echo "git_sha=$sha"
  echo "repo=$root"
  echo "build=$build"
  echo "platform=$(uname -s)"
  echo "main_sha=$(git rev-parse refs/remotes/origin/main 2>/dev/null || echo unknown)"
} | tee "$report/provenance.txt"

cmake -S . -B "$build" \
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
  2>&1 | tee "$report/configure.log"

cmake --build "$build" --parallel "$jobs" \
  --target aiverse_terraweave_metal_forest_test \
  2>&1 | tee "$report/build.log"

ctest --test-dir "$build" -N -R '^terraweave_metal_forest$' \
  2>&1 | tee "$report/inventory.log"
if ! grep -Eq 'Test[[:space:]]*#[0-9]+:[[:space:]]*terraweave_metal_forest[[:space:]]*$' \
     "$report/inventory.log"; then
  echo '[terra-forest] FAIL: hardware test missing from CTest' >&2
  exit 1
fi

export AIVERSE_TERRAWEAVE_FOREST_CAPTURE="$report/forest"
ctest --test-dir "$build" --no-tests=error \
  -R '^terraweave_metal_forest$' -V --output-on-failure \
  --output-junit "$report/ctest.xml" \
  2>&1 | tee "$report/ctest.log"
if ! grep -Eq '^[[:space:]]*1/1 Test #[0-9]+: terraweave_metal_forest.*Passed' \
     "$report/ctest.log"; then
  echo '[terra-forest] FAIL: real GPU CTest not confirmed Passed' >&2
  exit 1
fi
for suffix in baseline branch-lod impostor-a impostor-b \
              baseline-resized branch-resized; do
  if [[ ! -s "$report/forest-${suffix}.ppm" ]]; then
    echo "[terra-forest] FAIL: GPU frame missing: $suffix" >&2
    exit 1
  fi
done

printf 'PASS: real TerraWeave Metal forest GPU/PPM acceptance; sha=%s; evidence=%s\n' \
  "$sha" "$report" | tee "$report/result.txt"
echo '[terra-forest] NOTE: GPU frame capture does not prove Linux Vulkan/GLES or sustained native four-owner erosion.'
