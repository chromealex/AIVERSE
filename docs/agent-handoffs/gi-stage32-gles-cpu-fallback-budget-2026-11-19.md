# Hybrid GI — Stage 32: adaptive CPU fallback for GLES3/WebGL2 and Vulkan (2026-11-19)

## Status / commits

Branch: `feature/gi-lighting-portable-backends-20261119`. PR #47 is a draft targeting the original GI feature, not main. SOURCE WRITTEN, NATIVE COMPILATION, CTEST AND REAL MOBILE PERFORMANCE **NOT RUN**.

Code commits:
- `4a873aa5b54ab5812823408e520a1381ee260c94` — `engine/render/GiCpuTraceBudget.h`, backend-independent cost policy.
- `5eb45e0d26f40afa397977e911cb2a0f932f4e45` — `engine/render/GiDynamicRuntime.h`, `updateAdaptive(float3 camera)` plus per-instance `cpuTraceBudget()` configuration.
- `b91db0c0840e387ff2527fba112a2a629890a4ed` — `tests/render/gi_cpu_trace_budget.cpp` source regression.
- `18106adfe356d5c24829db6df4574900edf0faad` — CMake/CTest registration.
- `580b36c5275bee640a6abab29558a30c09f2cf91` — `tests/render/gi_dynamic_runtime.cpp` bounded adaptive probe updates on backend-identity FakeDevice.

## New quality/performance contract

`GiCpuTraceBudget::configure(targetMilliseconds,maxProbes)` sets a hard maximum dirty probes per update and a soft adaptive CPU frame-time target (default 1.25ms, at most 4 probes). `choose(pending)` uses the observed exponential average of wall-clock milliseconds per published/claimed probe to choose 1..maxProbes, never more than pending; one-probe minimum guarantees progress under heavy costs. `observe(claimed,elapsedMs)` adapts future updates without predicting GPU time or blocking a separate thread.

The existing `GiDynamicRuntime::update(explicitProbeBudget,camera)` is unchanged. New opt-in `updateAdaptive(camera)` chooses the budget, calls the same sparse RHI texture upload + CPU tracer, and updates its CPU cost estimate using `std::chrono::steady_clock`. Host chooses this path when `gfx::Backend::GLES3`/WebGL2 or a Vulkan device cannot create storage-image compute. It is fully genre-independent presentation; direct lights and baked GI are still separate.

Runtime initialization resets the cost estimator but keeps the host's target/probe cap. Zero-budget invalidations remain supported via `update(0,camera)` to clear stale atlas texels safely.

## Compatibility and limitations

- Existing API remains available. No ECS/network/save file changes or automatic activation of dynamic GI.
- GLES 3.0 GPU compute is **not** required and is not emulated. It uses CPU tracing and RGBA32F nearest image sampling where supported, with HDR/material tier and fragment texture slot guards from Stage 31.
- A CPU budget is a soft target: one expensive scene probe may exceed it, and texture uploads/driver stalls can fluctuate. No Android device time measurements; baseline 1.25ms/4 probe values are provisional choices, not certified performance guarantees.
- This does not replace a large-world GI acceleration structure, automatically connect light/terrain/vegetation publishers, or implement GPU-based Vulkan ray tracing beyond Stage 30's authored RHI and shader. Host must still drive `updateAdaptive` and `bind`.

## Actual tests and next steps

- Performed: successful GitHub source inspection/commits, assertions authored and CTest registered.
- NOT RUN: CMake/Clang/GCC compilation, CTest, real GLES3/WebGL2 shader compiler, Vulkan GPU, Metal GPU, Android power/performance, full AIVERSE acceptance.
- Next: compile `aiverse_gi_cpu_trace_budget_test` and `aiverse_gi_dynamic_runtime_test` and run their CTests. Benchmark actual mobile CPU GI per-frame budget under moving lights and edited colliders. Probe no-compute GL/GLES fallback at 16 fragment texture slots. Build and validate Stage 30 Vulkan compute pipeline plus actual Vulkan readback. Keep feature off by default pending final parity.
