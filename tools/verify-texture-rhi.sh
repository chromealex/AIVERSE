#!/usr/bin/env bash
# Texture RHI focused native verification. Run at the AIVERSE repo root or
# by absolute path. This script performs real builds/tests only when invoked;
# its existence is NOT evidence that the texture branch passed validation.
set -uo pipefail

# Bash reads a script incrementally: without a full-file syntax preflight,
# a broken tail can still configure/build many targets before the parser
# discovers an unmatched quote. Refuse to perform any work in that case.
if ! bash -n "${BASH_SOURCE[0]}"; then
    echo "Texture RHI verifier: Bash syntax check failed; no build started" >&2
    exit 2
fi

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${AIVERSE_TEXTURE_BUILD_DIR:-$repo/build/texture-rhi}"
jobs="${AIVERSE_TEXTURE_JOBS:-4}"
want_editor=0
want_vulkan=0
want_metal=0
cmake_flags=()

usage() {
    cat <<'USAGE'
Usage: tools/verify-texture-rhi.sh [--editor] [--vulkan] [--metal] [--jobs N] [--build-dir PATH] [-DCMAKE_VAR=value ...]
  --editor    Build HTML Studio (requires its built web assets and native host dependencies)
  --vulkan    Build aiverse_vulkan_tests and run vulkan_backend (inspect skips)
  --metal     Build and RUN native Metal Texture3D/array/cube GPU readbacks (macOS only)
  --jobs N    Parallel compilation tasks (default 4)
  --build-dir PATH  Independent CMake build directory
Outputs: <build-dir>/verify-logs/{configure,build,ctest,summary}.log/txt
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --editor) want_editor=1; shift ;;
        --vulkan) want_vulkan=1; shift ;;
        --metal) want_metal=1; shift ;;
        --jobs)
            [[ $# -ge 2 ]] || { echo "--jobs requires a value" >&2; exit 2; }
            jobs="$2"; shift 2 ;;
        --build-dir)
            [[ $# -ge 2 ]] || { echo "--build-dir requires a path" >&2; exit 2; }
            build="$2"; shift 2 ;;
        -D*) cmake_flags+=("$1"); shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage; exit 2 ;;
    esac
done
if ! [[ "$jobs" =~ ^[0-9]+$ ]] || [[ "$jobs" -lt 1 ]]; then
    echo "--jobs must be a positive integer" >&2
    exit 2
fi

if [[ "$want_metal" -eq 1 && "$(uname -s)" != "Darwin" ]]; then
    echo "--metal requires macOS with a native Metal GPU" >&2
    exit 2
fi

if [[ "$want_editor" -eq 1 ]]; then
    cmake_flags+=("-DAIVERSE_BUILD_STUDIO_HTML5=ON")
fi

# Never register hardware CTests during ordinary source verification,
# even if this build directory previously ran in --metal mode.
if [[ "$want_metal" -eq 1 ]]; then
    cmake_flags+=("-DAIVERSE_TEXTURE_METAL_GPU_TESTS=ON")
else
    cmake_flags+=("-DAIVERSE_TEXTURE_METAL_GPU_TESTS=OFF")
fi

mkdir -p "$build/verify-logs"
logs="$build/verify-logs"
: >"$logs/build.log"
: >"$logs/ctest.log"
summary="$logs/summary.txt"
{
    printf 'AIVERSE Texture RHI verification\n'
    printf 'UTC %s\n' "$(date -u +'%Y-%m-%dT%H:%M:%SZ')"
    printf 'Repository: %s\n' "$repo"
    printf 'Build directory: %s\n' "$build"
    if command -v git >/dev/null 2>&1; then
        printf 'Commit: %s\n' "$(git -C "$repo" rev-parse HEAD 2>/dev/null || echo unknown)"
        printf 'Branch: %s\n' "$(git -C "$repo" branch --show-current 2>/dev/null || echo unknown)"
    fi
} >"$summary"

echo "Configuring CMake... (see $logs/configure.log)"
if cmake -S "$repo" -B "$build" -DAIVERSE_BUILD_TESTS=ON "${cmake_flags[@]+"${cmake_flags[@]}"}" \
    >"$logs/configure.log" 2>&1; then
    echo "CONFIGURE PASS" | tee -a "$summary"
else
    echo "CONFIGURE FAIL (see $logs/configure.log)" | tee -a "$summary"
    exit 1
fi

targets=(
    aiverse_texture_contract_test
    aiverse_render_target_set_test
    aiverse_texture_streaming_test
    aiverse_texture_mip_residency_test
    aiverse_texture_resident_streamer_test
    aiverse_virtual_texture_pages_test
    aiverse_virtual_texture_page_table_test
    aiverse_virtual_texture_streamer_test
    aiverse_virtual_texture_mip_pipeline_test
    aiverse_virtual_texture_disk_queue_test
    aiverse_virtual_texture_file_streamer_test
    aiverse_virtual_texture_prefetch_test
    aiverse_virtual_texture_feedback_test
    aiverse_virtual_texture_feedback_pass_test
    aiverse_gpu_icb_material_contract_test
    aiverse_ktx2_layers_test
    aiverse_ktx2_transcode_test
    aiverse_dds_layers_test
    aiverse_texture_asset_test
    aiverse_hdr_texture_test
    aiverse_exr_texture_test
)
if [[ "$want_editor" -eq 1 ]]; then targets+=(aiverse_desktop_editor); fi
if [[ "$want_vulkan" -eq 1 ]]; then targets+=(aiverse_vulkan_tests); fi
if [[ "$want_metal" -eq 1 ]]; then targets+=(aiverse_metal_texture_gpu_test); fi

failures=0
for target in "${targets[@]}"; do
    echo "BUILD $target"
    {
        printf '\n===== %s =====\n' "$target"
        cmake --build "$build" --target "$target" --parallel "$jobs"
    } >>"$logs/build.log" 2>&1
    status=$?
    if [[ "$status" -eq 0 ]]; then
        echo "BUILD PASS $target" | tee -a "$summary"
    else
        echo "BUILD FAIL $target (exit $status)" | tee -a "$summary"
        failures=$((failures+1))
    fi
done

test_filter='^(texture_contract|render_target_set|texture_streaming|texture_mip_residency|texture_resident_streamer|virtual_texture_pages|virtual_texture_page_table|virtual_texture_streamer|virtual_texture_mip_pipeline|virtual_texture_disk_queue|virtual_texture_file_streamer|virtual_texture_prefetch|virtual_texture_feedback|virtual_texture_feedback_pass|gpu_icb_material_contract|virtual_texture_feedback_shaders|virtual_texture_feedback_msl|virtual_texture_sampling_glsl|virtual_texture_sampling_vulkan_spirv|virtual_texture_sampling_msl|virtual_texture_pack|ktx2_layers|ktx2_transcode|dds_layers|texture_asset|hdr_texture|exr_texture)$'
if [[ "$want_vulkan" -eq 1 ]]; then
    test_filter='^(texture_contract|render_target_set|texture_streaming|texture_mip_residency|texture_resident_streamer|virtual_texture_pages|virtual_texture_page_table|virtual_texture_streamer|virtual_texture_mip_pipeline|virtual_texture_disk_queue|virtual_texture_file_streamer|virtual_texture_prefetch|virtual_texture_feedback|virtual_texture_feedback_pass|gpu_icb_material_contract|virtual_texture_feedback_shaders|virtual_texture_feedback_msl|virtual_texture_sampling_glsl|virtual_texture_sampling_vulkan_spirv|virtual_texture_sampling_msl|virtual_texture_pack|ktx2_layers|ktx2_transcode|dds_layers|texture_asset|hdr_texture|exr_texture|vulkan_backend)$'
fi

# Record actual CTest registration of optional validators. Neither
# executable discovery nor registration proves a shader was compiled.
{
    printf '\nShader validator registration:\n'
    ctest --test-dir "$build" -N -R '^(virtual_texture_feedback_shaders|virtual_texture_feedback_msl|virtual_texture_sampling_glsl|virtual_texture_sampling_vulkan_spirv|virtual_texture_sampling_msl)$'
} >>"$logs/configure.log" 2>&1

if command -v glslangValidator >/dev/null 2>&1; then
    echo "GLSL validator: present (check CTest log for execution)" | tee -a "$summary"
else
    echo "GLSL validator: NOT INSTALLED (shader compilation not verified)" | tee -a "$summary"
fi
if [[ "$(uname -s)" == "Darwin" ]] && command -v xcrun >/dev/null 2>&1; then
    echo "Metal validator: xcrun present (check CTest log for execution)" | tee -a "$summary"
else
    echo "Metal validator: NOT AVAILABLE on this host" | tee -a "$summary"
fi

echo "Running focused CTest... (see $logs/ctest.log)"
ctest --test-dir "$build" --output-on-failure -R "$test_filter" \
    >"$logs/ctest.log" 2>&1
ctest_status=$?
if [[ "$ctest_status" -eq 0 ]]; then
    echo "CTEST PASS (inspect log for SKIPPED tests and GPU execution)" | tee -a "$summary"
else
    echo "CTEST FAIL (exit $ctest_status)" | tee -a "$summary"
    failures=$((failures+1))
fi

if [[ "$want_vulkan" -eq 1 ]]; then
    echo "VULKAN NOTE: CTest may return success while a missing GPU driver causes a skip." | tee -a "$summary"
    echo "VULKAN NOTE: Read $logs/ctest.log and confirm real Vulkan rendering/readbacks." | tee -a "$summary"
fi
# Separate fail-closed opt-in CTest GPU gate. --no-tests=error prevents
# missing test registration from masquerading as a successful run.
if [[ "$want_metal" -eq 1 ]]; then
    echo "Running native Metal GPU acceptance... (see $logs/metal-gpu.log)"
    ctest --test-dir "$build" --verbose --output-on-failure --no-tests=error \
        -R '^metal_texture_gpu$' >"$logs/metal-gpu.log" 2>&1
    metal_status=$?
    if [[ "$metal_status" -eq 0 ]]; then
        echo "METAL GPU PASS (inspect real readback evidence)" | tee -a "$summary"
    else
        echo "METAL GPU FAIL (exit $metal_status; see $logs/metal-gpu.log)" | tee -a "$summary"
        failures=$((failures+1))
    fi
fi
if [[ "$failures" -gt 0 ]]; then
    echo "OVERALL FAIL: $failures build/test gates failed" | tee -a "$summary"
    exit 1
fi
if [[ "$want_metal" -eq 1 ]]; then
    echo "OVERALL SOURCE AND METAL TEXTURE GPU TESTS PASS (other GPU validation remains separate)" | tee -a "$summary"
else
    echo "OVERALL SOURCE TESTS PASS (GPU validation remains separate)" | tee -a "$summary"
fi
