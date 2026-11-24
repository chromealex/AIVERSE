# Stage 115 — production Metal allocator stress acceptance

Branch `feature/spectrafx-html5-data-interfaces-20261123`. Parent handoff `6b0743ff426ae2f0678b2f7dfaf2e18ed261a125`; plan `ff208708151136c2e51207ea9422f20ee5432dd3`; implementation `e68bc6c837814430cfc9ed41e43a36ebcdee87bb`; pixel/fence fixture extension `aeac879595de89ed73b10da11ffee26dd22bc3b7`. Main untouched.

## Changes
- `tests/spectrafx_metal_allocator.mm`: production ComputeParticles + GfxMetal, real headless offscreen Metal device, late drawable acquisition, 256/65536/262144 capacities in expanded and procedural modes. Repeated growth, exact alive/free partition and owner continuity, indirect compute, zero-visible camera and re-entry, source kill/reuse, life expiration, asynchronous idle proof/shrink, checked reset, stable Medium LOD, real offscreen indirect-draw pixel readback, shared budget denial/overlap/completion/release. Prints capacity/geometry/growth/shrink/bytes/elapsed QA time; elapsed includes blocking diagnostics and is NOT a GPU performance benchmark. No Metal device => SKIP 77.
- `engine/fx/SpectraGpuParticles.{h,cpp}`: explicit blocking QA-only auditGpuAllocation, validates slot partition uniqueness and bounds, exact counters, VM errors, owner tokens and current draw arguments. No normal-frame readback. Requires between completed frames; not a GPU checkpoint or ECS save.
- `tests/vfx_metal_compaction_smoke.mm`: repair older standalone shader uniform size to 160 bytes and bind current required VM/attribute/material/kill buffers, empty VM ABI 1.
- `CMakeLists.txt`: add macOS CTest spectrafx_metal_allocator with production runtime linkage, MetalGPU/SpectraFX/RHI labels, timeout 300, skip code 77.
- `docs/spectrafx.md`: roadmap/acceptance notes.

## Gate and actual evidence
Source and CTest registration committed, implementation commit/changed paths and relevant production RHI contracts read back. Native compile, CTest, actual Metal device execution, screenshots, performance, GPU validation and CI NOT RUN in this session. No Apple GPU available here. New audit is diagnostic native API; no authored graph, ECS, skill/MCP/Studio property or persisted schema changes. Existing integrations and independent native consumer compile NOT RUN. Test source exercises existing production owners; no fake pass. Publishing/devlog and measured GPU throughput/driver residency remain NOT RUN. Main integration remains blocked on merged-tree Mac quick gate.

## Mac reproduction
From repository root, checkout the feature branch and verify exact HEAD. Run:
```sh
./run-tests.sh --quick --filter 'spectrafx_metal_allocator|vfx_metal_compaction_smoke|vfx_gpu_backend' --build-dir /Volumes/KINGSTON/AIVERSE-builds/spectrafx-stage115 --ctest-jobs 1
```
Then run complete quick gate with same build-dir and no filter. Record actual command, SHA, logs and exit status. For performance use separate non-audit benchmark; fixture timings are QA-inclusive. If native errors appear, fix in same branch, write separate handoff. Do not claim feature verified before real Metal PASS.
