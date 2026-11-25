#!/usr/bin/env bash
# Caverns Stage33 — native checkout acceptance; never claim pass on failures.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${AIVERSE_CAVERNS_BUILD_DIR:-${repo}/build/caverns-stage33}"
report="${AIVERSE_CAVERNS_REPORT_DIR:-${build}/acceptance}"
mkdir -p "${report}"

echo "repo=${repo}" > "${report}/provenance.txt"
git -C "${repo}" rev-parse HEAD >> "${report}/provenance.txt"
git -C "${repo}" branch --show-current >> "${report}/provenance.txt"
git -C "${repo}" status --short > "${report}/working-tree.txt"

cmake -S "${repo}" -B "${build}" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_FETCH_TEMPLATES=OFF \
  -DAIVERSE_BUILD_EDITOR=ON \
  -DAIVERSE_ENABLE_PHYSICS=ON \
  "${@}" 2>&1 | tee "${report}/configure.log"

cmake --build "${build}" \
  --target \
  aiverse_terraweave_caverns_mesh_test \
  aiverse_terraweave_caverns_swap_policy_test \
  aiverse_terraweave_caverns_owner_barrier_test \
  aiverse_terraweave_caverns_preview_pacer_test \
  -j "${AIVERSE_CAVERNS_JOBS:-4}" \
  2>&1 | tee "${report}/build-smoke.log"

ctest --test-dir "${build}" --output-on-failure -L Caverns \
  2>&1 | tee "${report}/ctest-smoke.log"

if [[ "${AIVERSE_CAVERNS_FULL_BUILD:-0}" == "1" ]]; then
  cmake --build "${build}" -j "${AIVERSE_CAVERNS_JOBS:-4}" \
    2>&1 | tee "${report}/build-full.log"
  ctest --test-dir "${build}" --output-on-failure \
    2>&1 | tee "${report}/ctest-full.log"
fi
echo "Caverns stage33 native verification completed; logs: ${report}"
