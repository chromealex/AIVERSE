# Stage 147 — shared immutable GPU SDF storage

Owner: SpectraFX continuation agent. Branch: feature/spectrafx-html5-data-interfaces-20261123
Previous HEAD: 50429253f3b9071d13c02ee5a5a07906e43a8187
Plan: 9c14c29cc7f7e52de151b5e3ac3c4110cf269b32
Implementation: 1fb799cb787fefebbe666006f6fa56feac00f9ce
Fence accounting, fixture and docs: 4c704ad661daccec0d940eed59b051a073e521fb

Added SharedSdfGpuCache and SharedSdfGpuPayload in engine/fx/SpectraSharedSdfGpuCache.h. Shares exact slot-13 immutable material lane and SDF words using hash + full equality. Weak registry capped at 4096 live entries; final lease destroys via existing RHI buffer lifetime and retires canonical Device ledger charge with max observed submission fence. Vulkan pre-frame immutable upload has no reported fence: retain charge at unknownFence if released without a later marked submission. This is conservative and can retain budget until Device teardown; future explicit upload fence API should remove that gap. Cache/leases require Device outliving users and render-owner thread only.

ComputeParticles uses optional cache, no sharing of mutable depth collision buffers. Unique payload reserves its bytes once; private emitter and physical resize envelope exclude shared SDF extra bytes (base estimate still conservative). Shared reference bytes reported separately. GpuPoolWorld creates one cache per pool, passes it to groups, drops stored option reference, counts unique live cache storage once. Independent native ComputeEmitterDesc/GraphRuntimeOptions can share an explicit cache. No packed multi-volume atlas, offsets, 3D texture, cross-pool default sharing or shader ABI change. Authored SDF graph and CPU behavior unchanged; no new Studio/MCP operation needed for this transparent optimization.

Paths: engine/fx/{SpectraSharedSdfGpuCache.h,SpectraGpuParticles.h,SpectraGpuParticles.cpp,SpectraGraphRuntime.h,SpectraGraphRuntime.cpp,SpectraGpuPoolWorld.h,SpectraGpuPoolWorld.cpp}; tests/spectrafx_metal_allocator.mm; docs/spectrafx-modernization-roadmap-2026-11-23.md; docs/agents/spectrafx.md; skills/visuals/aiverse-spectrafx/SKILL.md.

Regression source uses real Metal Device and ComputeParticles: equal payload reuse, different content separation, one ledger reservation, both dispatches, first release retains buffer, remaining emitter runs, final release and completion restore budget. Tests NOT RUN. Existing GPU code for buffer create/destroy and submission fence inspected in GfxMetal.mm, VkResources.cpp, GfxVulkan.cpp and VkDevice.h. No compile, CTest, Metal/Vulkan execution, performance measurements, CI, UI/visual or skill audit. GitHub expected-head non-force updates succeeded; implementation diff inspected. No main merge/sync. Feature readiness NOT established. Native API and docs present in source; no new serialization, ECS persistence or editor affordance applicable. Real platform/fence verification pending.

Next: Stage 148 spatial acceleration for mesh baking, then higher resolution; packed heterogeneous atlas still open as separate GPU work. Retain Stage 146 package/replay closure, live watcher and indexing gaps. Do not claim complete atlas or completed feature set.
