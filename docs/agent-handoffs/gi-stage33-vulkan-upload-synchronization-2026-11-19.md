# Hybrid GI — Stage 33: Vulkan buffer-upload visibility and compute RHI audit (2026-11-19)

## Code, commits and status

Branch `feature/gi-lighting-portable-backends-20261119`. Draft PR #47 -> `feature/hybrid-dynamic-gi`; main integration NOT DONE. Stage 30 described the initial Vulkan compute source; this stage fixes an ordering hole before native tests.

Code commits:
- `7f58dc3ce95d69c320882d59a249a2c1341a4405` — `engine/gfx/vulkan/VkResources.cpp`: immutable `createBuffer` staging copies now include a `VK_ACCESS_TRANSFER_WRITE_BIT -> VK_ACCESS_SHADER_READ_BIT` barrier for GPU BVH reads. Both initial immutable upload and `updateBuffer` barriers include `VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT` **only when** the chosen queue supports compute, preventing an invalid pipeline stage on graphics-only queues.
- `f402d4f92f64ed7a828e639a1d1a06427186c128` — `engine/gfx/vulkan/VkCompute.cpp` explicitly includes `<utility>` for `std::move`.
- `a560800efa4212e14bf8d727a4e38753d67d28e0`, `8e02fb93cad4b9447aa13bf054dc7ccfd2733f3e` — `GiGpuDynamicRuntime.h`, `GiGpuTicketQueue.h` comments now reflect both Metal/Vulkan and the separate GLES/no-compute CPU path.
- `e08533ec21fcebda4a71315c72ca007f987ed956` — `docs/vulkan.md` describes compute ABI, stage/fence guarantees, sampler limits, fallback and NOT RUN status.

## Reason for the barrier fix

The new Vulkan GI kernel reads BVH triangles, nodes, lamps and jobs from `BufferType::Storage` buffers. Vulkan's initial immutable `createBuffer()` used a transfer copy without an explicit dependency from transfer-write to compute shader-read; the previous per-update dependency only covered changed buffers. Even when pre-frame transfer commands execute before main commands, **execution order is not sufficient proof of shader-memory visibility**. The fix places a GPU memory barrier after the copy and conditionally includes compute-read stage. An unsupported graphics-only queue must never see an invalid COMPUTE_SHADER pipeline stage. No unsafe GPU queue wait or CPU readback was introduced.

## Actual verification / next actions

- Actually done: GitHub source reads, exact edits committed; documented API semantics. Native build, CTest, Vulkan validation, actual GI output, Metal and GLES shader compilation, performance profiling: **NOT RUN**.
- Next: run a Vulkan SDK/glslang native compile on Linux/Windows, `VK_LAYER_KHRONOS_validation`, offscreen probe-texture readback after actual completed VkFence and positive/negative changed-light tests. Verify upload barriers and descriptor lifetime on physical GPUs and lavapipe. Run the registered CPU/GLSL source tests and the rest of GI acceptance only once all features are integrated.
- Preserve the source-boundary distinction: GLES3/WebGL2 have no compute requirement, use `GiDynamicRuntime::updateAdaptive`; Vulkan GPU path is optional and source-authored but still unvalidated. Do not merge directly into main while the render/RHI branches are divergent.
