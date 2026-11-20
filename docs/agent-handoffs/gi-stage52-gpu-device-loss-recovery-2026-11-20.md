# AIVERSE GI — Stage 52: device-loss abort and fail-closed GPU publication (2026-11-20)

## Branch and committed source

Repo `chromealex/AIVERSE`, branch `feature/gi-lighting-portable-backends-20261119`, draft PR #47 against `feature/hybrid-dynamic-gi`; do not merge into main without validation.

- `edb4d09582519298a108fceaa69a7c65e1182f06` — `engine/render/GiGpuTicketQueue.h`: `poll` now treats `!Device::gpuHealthy()` as a terminal failed submission. Mark each batch unsuccessful and ready, abort its scheduler tickets and leave its probes dirty **without requiring a Vulkan VkFence serial or Metal readback callback that may never arrive**. This is NOT a fabricated successful GPU completion.
- `6fde97c2e722b726bddf37909b378317f6224ee2` — `tests/render/gi_gpu_ticket_queue.cpp`: fake Vulkan device loses GPU after submitting serial=12, but `completedFrameSerial()` remains 11 forever. Poll must abort (not accept or mark stale as completed) and release the queue's pending state; shutdown can then proceed. Old test falsely simulated VkFence completion on a lost GPU.
- `916b7efcf6fb5d8ba804ffba0c871d27417a23a0` — `GiGpuTicketQueue` now requires `Device::gpuHealthy()` to return true from `safeToSample`, and refuses *new* GI dispatches while the device reports lost/failed. Prevents a completed stale atlas from being sampled or repeated GPU submission attempts during a lost-device interval.
- `330fd62dca767ee2ced28587e700986faa8a4cbe` — extends FakeDevice regression to assert no new GPU work is submitted while unhealthy and dirty scheduler probes stay pending for a restored device.

## API invariant and actual status

The existing completion contract is unchanged on healthy devices: Vulkan tickets are accepted only once `completedFrameSerial>=submittedFrameSerial` reports a **genuine completed VkFence**; Metal requires a verified asynchronous readback/callback or a real external fence. Lost-device condition is a separate terminal cancellation: an old command cannot ever be reported as a successful new GI result. `poll` returns the queued tickets to dirty state; `safeToSample` fails until health and correctly rebuilt GPU resources are restored. An unhealthy RHI must be recreated/reinitialized by the application, not simply toggled to healthy while still retaining invalid handles.

On a device loss with an in-flight GPU command, aborting **scheduler tickets** does not itself prove the GPU is idle, restore VkDevice, or make it safe to reuse raw driver memory on a still-executing queue. The underlying Vulkan/Metal backend owns teardown and driver-specific device-lost cleanup. This stage prevents GI CPU-side lifetime from deadlocking waiting for an impossible completion; do not claim physical GPU memory resource safety without the RHI driver tests.

**Performed:** GitHub connector source inspection and successful commits. **NOT RUN:** CMake/Clang/GCC build, CTest, Vulkan validation/lavapipe, Metal callback loss fixture on real driver, GLES3, GPU pixels, full suite. The authored `gi_gpu_ticket_queue` test is NOT an executed PASS, and GH Actions queued statuses do not count.

## Next steps

1. Build and run `aiverse_gi_gpu_ticket_queue_test` with `gi_gpu_dynamic_runtime`, `gi_lighting_controller`, `vulkan_backend`; report actual result with tested commit SHA. Exercise a real Vulkan device loss or forced `vkWaitForFences` failure and ensure no stale probe release or GPU resource use.
2. Real Metal async callback failure and device-lost cleanup/restart; confirm queue scheduler cancellation does not race a still-live Metal command.
3. Complete remaining backend parity, GI large-world source streaming and native TerraWeave ECS event publisher compatibility. After features, run the single full acceptance suite and reconcile current main.
