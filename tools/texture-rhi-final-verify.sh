#!/usr/bin/env bash
# AIVERSE Texture RHI native macOS one-shot validation.
# Verify a clean main or feature checkout at an explicit revision.
# bash tools/texture-rhi-final-verify.sh --build-root PATH [--expected-ref REF] [--full-engine] [--jobs N]
# GPU tests are always separate processes and run sequentially.
set -euo pipefail
JOBS=4
FULL_ENGINE=0
BUILD_ROOT="${AIVERSE_TEXTURE_FINAL_BUILD_ROOT:-}"
EXPECTED_REF=HEAD
usage() {
  echo "Usage: bash tools/texture-rhi-final-verify.sh --build-root ABSOLUTE_PATH [--expected-ref REF] [--full-engine] [--jobs N]"
}
while (($#)); do
  case "$1" in
    --help|-h) usage; exit 0;;
    --build-root|--expected-ref)
      (($#>=2)) && [[ -n "$2" ]] || { usage >&2; exit 2; }
      if [[ "$1" == --build-root ]]; then BUILD_ROOT="$2"; else EXPECTED_REF="$2"; fi
      shift 2;;
    --full-engine) FULL_ENGINE=1; shift;;
    --jobs)
      (($#>=2)) && [[ "$2" =~ ^[1-9][0-9]*$ ]] || {
        echo "FAIL: --jobs needs a positive integer" >&2; exit 2;
      }
      JOBS="$2"; shift 2;;
    *) usage >&2; exit 2;;
  esac
done
ROOT="$(git -C "$(dirname "$0")/.." rev-parse --show-toplevel)"
cd "$ROOT"
# A merged feature must remain verifiable. Pin the caller's chosen revision,
# not an obsolete feature branch, and reject uncommitted source changes.
CURRENT_BRANCH="$(git symbolic-ref --quiet --short HEAD || true)"
CURRENT_SHA="$(git rev-parse --verify HEAD)"
EXPECTED_SHA="$(git rev-parse --verify "$EXPECTED_REF^{commit}")"
if [[ "$CURRENT_SHA" != "$EXPECTED_SHA" ]] || [[ -n "$(git status --porcelain --untracked-files=normal)" ]]; then
  echo "FAIL: verification requires a clean checkout at $EXPECTED_REF ($EXPECTED_SHA)" >&2
  exit 2
fi
case "$BUILD_ROOT" in
  /*) ;;
  *) echo "FAIL: --build-root must name an absolute build location" >&2; exit 2;;
esac
printf 'Validated source: %s (%s)\n' "$CURRENT_SHA" "${CURRENT_BRANCH:-detached HEAD}"
[[ "$(uname -s)" == "Darwin" ]] || {
  echo "FAIL: native Metal Mac required (no silent GPU skip)" >&2; exit 2;
}
mkdir -p "$BUILD_ROOT"
BUILD_ROOT="$(cd "$BUILD_ROOT" && pwd -P)"
LOGS="$BUILD_ROOT/logs"
SAFE="$BUILD_ROOT/safe"
GPU="$BUILD_ROOT/gpu"
export TMPDIR="$BUILD_ROOT/tmp"
export CLANG_MODULE_CACHE_PATH="$BUILD_ROOT/module-cache"
export PYTHONPYCACHEPREFIX="$BUILD_ROOT/python-cache"
mkdir -p "$LOGS" "$TMPDIR" "$CLANG_MODULE_CACHE_PATH"
# A failed/new run must never leave an old PASS manifest visible.
rm -f "$LOGS/acceptance.txt" "$LOGS/failure.txt"
record_failure() {
  local status="$?"
  if ((status != 0)); then
    {
      printf 'result=FAIL\n'
      printf 'sha=%s\n' "$(git rev-parse HEAD)"
      printf 'exit_code=%s\n' "$status"
      printf 'logs=%s\n' "$LOGS"
    } > "$LOGS/failure.txt"
    echo "FAIL: one-shot texture check exited $status; see $LOGS/failure.txt" >&2
  fi
}
# EXIT also catches an explicit 'exit 1' after a CTest marker mismatch.
trap record_failure EXIT
trap 'echo "FAIL: one-shot texture verification stopped at line $LINENO; logs in $LOGS" >&2' ERR
run_logged() {
  local name="$1"
  shift
  printf "\n======= %s =======\n" "$name"
  "$@" 2>&1 | tee "$LOGS/$name.log"
}
printf 'HEAD=%s jobs=%s full-engine=%s\n' "$(git rev-parse HEAD)" "$JOBS" "$FULL_ENGINE"

run_logged safe-configure cmake -S . -B "$SAFE" \
  -DCMAKE_BUILD_TYPE=Debug -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_TEXTURE_METAL_GPU_TESTS=OFF \
  -DAIVERSE_TEXTURE_METAL_ICB_GPU_TESTS=OFF \
  -DAIVERSE_TEXTURE_METAL_RENDERER_GRAPH_GPU_TESTS=OFF \
  -DAIVERSE_TEXTURE_METAL_GRAPH_VT_ICB_GPU_TESTS=OFF \
  -DAIVERSE_TEXTURE_METAL_GRAPH_VT_SEAM_GPU_TESTS=OFF \
  -DAIVERSE_TEXTURE_METAL_VT_BILINEAR_GPU_TESTS=OFF \
  -DAIVERSE_TEXTURE_METAL_GRAPH_VT_GUTTER_GPU_TESTS=OFF
safe_targets=(
  aiverse_shader_graph_tests
  aiverse_texture_contract_test aiverse_render_target_set_test
  aiverse_ktx2_layers_test aiverse_ktx2_transcode_test
  aiverse_texture_asset_test aiverse_hdr_texture_test
  aiverse_exr_texture_test aiverse_dds_layers_test
  aiverse_texture_streaming_test aiverse_texture_mip_residency_test
  aiverse_texture_resident_streamer_test
  aiverse_virtual_texture_pages_test aiverse_virtual_texture_page_table_test
  aiverse_virtual_texture_streamer_test aiverse_virtual_texture_mip_pipeline_test aiverse_virtual_texture_disk_queue_test
  aiverse_virtual_texture_file_streamer_test aiverse_virtual_texture_prefetch_test
  aiverse_virtual_texture_feedback_test aiverse_virtual_texture_feedback_pass_test
  aiverse_gpu_icb_material_contract_test
)
safe_tests=(
  shader_graph texture_contract render_target_set ktx2_layers ktx2_transcode
  texture_asset hdr_texture exr_texture dds_layers texture_streaming
  texture_mip_residency texture_resident_streamer
  virtual_texture_pages virtual_texture_page_table virtual_texture_streamer
  virtual_texture_mip_pipeline virtual_texture_disk_queue virtual_texture_file_streamer virtual_texture_prefetch
  virtual_texture_feedback virtual_texture_feedback_pass gpu_icb_material_contract
)
run_logged safe-build cmake --build "$SAFE" --target ${safe_targets[@]} --parallel "$JOBS"
safe_regex="$(IFS='|'; echo "${safe_tests[*]}")"
run_logged safe-ctest ctest --test-dir "$SAFE" \
  --output-on-failure --no-tests=error --parallel "$JOBS" -R "^($safe_regex)$"
if ctest --test-dir "$SAFE" -N -R '^(virtual_texture_pack|virtual_texture_feedback_(shaders|msl)|virtual_texture_sampling_(glsl|vulkan_spirv|msl))$' |
   grep -Eq 'Test +#[0-9]'; then
  run_logged safe-optional-vt-ctest ctest --test-dir "$SAFE" \
    --output-on-failure --no-tests=error \
    -R '^(virtual_texture_pack|virtual_texture_feedback_(shaders|msl)|virtual_texture_sampling_(glsl|vulkan_spirv|msl))$'
fi
if ((FULL_ENGINE)); then
  run_logged full-engine-build cmake --build "$SAFE" --parallel "$JOBS"
  run_logged full-engine-ctest ctest --test-dir "$SAFE" \
    --output-on-failure --no-tests=error --parallel "$JOBS"
fi

# The eight real Metal tests are opt-in and compiled from current source
# before ANY CTest executes. CTest tests run serially, in fresh processes.
run_logged gpu-configure cmake -S . -B "$GPU" \
  -DCMAKE_BUILD_TYPE=Debug -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_TEXTURE_METAL_GPU_TESTS=ON \
  -DAIVERSE_TEXTURE_METAL_ICB_GPU_TESTS=ON \
  -DAIVERSE_TEXTURE_METAL_RENDERER_GRAPH_GPU_TESTS=ON \
  -DAIVERSE_TEXTURE_METAL_GRAPH_VT_ICB_GPU_TESTS=ON \
  -DAIVERSE_TEXTURE_METAL_GRAPH_VT_SEAM_GPU_TESTS=ON \
  -DAIVERSE_TEXTURE_METAL_VT_BILINEAR_GPU_TESTS=ON \
  -DAIVERSE_TEXTURE_METAL_GRAPH_VT_GUTTER_GPU_TESTS=ON
gpu_targets=(
  aiverse_metal_texture_gpu_test aiverse_metal_texture_icb_graph_gpu_test
  aiverse_metal_renderer_graph_icb_gpu_test
  aiverse_metal_renderer_graph_vt_icb_gpu_test
  aiverse_metal_renderer_graph_vt_seam_gpu_test
  aiverse_metal_vt_bilinear_gpu_test
  aiverse_metal_renderer_graph_vt_gutter_gpu_test
)
gpu_tests=(
  metal_texture_gpu metal_texture_icb_graph_gpu
  metal_renderer_graph_icb_gpu metal_renderer_graph_vt_icb_gpu
  metal_renderer_graph_vt_seam_gpu metal_vt_bilinear_gpu
  metal_renderer_graph_vt_gutter_gpu metal_renderer_indexed_vt_gpu
)
markers=(
  ''
  'PASS: GPU ICB graph sources=4, per-command textures/U2 pixel parity'
  'PASS: production Renderer3D compiled Shader Graph 4-source mixed Metal GPU ICB pixels'
  'PASS: production Renderer3D Shader Graph+disk VT mixed Metal GPU ICB coarse/fine/LRU pixel parity'
  'PASS: Renderer3D Shader Graph disk VT missing-page and Repeat/MirrorRepeat seam GPU ICB oracle'
  'PASS: native Metal canonical VT bilinear nonuniform gutter seam and Repeat/MirrorRepeat CPU pixel oracle'
  'PASS: production Renderer3D Shader Graph nonuniform guttered disk VT mixed Metal GPU ICB seam parity'
  'PASS: production Renderer3D indexed VT nonuniform direct and ICB fallback parity'
)
run_logged gpu-build cmake --build "$GPU" --target ${gpu_targets[@]} --parallel "$JOBS"
for index in "${!gpu_tests[@]}"; do
  test_name="${gpu_tests[$index]}"
  run_logged "gpu-$test_name" ctest --test-dir "$GPU" --verbose \
    --output-on-failure --no-tests=error -R "^$test_name$"
  # Some macOS NSApplication-hosted tests used to terminate with status 0
  # even after a fixture check printed FAIL:. Reject all such false PASSes.
  if grep -Eq '(^|[[:space:]])FAIL:' "$LOGS/gpu-$test_name.log"; then
    echo "FAIL: $test_name printed a native test assertion error" >&2
    exit 1
  fi
  marker="${markers[$index]}"
  if [[ -n "$marker" ]] && ! grep -Fq "$marker" "$LOGS/gpu-$test_name.log"; then
    echo "FAIL: $test_name CTest lacked mandatory execution marker: $marker" >&2
    exit 1
  fi
done
{
  echo "sha=$(git rev-parse HEAD)"
  echo "safe_texture_tests=22"
  echo "native_metal_gpu_tests=8"
  echo "full_engine=$FULL_ENGINE"
  echo "result=PASS"
} | tee "$LOGS/acceptance.txt"
echo "PASS: all 22 portable texture/graph and 8 native Metal GPU tests"
echo "Logs: $LOGS"
