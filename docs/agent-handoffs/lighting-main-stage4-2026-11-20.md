# AIVERSE lighting main integration — Stage 4: GPU-fenced CPU GI atlas ownership (2026-11-20)

## Branch and source commits
- Repo `chromealex/AIVERSE`; isolated integration branch `integration/lighting-main-stage1-20261120`, original base `main` SHA `f9aed9a705982e2c6b77b29869e309995ce8b163`. `main` itself remains untouched by this stage.
- Donor: `feature/gi-lighting-portable-backends-20261119` at observed `770c7ac1d96ab752cb68bebdf34fbb26896cd335`.
- Source commit `e95a572302ebeee13af9caae500e8f4b25c46eb3` — new `engine/render/GiDynamicProbeTexture.h`, `tests/render/gi_dynamic_probe_texture.cpp`.
- Test registration `7295550a1229fba83b81f83a26a6d1d0bf533c21` — add CTest `lighting_gi_dynamic_probe_texture`; total 10 lighting sources registered in CMake across Stages 1–4.

## Actual implementation
- CPU-reflected RGB L1 probe coefficients are uploaded incrementally to RGBA32F texture, three `float4` texels/probe. Dirty probe IDs are coalesced per 128-probe texture row; Metal/Vulkan GPU writable RGBA16F remains separate.
- Adapts legacy `GiDynamicProbeTexture` to current main's `GpuResourceRetirement` rather than immediately calling `Device::destroyTexture` while previous GPU draws may still sample the atlas.
- `release()` detaches active handles and queues with real current GPU submission token when available; zero/unknown tokens stay quarantined. `collectGpuRetirements()` nonblockingly releases only fence-complete work. `drainRetiredAfterGpuIdle()` explicitly requires host-proven **full graphics queue idle**, and `shutdownAfterGpuIdle()` performs release + drain. `pendingRetiredTextures()` exposes pending texture count.
- Attempts to reinitialize on a different device are rejected while the earlier device still has pending textures, preventing cross-device token confusion. On retirement-queue allocation failure a raw handle is abandoned rather than unsafely destroyed, a deliberate diagnostic/leak tradeoff.
- Extended donor test: after old atlas `release()`, FakeDevice still owns it until explicit full-idle drain; no release based on CPU frame count or compute callback. Ported existing 130-probe two-row upload and row-coalescing coverage. No production renderer integration yet.

## Ownership, compatibility, risks
- Still completely opt-in. No ECS authority, save format, active camera/render pass, material shader or renderer/default lighting changed.
- Host must unbind the borrowed atlas sampler **before** release; if a host destroys `GiDynamicProbeTexture` without full-idle drain, unknown/inflight retired resources are intentionally orphaned rather than destroyed. The host must keep `gfx::Device` alive until it has completed GPU work and called shutdown. This does not establish device-loss recovery.
- The imported helper is not yet integrated with `Renderer3D` texture budget / PBR shader slots, so visible Metal/Vulkan/GLES GI cannot be claimed. No direct Luma light is baked into GI overlay.
- Current main `GpuResourceRetirement` uses genuine `gpuSubmissionFence` and `gpuCompletedFence`. Never substitute CPU frameSerial or mere Luma compute completion.

## Verification and next steps
- PERFORMED: GitHub-source review of old CPU atlas and current main retirement API; successful source/CMake GitHub update acknowledgements.
- NOT RUN: CMake configure, Clang/GCC/AppleClang compile, `lighting_gi_dynamic_probe_texture` or any of the ten focused CTests, full CI, Metal/Vulkan/GLES shader and GPU tests, device lifecycle smoke tests and performance.
- NEXT: Run focused C++20/Release CTests against this exact integration HEAD, address any actual failures in a new code+handoff stage. Then wire opt-in atlas into current Renderer3D with actual modern shader/material receiver/borrow logic, add Vulkan compute and GLES LDR fallbacks, validate GPU parity and only then consider main merge.
