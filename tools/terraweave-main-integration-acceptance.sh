#!/usr/bin/env bash
# TerraWeave + current main: one native build, one unfiltered CTest.
# Run from an ISOLATED checkout of integration/terraweave-main-20261120.
# This does not merge branches, touch another worktree, or change game data.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

# Stable folder is kept separate from earlier TerraWeave/PHYSIS QA builds.
build="${AIVERSE_INTEGRATION_BUILD_DIR:-build/terraweave-main-acceptance}"
jobs="${AIVERSE_INTEGRATION_JOBS:-4}"
ctest_jobs="${AIVERSE_INTEGRATION_CTEST_JOBS:-2}"
mkdir -p "$build"
build="$(cd "$build" && pwd)"
reports="$build/acceptance-reports"
mkdir -p "$reports"

printf 'repo=%s\n' "$root" | tee "$reports/provenance.txt"
printf 'git_sha=%s\n' "$(git rev-parse HEAD)" | tee -a "$reports/provenance.txt"
printf 'git_branch=%s\n' "$(git branch --show-current)" | tee -a "$reports/provenance.txt"
git status --short | tee "$reports/git-status.txt"

# A PASS must be attributable to one clean, immutable source tree.
# A detached QA checkout is fine; uncommitted changes are not.
if [[ -s "$reports/git-status.txt" ]]; then
  echo '[integration] FAIL: source worktree has uncommitted/untracked changes' >&2
  exit 1
fi

# The default is the last audited main ancestor. Callers can pin a newer
# main or an exact target HEAD without rewriting this QA script.
required_main="${AIVERSE_ACCEPTANCE_REQUIRED_MAIN_SHA:-d6c8fa4646cf6aa205eea5f6a0b7c0c34ac479cb}"
actual_head="$(git rev-parse HEAD)"
if [[ -n "${AIVERSE_ACCEPTANCE_REQUIRED_HEAD:-}" && "$actual_head" != "$AIVERSE_ACCEPTANCE_REQUIRED_HEAD" ]]; then
  echo "[integration] FAIL: unexpected HEAD $actual_head" >&2
  exit 1
fi
if ! git cat-file -e "${required_main}^{commit}" 2>/dev/null ||
   ! git merge-base --is-ancestor "$required_main" HEAD; then
  echo "[integration] FAIL: required main ancestor $required_main is missing" >&2
  exit 1
fi
# On local Mac QA, fetch origin/main before running. Do not silently
# accept a branch that is behind an already-visible newer main.
if git show-ref --verify --quiet refs/remotes/origin/main &&
   ! git merge-base --is-ancestor refs/remotes/origin/main HEAD; then
  echo '[integration] FAIL: origin/main is newer than this Terra checkout' >&2
  exit 1
fi
printf 'required_main_sha=%s\n' "$required_main" | tee -a "$reports/provenance.txt"
printf 'observed_origin_main_sha=%s\n' \
  "$(git rev-parse refs/remotes/origin/main 2>/dev/null || echo unavailable)" \
  | tee -a "$reports/provenance.txt"

# Prevent a source checkout that predates one side of the 2026-11-20 merge
# from being mistaken for its integrated descendant.
git merge-base --is-ancestor d9de76dcd8aa4e7b4e01a42e183a0b9c98c4854b HEAD || {
  echo '[integration] FAIL: TerraWeave baseline is not an ancestor' >&2
  exit 1
}
git merge-base --is-ancestor d7d9b3f4180dcba98b9238fd56eedc7ccb420bb1 HEAD || {
  echo '[integration] FAIL: main baseline is not an ancestor' >&2
  exit 1
}

echo '========== configure merged native/editor/physics =========='
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
  2>&1 | tee "$reports/configure.log"

grep -E '^(CMAKE_BUILD_TYPE|AIVERSE_BUILD_TESTS|AIVERSE_ENABLE_PHYSICS|AIVERSE_PLACE_FAST_DEBUG|AIVERSE_BUILD_EDITOR):' \
  "$build/CMakeCache.txt" | tee "$reports/cmake-cache-flags.txt"

echo '========== build ALL enabled native targets =========='
cmake --build "$build" --parallel "$jobs" \
  2>&1 | tee "$reports/build.log"

echo '========== registered native tests =========='
ctest --test-dir "$build" -N \
  2>&1 | tee "$reports/inventory.log"

# CTest exits successfully even when no tests are registered. Explicitly
# require the primary end-to-end and saved-nutrient fixtures before running.
required_tests=(
  aiverse_tests
  terraweave
  terraweave_micro_nutrient
  physis_terraweave_fall_bridge
)
for test_name in "${required_tests[@]}"; do
  if ! grep -Eq "^[[:space:]]*Test[[:space:]]*#[0-9]+:[[:space:]]*${test_name}[[:space:]]*$" \
       "$reports/inventory.log"; then
    echo "[integration] FAIL: required CTest is missing: $test_name" >&2
    exit 1
  fi
done
echo '[integration] PASS: four mandatory native CTests registered' \
  | tee "$reports/required-tests.txt"

echo '========== run ONE complete CTest =========='
ctest --test-dir "$build" --output-on-failure --parallel "$ctest_jobs" \
  --output-junit "$reports/ctest.xml" \
  2>&1 | tee "$reports/ctest.log"

echo "PASS: merged native/editor/Box3D CTest; provenance and logs: $reports"
echo 'GPU Metal/Vulkan/GLES screenshots and actual live erosion/fall world remain separate acceptance gates.'
