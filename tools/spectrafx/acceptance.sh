#!/usr/bin/env bash
# SpectraFX ONE-COMMAND acceptance runner. Native compile, full CTest,
# SpectraFX contracts and real Metal compute/raster validation on macOS.
# Usage: bash tools/spectrafx/acceptance.sh [build-dir]
# AIVERSE_ACCEPTANCE_REQUIRE_METAL=1 makes skipped MetalGPU tests fatal.
# On hosted CI without an actual device, per-test PASS/SKIP is always reported.
# Never pulls, rebases or merges user worktrees.
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ARG="${1:-build/spectrafx-validation}"
if [[ "$ARG" = /* ]]; then BUILD="$ARG"; else BUILD="$ROOT/$ARG"; fi
LOG="$BUILD/spectrafx-acceptance"
mkdir -p "$LOG"
JOBS="${AIVERSE_ACCEPTANCE_JOBS:-4}"
if ! [[ "$JOBS" =~ ^[1-9][0-9]*$ ]] || (( JOBS > 64 )); then
    echo "AIVERSE_ACCEPTANCE_JOBS must be 1..64" >&2
    exit 2
fi
OS="$(uname -s)"
REV="$(git -C "$ROOT" rev-parse HEAD 2>/dev/null || echo unknown)"
SUMMARY="$LOG/summary.md"
ERRORS=0
SKIPPED=0
printf '# SpectraFX full native acceptance\n\nCommit: \`%s\`\nPlatform: %s\nBuild: \`%s\`\n\n| Step | Exit | Log |\n|---|---:|---|\n' "$REV" "$OS" "$BUILD" > "$SUMMARY"
echo "[spectrafx] revision=$REV platform=$OS build=$BUILD"

stage() {
    local name="$1"
    shift
    echo "========== $name =========="
    set +e
    "$@" 2>&1 | tee "$LOG/$name.log"
    local status=${PIPESTATUS[0]}
    set -e
    printf '| %s | %s | %s.log |\n' "$name" "$status" "$name" >> "$SUMMARY"
    if (( status != 0 )); then
        ERRORS=$((ERRORS+1))
        echo "[spectrafx] FAIL $name exit=$status"
    else
        echo "[spectrafx] PASS $name"
    fi
    return "$status"
}
skip() {
    local name="$1" reason="$2"
    SKIPPED=$((SKIPPED+1))
    printf '| %s | SKIP | %s |\n' "$name" "$reason" >> "$SUMMARY"
    echo "[spectrafx] SKIP $name: $reason"
}

opts=(
    -S "$ROOT" -B "$BUILD"
    -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON
    -DAIVERSE_BUILD_CLIENT=ON -DAIVERSE_BUILD_HEADLESS=ON
    -DAIVERSE_BUILD_TOOLS=ON
)
# Preserve an existing Mac build cache and avoid needlessly rebuilding.
if [[ ! -f "$BUILD/CMakeCache.txt" ]]; then opts+=(-DCMAKE_BUILD_TYPE=Debug); fi
if [[ "${AIVERSE_ACCEPTANCE_WARNINGS_AS_ERRORS:-0}" == 1 ]]; then
    opts+=(-DCMAKE_COMPILE_WARNING_AS_ERROR=ON)
fi

if stage configure cmake "${opts[@]}"; then
    # Compile genre-neutral SpectraFX first. Host binaries are a separate
    # integration gate and cannot suppress core's focused CTest results.
    targets=(
        aiverse_spectrafx_header_smoke aiverse_spectrafx_contract_tests
        aiverse_vfx_graph_tests aiverse_vfx_ecs_tests aiverse_vfx_assets_tests
        aiverse_spectrafx_retirement_tests
        aiverse_spectrafx_preview_policy_tests aiverse_vfx_gpu_tests
    )
    if [[ "$OS" == Darwin ]]; then
        targets+=(aiverse_spectrafx_metal_gpu_test aiverse_spectrafx_metal_billboard_test
                  aiverse_spectrafx_metal_rhi_test aiverse_spectrafx_metal_scene_test
                  aiverse_spectrafx_metal_allocator_test aiverse_vfx_metal_smoke)
    fi
    if [[ "$OS" == Linux ]] && grep -q '^AIVERSE_VULKAN:BOOL=ON
    stage spectrafx_core_build cmake --build "$BUILD" --config Debug --parallel "$JOBS" --target "${targets[@]}" && target_ok=1 || true
    stage architecture python3 "$ROOT/tools/spectrafx/check_boundaries.py" || true
    stage host_integration_build cmake --build "$BUILD" --config Debug --parallel "$JOBS" --target aiverse_editor aiverse_client || true
    full_ok=0
    stage full_build cmake --build "$BUILD" --config Debug --parallel "$JOBS" && full_ok=1 || true
    if (( full_ok )); then
        stage ctest_full ctest --test-dir "$BUILD" -C Debug --parallel "$JOBS" --timeout 900 --output-on-failure || true
    else
        skip ctest_full "build failed: refuse stale test binaries"
    fi
    if (( target_ok )); then
        stage ctest_spectrafx ctest --test-dir "$BUILD" -C Debug -L SpectraFX --output-on-failure || true
        if [[ "$OS" == Darwin ]]; then
            stage ctest_metal ctest --test-dir "$BUILD" -C Debug -L MetalGPU -V --output-on-failure || true
            metal_args=("$LOG/ctest_metal.log")
            if [[ "${AIVERSE_ACCEPTANCE_REQUIRE_METAL:-0}" == 1 ]]; then
                metal_args+=(--require-all)
            fi
            # CTest exits 0 when tests SKIP. Do not silently call this a
             # full hardware PASS; explicitly report each hardware result.
            stage metal_evidence python3 "$ROOT/tools/spectrafx/verify_metal_ctest.py" "${metal_args[@]}" || true
            if [[ -n "${AIVERSE_SPECTRAFX_SCENE_CAPTURE:-}" ]]; then
                stage visual_review python3 "$ROOT/tools/spectrafx/review_frames.py" \
                    --prefix "$AIVERSE_SPECTRAFX_SCENE_CAPTURE" \
                    --output-prefix "$LOG/spectrafx-visual-review" --strict || true
            fi
        else
            skip ctest_metal "no Apple Metal on this OS"
            skip metal_evidence "no Apple Metal on this OS"
        fi
    else
        skip ctest_spectrafx "targeted build failed"
        skip ctest_metal "targeted build failed"
        skip metal_evidence "targeted build failed"
    fi
else
    skip spectrafx_core_build "configure failed"
    skip architecture "configure failed"
    skip host_integration_build "configure failed"
    skip full_build "configure failed"
    skip ctest_full "configure failed"
    skip ctest_spectrafx "configure failed"
    skip ctest_metal "configure failed"
    skip metal_evidence "configure failed"
fi

printf '\n**Failed stages:** %d  \n**Skipped stages:** %d\n' "$ERRORS" "$SKIPPED" >> "$SUMMARY"
echo
cat "$SUMMARY"
echo "[spectrafx] all logs: $LOG"
# A report with failures MUST exit nonzero, not be labeled green.
if (( ERRORS )); then exit 1; fi
exit 0
 "$BUILD/CMakeCache.txt"; then
        targets+=(aiverse_spectrafx_vulkan_allocator_test)
    fi
    target_ok=0
    stage spectrafx_core_build cmake --build "$BUILD" --config Debug --parallel "$JOBS" --target "${targets[@]}" && target_ok=1 || true
    stage architecture python3 "$ROOT/tools/spectrafx/check_boundaries.py" || true
    stage host_integration_build cmake --build "$BUILD" --config Debug --parallel "$JOBS" --target aiverse_editor aiverse_client || true
    full_ok=0
    stage full_build cmake --build "$BUILD" --config Debug --parallel "$JOBS" && full_ok=1 || true
    if (( full_ok )); then
        stage ctest_full ctest --test-dir "$BUILD" -C Debug --parallel "$JOBS" --timeout 900 --output-on-failure || true
    else
        skip ctest_full "build failed: refuse stale test binaries"
    fi
    if (( target_ok )); then
        stage ctest_spectrafx ctest --test-dir "$BUILD" -C Debug -L SpectraFX --output-on-failure || true
        if [[ "$OS" == Darwin ]]; then
            stage ctest_metal ctest --test-dir "$BUILD" -C Debug -L MetalGPU -V --output-on-failure || true
            metal_args=("$LOG/ctest_metal.log")
            if [[ "${AIVERSE_ACCEPTANCE_REQUIRE_METAL:-0}" == 1 ]]; then
                metal_args+=(--require-all)
            fi
            # CTest exits 0 when tests SKIP. Do not silently call this a
            # real 3/3 GPU PASS; explicitly report each hardware result.
            stage metal_evidence python3 "$ROOT/tools/spectrafx/verify_metal_ctest.py" "${metal_args[@]}" || true
            if [[ -n "${AIVERSE_SPECTRAFX_SCENE_CAPTURE:-}" ]]; then
                stage visual_review python3 "$ROOT/tools/spectrafx/review_frames.py" \
                    --prefix "$AIVERSE_SPECTRAFX_SCENE_CAPTURE" \
                    --output-prefix "$LOG/spectrafx-visual-review" --strict || true
            fi
        else
            skip ctest_metal "no Apple Metal on this OS"
            skip metal_evidence "no Apple Metal on this OS"
        fi
    else
        skip ctest_spectrafx "targeted build failed"
        skip ctest_metal "targeted build failed"
        skip metal_evidence "targeted build failed"
    fi
else
    skip spectrafx_core_build "configure failed"
    skip architecture "configure failed"
    skip host_integration_build "configure failed"
    skip full_build "configure failed"
    skip ctest_full "configure failed"
    skip ctest_spectrafx "configure failed"
    skip ctest_metal "configure failed"
    skip metal_evidence "configure failed"
fi

printf '\n**Failed stages:** %d  \n**Skipped stages:** %d\n' "$ERRORS" "$SKIPPED" >> "$SUMMARY"
echo
cat "$SUMMARY"
echo "[spectrafx] all logs: $LOG"
# A report with failures MUST exit nonzero, not be labeled green.
if (( ERRORS )); then exit 1; fi
exit 0
