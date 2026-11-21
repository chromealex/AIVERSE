# STRATA Stage22 — fix all four system QA findings

User requested fixes and publication. Branch `codex/strata-batching-integration`; implementation `32535df16573d3c6bcfd8b0be28aa5b26d0e29ed`. Follows [Stage21 QA](strata-stage21-system-qa-2026-11-20.md); source base main076139d12.

## Changes

- QA01: tests/render/ktx2_transcode.cpp uses typed volatile function-pointer initialization to verify HDR import ABI and retain link references; removed always-true address comparisons, including the optional libktx symbol checks. No disabling of compiler diagnostics, no codec/runtime changes.
- QA02: tests/render/strata_vulkan_kernel.cpp separates `--compile-only` from optional runtime (unavailable returns77) and `--require-device` (unavailable returns1). Invalid CLI returns2. CMake adds strata_vulkan_compile and SKIP_RETURN_CODE77 for runtime. Focused Lavapipe CI runs compiler CTest then the strict executable, so missing compute cannot silently pass CI.
- QA03: actual newlines replace literal backslash-n in LOD CSV header/data; selection algorithm unchanged.
- QA04: docs/strata.md now starts with current implementation vs verified coverage, identifies Rosetta/native-arm64 distinction and remaining hardware/visual gates, and explicitly labels numbered implementation stages as historical. Removed obsolete no-render-integration statements from current scope/builder contracts.

Public engine API, shader ABI, save/network/ECS data and production opt-ins unchanged. Only test CLI/status is deliberately changed. No rendering product fix is implied: executed rendering fixtures already passed in Stage21.

## Actual verification

Evidence `/Volumes/KINGSTON/AIVERSE-builds/strata-qa-fixes-20261120`; builds/temp/module/Python caches on KINGSTON. Existing build `/Volumes/KINGSTON/AIVERSE-builds/strata-batching-20261120/debug`, Debug/assertions enabled, x86_64 on Apple M3 Ultra/macOS15.6.

1. `cmake --build <build> --target aiverse_ktx2_transcode_test aiverse_strata_lod_benchmark aiverse_strata_vulkan_kernel_tests --parallel 8`: PASS, no compiler errors/warnings.
2. `ctest --test-dir <build> --verbose --output-on-failure --no-tests=error --parallel 4 -R '^(strata_|ktx2_transcode$)' --output-junit <evidence>/ctest.xml`: **17 registered,16 PASS,1 correctly SKIPPED,0 failures**,2.24s. Explicitly inspected JUnit: strata_vulkan_compile PASS, strata_vulkan_kernel has skipped element.
3. Direct Vulkan CLI status regression: compile-only0, optional77, require-device1, unknown option2, all expected on this loader-less Mac; JSON and separate logs retained. Expected strict failure is not GPU execution acceptance.
4. Three fresh LOD invocations parsed with Python csv.reader: exactly4 rows,6 columns, expected header/policy labels, numeric240 frames and transitions0/122/2, stable checksums, no literal backslash-n. PASS3/3.
5. `xcrun clang++ -std=c++20 -Wall -Wextra -Werror=address -I. -Ithird_party/meshoptimizer/src -fsyntax-only tests/render/ktx2_transcode.cpp`: PASS. This is Clang, NOT a claim of GCC/Linux CI success.
6. `git diff --check`: PASS; read current docs summary and existing-reference targets.

NOT RUN: new-head Linux GCC, optional libktx-enabled link/runtime (package unavailable), Vulkan GPU execution, GLES/GL/WebGL hardware, native arm64 execution, lossy visual/spot-shadow/pressure-popping acceptance. Existing six Metal fixtures and50 streaming invocations passed in Stage21; not repeated for test/docs-only changes. Hardware acceptance remains open; no performance claim or production default enablement.

## Publication / next step

Publish QA report, these fixes and separate handoff by ordinary PR into main as requested; use expected-head merge, never direct protected-main push or force. Inspect new-head CI when runners execute and address actual new failures in a bounded follow-up. The four diagnosed code/test/docs issues are corrected; deferred platform/visual scenarios are not relabelled PASS.
