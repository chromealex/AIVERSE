# Stage 151 — SpectraFX Volumetrics: CPU gas reference

Owner: SpectraFX continuation agent. Branch: feature/spectrafx-html5-data-interfaces-20261123
Previous HEAD: ad0579b2383322fe455df4d301ec27930695180f
Plan: 589dce9b9fed6d66263fa27565edfc546dd6c1a4
Dense solver: f50a8fe0c7663b01039950a29acaa8802306d387
Native assets and MCP: 71356c23b28e6d3a6a7a85f21b004a962ed3ab76
Fixtures, examples and documentation: 7fb44f51b57bfe16d028479aefe331c928439cb4

User approved volumetric VFX. Reuse review: existing PRISMA local fog is analytical density/noise and depth-clipped raymarch; catalog voxel-world/ticks is authoritative block simulation, not dynamic presentation gas. No duplicate authoritative solver or postprocessing. SpectraFX owns world-space visual volumes; PHYSIS/NEREID stay environmental gameplay owners. Existing SDF lattice is reused as obstacle mask.

New engine/fx/SpectraVolumeGrid.h: cell-centred x-fastest dense 8..64 cubic grid. Density, temperature, fuel, velocity. Fixed 60 Hz tick, spherical continuous sources and explicit impulses, first-order semi-Lagrangian advection, combustion, smoke and heat yield, cooling/decay, host-vector gravity buoyancy and wind acceleration, approximate collocated pressure Jacobi (0..24 iterations), bounded velocity and scalar values, no per-step allocations. Existing SDF resampled on explicit setObstacle; solid cells cleared and constrained velocities. Outside SDF is fluid. Backtrace does not conservatively sweep solids, no physically calibrated reaction or exact conservation. CPU ray reference clips to host-supplied opaque ray distance, integrates emission/absorption in scene-linear color and returns opacity; this is a ray query, not rendered frame or full lighting.

engine/fx/SpectraVolumeAsset.h defines strict authored format aiverse.spectrafx.volume v1, typed config/source/material, optional validated content .sdfbin path via injected reader; atomic read/init. Public through SpectraFX.h. Existing particle compiler unchanged, does not accept the separate volume IR. Unified graph and GPU/scene runtime intentionally deferred. apps/editor/core/SpectraGraphOps.cpp registers fx_volume_validate and fx_volume_probe using current Workspace project guard and bounded resource reader, no writes or scene changes. Probe max 8 ticks and additional 16 Mi estimated cell-work cap; reports native statistics and optional ray. Examples fire/smoke under examples/spectrafx/volumes/.

## Feature delivery evidence
- Implementation/architecture: real CPU solver and ray reference source committed, no competing simulation authority; execution NOT RUN.
- Native API/headless: source available in public header; independent consumer/build/runtime NOT RUN.
- Skill/docs: updated owning skill, roadmap, agent doc, docs/spectrafx-volumetrics.md. Audit NOT RUN.
- MCP: source registration and Workspace regression fixture, registry and actual invocation NOT RUN.
- Studio/Inspector/Graphs: NOT IMPLEMENTED for volume editing or native viewport; separate versioned asset IR is not unified node graph. Pending stages 152–154.
- Persistence: authored config codec source; no transient checkpoint, prefab, package or replay integration; save/load fixture NOT RUN.
- Performance/platforms: bounded grid and operations, no allocations in step, actual timings and platform checks NOT RUN. CPU reference only. GPU Metal/Vulkan absent.
- Dependencies: header-only, no new library/target; compile/link NOT RUN.
- Examples/tests: fire/smoke assets and regression source in existing vfx_graph_runtime and vfx_graph_ops (combustion, sample, ray clipping, SDF mask, gravity/wind, cap, strict roundtrip and probe). CTest NOT RUN.
- Publishing: no visual capture or demo yet; NOT RUN.
- Handoff: this report.

Paths: engine/fx/{SpectraVolumeGrid.h,SpectraVolumeAsset.h,SpectraFX.h}; apps/editor/core/SpectraGraphOps.cpp; tests/{vfx_graph_runtime_test.cpp,vfx_graph_ops_test.cpp}; examples/spectrafx/volumes/{fire.volume.json,smoke.volume.json}; docs/spectrafx-volumetrics.md; owning roadmap, docs/agents/spectrafx.md and skill. GitHub expected-head non-force updates succeeded. No CMake, CTest, native execution, shader compilation, Studio UI, GPU, benchmark, CI or main sync/merge. User previously requested features first, tests later. No feature-ready claim.

Next Stage 152: native GPU dense volume kernels and buffers via RHI (Metal/Vulkan), matching CPU reference, explicit dispatch/fence/budget ownership. Stage 153 world-space depth-aware HDR ray march using renderer composition contract; Stage 154 unified particle/volume/hybrid graph and Studio; Stage 155 sparse/LOD, host field and particle coupling; Stage 156 packaging/replay and acceptance. Previous SDF asset workflow closure remains pending.
