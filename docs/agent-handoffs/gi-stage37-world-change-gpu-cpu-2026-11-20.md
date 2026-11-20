# AIVERSE GI — Stage 37: shared world-change invalidation for GPU + CPU transport (2026-11-20)

## Status / commit history

- Working branch: `feature/gi-lighting-portable-backends-20261119`; PR #47 is still draft and **not merged into current main**.
- Code commits:
  - `ff888c474554b17a50cbd4ad007799236baab2dc` — `engine/render/GiGpuDynamicRuntime.h`: `invalidate(bounds,reason,radius)` and `applyWorldChanges(GiWorldDirtyAccumulator&)`; GPU scheduler marks source revisions stale, even for in-flight Vulkan/Metal jobs.
  - `6b3f997506ddb64756a4344929ddae9119203ff2`, `40044e23b6ee9b7f8c1a283b83b96208d95db0f3` — `engine/render/GiLightingController.h`: one host-facing `applyWorldChanges` for CPU/GPU and `void invalidate` forwarding, without misleading fake affected counts.
  - `50f87a5652cd4459e0f159d77a96fc25c25e5130` — `tests/render/gi_gpu_dynamic_runtime.cpp`: release-safe test source verifying one changed-light region invalidates exactly one probe, hides old GPU atlas while pending, and accepts new atlas only after simulated Metal completion.
  - `981bb6b43c3e87acfbe8bd7d1d175acd27749009` — `tests/render/gi_lighting_controller.cpp`: CPU fallback world/material dirty event triggers atlas clearing and adaptive re-trace.

## Contract

Common workflow: finish authoritative ECS/physics/TerraWeave structural updates; publish read-only snapshot; explicitly update immutable geometry (if topology/albedo changed) using `GiLightingController::setGeometry` / `setGeometryRegion`; submit `GiWorldChange` events (previous and current bounds for movers, transport radius and reason); call `controller.applyWorldChanges(changes)`; within a frame call `updateFrame` and then `bind`. This same API handles GLES3/WebGL2 CPU GI and Metal/Vulkan compute.

The CPU path immediately zeros invalidated coefficients and sparse-uploads their old texture slots before reusing GI. The GPU path increments `GiUpdateScheduler` revisions even during an in-flight compute dispatch; `GiGpuTicketQueue::poll` must reject the obsolete completion, keeps dirty probes pending, and prevents an old atlas binding until all required GPU tickets are fence-confirmed. Changes with more than 128 disjoint regions saturate the existing accumulator into full-volume invalidation.

Important: **A dirty event alone never updates the traced geometry**; material albedo, digging, erosion, falling trees, destructible wall changes and new occluders need a new immutable geometry snapshot and GPU BVH submission. `GiDirtyReason::Environment` is currently an invalidation category only; GPU/CPU transport kernels in this PR compute point/spot bounce, not dynamic sun or sky transport. Do not describe this as automatically complete environment/sun GI.

## Remaining limitations and next action

- Added source regressions are NOT native tests: **no C++ compiler, CTest, MSL/SPIR-V/GLES GLSL compiler, GPU captures or mobile profiling has run**. FakeDevice execution itself has not been performed either.
- Producers do not auto-subscribe to the controller; explicit ECS presentation bridge integration remains for PHYSIS/TerraWeave/WorldScene plants/water/terrain. Use the source-derived `GiWorldGeometryBridge` and `GiGeometryScene` rather than inventing genre-specific components.
- GPU clipmap/cascade owners have separate validity textures; this stage updates the single-volume runtime. Extend equivalent event forwarding for the clipmap controller after reading its existing API and tests, and preserve the latest confirmed fine mask.
- Next: build `aiverse_gi_gpu_dynamic_runtime_test`, `aiverse_gi_lighting_controller_test`, `aiverse_gi_world_dirty_accumulator_test`; run CTest (do not claim PASS until actually executed). After remaining feature integration, run full Metal/GLES/Vulkan final acceptance and explicitly reconcile the diverged main branch.
