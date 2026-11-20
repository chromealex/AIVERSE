# AIVERSE GI — Stage 57: transactional compaction of fragmented GPU BLAS pages (2026-11-20)

## Branch, exact commits and actual verification

Repository: `chromealex/AIVERSE`. Branch: `feature/gi-lighting-portable-backends-20261119`; PR #47 remains DRAFT targeting `feature/hybrid-dynamic-gi`, NOT current divergent main.

- `055aa38c20db6b7ddd1a1bf035cde4dad20c3f1a` — `engine/render/GiGpuPagedBvhArena.h`: detect logical BLAS page-address exhaustion and construct ONE fresh compacted candidate from all still-resident chunks. The previous committed layout remains unchanged until the RHI accepts the replacement packet and `arena.commit` executes.
- `c109571694f18bc575a24e4d80d7650ecf83455d` — same header: explicit lower triangle/node address limits for deterministic compaction tests; production defaults remain `GiGpuBvh::kMaxTriangles=8192`, `kTlasCapacity=4096`, `kNodeLimit=20480`.
- `12df752d7ff869d8e347636a12ae35f521b2542c` — new `tests/render/gi_gpu_page_compaction.cpp`: grow one source chunk through 1,2,3,5,9,17,33 triangles with artificially constrained 64 physical triangle slots, trigger compaction rather than silently dropping a light blocker; test rejected too-small node capacity retains old page and address.
- `890372d9749cd920fbe444f5424e2a674483ac56` — `CMakeLists.txt`: `aiverse_gi_gpu_page_compaction_test` and CTest `gi_gpu_page_compaction`.

**Actually performed:** GitHub source reads and commits, file existence checks and CTest source registration. **NOT RUN:** Clang/GCC/CMake native build, CTest execution, Metal/Vulkan GLSL/MSL shader compilation or validation, GLES3 hardware test, GPU memory safety/performance, full AIVERSE suite. Do not report PASS from authored assertions.

## Architecture and safety contract

The Stage56 arena allocates stable per-chunk BLAS triangle and node spans inside two persistent GPU buffer streams; unchanged chunks retain their addresses while other chunks grow, shrink, or disappear. Each chunk gets power-of-two page capacity, and deleted spans may be reused only after the previous GPU compute ticket's genuine Metal callback or Vulkan completed-frame fence. With repeated changes, the high-water address can exhaust while live chunk data would fit into a fresh layout.

`GiGpuPagedBvhArena::plan(snapshot,cache)` first attempts normal stable-page incremental publication. Only `AddressSpaceExhausted` triggers an alternative candidate built from a clean `State`; malformed input and hard source-count limits remain errors. The clean plan gives every live BLAS new compact absolute addresses, rewrites internal BVH node/triangle references and a root-at-zero TLAS. The `Plan::stats.compacted` flag records this fallback. An uncommitted plan has no effect on the prior accepted layout, even after compaction. `GiGpuDynamicRuntime::publishPackedGeometry` invokes this after the queue reports no outstanding GI jobs, submits the entire candidate to `GiGpuSmallScene::setPackedGeometry`, then **only on success** moves the new layout into `pages_.commit`. `GiGpuSmallScene` uses its existing changed-range uploads, automatically selecting full upload when relocation alters many bytes.

If a fresh layout still cannot fit, publication fails explicitly, and `GiLightingController` blocks presentation of stale bounced light while `GiWorldPresentationSync` retains pending world edits for future retry/LOD. The host may opt into Stage53 full-scene CPU fallback after GPU fences, with a soft CPU probe budget. No relevant collision geometry is silently clipped.

This is **logical suballocation inside two Metal/Vulkan storage buffers**, not separate per-chunk VkBuffer/MTLBuffer memory, hardware acceleration structures, or unlimited GI streaming. The TLAS still reserves 4096 nodes and the local GPU triangle limit stays 8192. GLES3 uses CPU probe tracing and does not need GPU BLAS pages.

## Next exact action

1. Native compile and execute `gi_gpu_page_compaction`, `gi_gpu_paged_bvh`, `gi_gpu_dynamic_runtime`, `gi_gpu_sparse_patch` and `gi_gpu_chunk_bvh_cache` tests; report exact tested commit/CTest stdout and PASS/FAIL. Fix actual C++/runtime failures.
2. Verify page fragmentation, TLAS root traversal, reused page references, old/new source revision and driver resource safety on real Metal and Vulkan compute; enable Vulkan validation layers, Metal capture, and compare output pixels with CPU reference on live chunk edits.
3. Add telemetry and bounded CPU compaction budget for very large resident worlds. The current planner copies in-memory physical vectors when constructing candidate pages; it is not a zero-copy GPU virtual-memory system.
4. After remaining features, reconcile current main/TerraWeave/PHYSIS source conflicts and run the requested full native acceptance pass across Metal, Vulkan and GLES. Keep PR #47 DRAFT until real validation.

## Follow-up source regression (still unexecuted)

- `15c7eb3bdc202fbcc270ef35e4737f4d954feb5b`, `d1fbb1f925e18e24467a4a7d26aec444fba27f00`: extend `gi_gpu_page_compaction.cpp` with three independent resident chunk pages under a 16-triangle physical-address budget. Grow the first source to five triangles (8-slot page), second to three (4-slot page), third from one to three (4-slot page). Two released 1-slot spans coalesce into only a 2-slot hole, so the last allocation cannot fit its required 4-slot page without compaction. Test requires unchanged chunk #3 address until the new compacted layout is COMMITTED, then verifies all three wall rays and that the committed address actually relocates. This catches both premature page mutation and false compaction successes. **No native compiler or CTest run.**
