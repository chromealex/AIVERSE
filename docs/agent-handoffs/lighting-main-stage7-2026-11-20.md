# AIVERSE lighting main integration — Stage 7: fail-closed CPU GI GPU-upload errors (2026-11-20)

## Branch / exact source commit
- `chromealex/AIVERSE`, `integration/lighting-main-stage1-20261120` (original current-main base `f9aed9a705982e2c6b77b29869e309995ce8b163`; no main mutation).
- Source commit `9987d0b95e8ecca8c27a0377e2a3ed47fc6190b7`, paths `engine/render/GiMainCpuBridge.h` and `tests/render/lighting_cpu_gi_bridge.cpp`.
- Existing CMake test name `lighting_lighting_cpu_gi_bridge`; no new test target or ABI.

## Bug / fixed behavior

Previously `GiMainCpuBridge::updateFrame` could accept a coherent GPU atlas before starting the CPU trace and then call `atlas_.upload(...)` while `safe_` remained true. If `gfx::Device::updateTexture` failed partway through row uploads (out of memory/device lost/driver RHI exception), the CPU reference had advanced and a partial GPU texture might remain visible through `borrowedAtlas()`. The old atlas could then be presented as valid despite newer authoritative point/spot/environment source changes.

This stage sets `safe_=false` **before** CPU tracing and before any texture upload. It only restores `safe_=true` after every changed row upload has completed without an exception. On error the exception propagates, the old atlas borrow is refused, and a subsequent `updateFrame` cannot blindly continue from already acknowledged probe tickets. The host must call `flushAll()` to republish a complete coherent CPU coefficient atlas before resuming presentation.

## Regression source

The existing Stage5 FakeDevice fixture now uses `FaultDevice : FakeDevice` to inject a thrown `updateTexture` error. It verifies a newer same-key light source revision, thrown frame upload, invalid `borrowedAtlas()`, disabled follow-up frame and successful `flushAll()` recovery. The existing positive geometry/legacy-source/release tests are preserved.

No changes to GPU shaders, Vulkan/GLES/Metal RHI, batching, main Renderer3D, PRISMA, SpectraFX, ECS/save or editor. The policy remains explicit and genre independent.

## Verification truth

- PERFORMED: review of exact current bridge/atlas code and FakeDevice update signature, successful connected-GitHub code commit acknowledgement.
- NOT RUN: native C++ compiler, the 12 registered lighting CTests (including fault injection), CMake configure, full AIVERSE CI, real GLES framebuffer, Metal/Vulkan compute, device loss/OOM test, GPU shader/pixel/frame-timing.
- No tests are marked passed before execution.

## Next action
Compile with C++20/Release `-DNDEBUG -Werror` and execute the focused suite on this exact HEAD, fix actual failures and add a separate results handoff. Then wire the CPU RGBA32F atlas to current model/terrain shaders through explicit material receivers, complete Metal/Vulkan compute direct+GI routes and GLES runtime fallback, and perform one final full-GPU acceptance before merging main.
