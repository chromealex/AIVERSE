#!/usr/bin/env bash
# Dedicated Mac gate; never fall back to internal storage.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
drive="/Volumes/KINGSTON"
if [[ ! -d "$drive" || ! -w "$drive" ]] || ! mount | grep -F " on $drive (" >/dev/null; then
    echo "ERROR: KINGSTON not mounted/writable; no build attempted" >&2
    exit 2
fi
sha="$(git -C "$root" rev-parse HEAD)"
build="$drive/AIVERSE-builds/caverns-stage59/$sha"
mkdir -p "$build/tmp" "$build/modules"
export TMPDIR="$build/tmp"
export CLANG_MODULE_CACHE_PATH="$build/modules"
git -C "$root" status --short | tee "$build/working-tree.txt"
printf '%s\n' "$sha" > "$build/provenance.txt"
cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Debug \
    -DAIVERSE_ENABLE_PHYSICS=ON -DAIVERSE_BUILD_TESTS=ON \
    -DAIVERSE_BUILD_EDITOR=OFF -DAIVERSE_BUILD_STUDIO_HTML5=OFF \
    -DAIVERSE_FETCH_TEMPLATES=OFF -DAIVERSE_BUILD_CAVERNS_PLANET_DEMO=ON \
    -DAIVERSE_CAVERNS_PLANET_DEMO_SMOKE=ON 2>&1 | tee "$build/configure.log"
ctest --test-dir "$build" -N > "$build/inventory.txt"
for scenario in chamber portal swim moving_restore portal_restore swim_restore; do
    grep -q "terraweave_caverns_demo_$scenario" "$build/inventory.txt"
done
bash "$root/run-tests.sh" --build-dir "$build" --filter caverns --jobs "${AIVERSE_CAVERNS_JOBS:-4}"
