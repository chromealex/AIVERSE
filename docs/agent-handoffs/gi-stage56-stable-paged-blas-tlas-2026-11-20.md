# AIVERSE GI — Stage 56: stable logical GPU BLAS pages and transactional TLAS (2026-11-20)

## Branch, exact commits and actual status

- Repository: `chromealex/AIVERSE`.
- Working branch: `feature/gi-lighting-portable-backends-20261119`; PR #47 is DRAFT into `feature/hybrid-dynamic-gi`, **NOT current main**.
- `7285116c62874af86fbbf3d050c0994b2e8966dc` — new `engine/render/GiGpuPagedBvhArena.h`. Explicit stable logical BLAS node/triangle page spans, persistent absolute GPU-address offsets within existing node and triangle SSBOs, separate reserved TLAS at node 0, deterministic top-level builds, bounded best-fit free spans/coalescing, transactional plan/commit, immutable source ownership, non-truncating resource caps.
- `6c45ab9b4ec2e7eacbdcd934b2e516b6e56449b4` — `engine/render/GiGpuDynamicRuntime.h`: normal Metal/Vulkan geometry publication now `chunkCache_.refresh(snapshot)` → `pages_.plan(snapshot,chunkCache_)` → `queue_.setPackedGeometry(gpu_,plan.packet,revision)` → **only on success** `pages_.commit(plan)`. The GPU queue still refuses geometry mutation with any in-flight compute ticket. Added `gpuPages()` and `lastGpuPageStats()`, and clears the arena on shutdown.
- `b2e49d434be7117b5b7b403aba7cb84b637a3d13` — new `tests/render/gi_gpu_paged_bvh.cpp`: CPU ray/occlusion parity against immutable scene, unchanged page offsets, transactional failed/uncommitted plan, changed early-chunk growth without shifting later pages, page retirement and reuse, and excessive source budget rejection.
- `73fef787dae51506afde40fea7f90b1b269a073a` — CMake/CTest `gi_gpu_paged_bvh`.
- `6ad66f0b974c6cf4c8dc74198f3a3b926789561b` — `tests/render/gi_gpu_dynamic_runtime.cpp`: confirms the REAL runtime publisher uses logical pages and retires them when an empty scene revision is safely accepted.

**DONE:** connected GitHub source inspection, code commits and test registration.
**NOT RUN:** native CMake/C++ compiler, CTest, Metal MSL shader compile, Vulkan GLSL/SPIR-V and validation layers, GLES3 shader/driver, GPU pixel/ray parity, perf or full suite. FakeDevice and CPU tests are **authored** but not executed. No PASS may be claimed.

## Architecture / ABI invariants

The old Stage54 `GiGpuChunkBvhCache::pack()` repacked TLAS+BLAS in chunk order on every geometry revision; edits to an early chunk shifted every subsequent absolute BLAS address and forced potentially huge storage-buffer updates. Stage56 adds a **logical, per-chunk GPU memory page allocator**, still backed by the existing two flat storage buffers and unchanged 64B triangle / 48B node structs. It reserves a root-index-0 TLAS region of 4096 node slots (supports <=2048 resident chunks). Individual chunk BLAS nodes/triangles are suballocated after/beside this reserve with independently stable offsets. Unchanged immutable geometry pointers retain their page and addresses. Rewritten/relocated pages patch their leaf `meta[2]` triangle indices and child `meta[0/1]` absolute node indices. A root-to-leaf TLAS references each page's stable BLAS root; the GPU shaders are unchanged and see ordinary absolute index pointers.

Two separate source-critical safety steps:
1. **Completed compute fence / lifetime:** `GiGpuDynamicRuntime::setGeometry*` forbids an update while `GiGpuTicketQueue` has any in-flight GPU batch, and that queue ACKs only after genuine Vulkan completed-frame fence/Metal callback. Only after that can deleted BLAS spans be reused; the current output remains an immutable page plan until RHI acceptance. Renderer atlas lifetimes remain under Stage55 borrowed-owner guard.
2. **Transactional publication:** `GiGpuPagedBvhArena::plan` copies the CPU layout and builds a candidate without touching the committed one. If `GiGpuSmallScene::setPackedGeometry` rejects, only the candidate is discarded; the last successful chunk mapping remains stable. If it succeeds, `arena.commit` changes the authoritative logical GPU page map. The source `GiWorldPresentationSync` and controller keep old/new dirty events if publication fails.

The shader's `Uniforms::triangles` and `Uniforms::nodes` count the **physical high-water** in the two storage streams. Freed/interior gaps remain allocated but unreachable: the new TLAS points only to live BLAS pages, so stale GPU bytes cannot be traversed. An empty scene emits an EMPTY BVH packet (not a self-referential TLAS node 0). Bounds/probe reach and missing/over-budget geometry remain guarded by Stage48 (8192 triangle hard limit) and Stage53 explicit CPU fallback. Stage54 `GiGpuSmallScene` grow-only GPU buffer capacity and sparse upload planner automatically send only changed TLAS and page spans when possible.

## Limitations / what is not yet implemented

- This is stable **suballocated logical BLAS pages inside two SSBO streams**, NOT independent `VkBuffer`/`MTLBuffer` per chunk, Vulkan buffer-device-address ray tracing, descriptor arrays, or unlimited GPU virtual memory. CPU produces a packed physical stream vector on each publication so CPU CPU-memory copies remain; GPU changed-range uploads are reduced, not completely eliminated.
- TLAS reserves 4096 nodes, so small scenes incur a ~192 KiB node prefix overhead. Max resident chunks 2048, max GPU triangle physical addresses 8192 and node physical addresses 20480. Fragmentation/high-water pressure can reject an otherwise logical-bounded scene: add fence-safe compaction/rebase and per-page LOD/memory telemetry in the next stage, without silent occluder clipping.
- CPU reference parity assertions and GPU pages must be actually compiled before trusting GLSL/MSL absolute index conventions. No runtime GPU performance evidence. GLES3 continues with the full CPU GI fallback and is unaffected by this GPU paging design.
- Do not merge into `main` while parallel TerraWeave, PHYSIS, NEREID, texture RHI and renderer work remains divergent.

## Next precise action

1. Build and run `aiverse_gi_gpu_paged_bvh_test`, `aiverse_gi_gpu_dynamic_runtime_test`, `aiverse_gi_gpu_chunk_bvh_cache_test`, `aiverse_gi_gpu_sparse_patch_test`; record command, exit code, tested SHA, PASS/FAIL and failure fixes. Do NOT substitute authored tests for executed ones.
2. Add compact/defragment fallback if page-address capacity exhausts despite smaller live geometry, only after real GPU ticket-fence completion; preserve transactional plan/commit and never silently discard relevant collision.
3. End-to-end Metal/Vulkan compute GI parity for scenes adding, removing and relocating chunks while a frame is queued; enable Vulkan validation and read back atlas pixels. Full GLES3/Android scene parity remains separate.
4. Add real GPU BLAS page buffer-address/descriptor-residency infrastructure if true independent buffers are required, keeping shader and RHI per-backend compatibility; then reconcile current main and perform the requested single broad native acceptance run.
