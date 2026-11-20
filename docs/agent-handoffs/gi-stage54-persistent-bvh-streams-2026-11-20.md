# AIVERSE GI — Stage 54: GPU BVH stream capacity reuse and per-chunk CPU BLAS integration (2026-11-20)

## Branch, exact commits and status

Repository `chromealex/AIVERSE`; branch `feature/gi-lighting-portable-backends-20261119`; draft PR #47 targets `feature/hybrid-dynamic-gi` and has NOT been merged to the divergent `main`.

Source commits:
- `91a03f7a71cfdd18f787a91fe2630e76016587aa`: new `engine/render/GiGpuSparsePatch.h`, pure size-bounded contiguous dirty-range planner and grow-only power-of-two GPU buffer capacity selection.
- `a1e8d262521125aeca96a1186b31e52a9c0d77fc`: `engine/render/GiGpuSmallScene.h`, reuse persistent Metal/Vulkan triangle and BVH node storage-buffer handles across updates when sizes fit existing capacities. Allocate exponentially only upon growth, stage all required allocations before committing handles, and use incremental changed-runs `updateBuffer` calls. If >16 ranges or >60% of elements changed, issue one complete update, never drop a changed element. Zero logical count suppresses old tail traversal. Accept update only after previous GPU completion fence via existing queue; reset capacities on shutdown.
- `dad0c581fa277b8934077889623a517e5b4255a3`: `engine/render/GiGpuDynamicRuntime.h`, wire the already-existing `GiGpuChunkBvhCache` into actual normal `setGeometry` and `setGeometryRegion` code paths. Its `refresh` reuses per-chunk CPU BLAS by immutable source geometry pointer; `pack` constructs deterministic bounded TLAS and BLAS packed buffers; queue only calls `setPackedGeometry` once previous GPU tickets are fence-ACKed. Cache cleared on shutdown. Fail-closed on packing errors; controller keeps prior accepted revision.
- `1b023c3e344affdc47e15b70ad46d9c1637d80bd`: new `tests/render/gi_gpu_sparse_patch.cpp`, header-only source test for capacities, no-op, append, shrink and fragmented/full uploads.
- `36c77953b110d31c8c5ea80a7026d878119d19ed`: extend `tests/render/gi_gpu_chunk_bvh_cache.cpp` to check unchanged packed geometry uploads no bytes, chunk add/replace while still within capacity keeps all storage buffer handles stable (FakeDevice handle allocation index unchanged), and uploaded byte count is never larger than current full packet.
- `1978d79c3a39026e0c67037f23844085bc72a913`: `CMakeLists.txt` adds CTest source target `gi_gpu_sparse_patch`.

### Actual verification

**Performed:** GitHub source inspection of existing Vulkan/Metal `BufferType::Storage` update path, code patches, file commit readback. **NOT RUN:** native C++ compilation, `gi_gpu_sparse_patch` CTest, GPU cache test, real Metal/Vulkan storage-buffer upload/compute shaders, GLES fallback hardware, memory/performance capture or full AIVERSE acceptance. Do not claim performance wins without paired GPU/CPU measurements or code correctness without the native compiler.

## Architecture and exact scope

- The existing `GiGpuChunkBvhCache` was previously a useful standalone CPU test helper but **not** active in the normal `GiGpuDynamicRuntime` publication path. This stage makes it the real publisher. `refresh(snapshot)` skips BLAS rebuilding on chunks with identical shared immutable source pointer. `pack()` emits the shader's existing flat 48-byte node / 64-byte triangle TLAS+BLAS ABI and is deterministic. `GiGpuSmallScene::setPackedGeometry()` uploads only changed ranges of that packet, using the original GPU descriptor set layout and shader ABI. Metal storage updates are blit-ordered; Vulkan immutable storage updates use explicit transfer-write→compute-read barriers.
- GPU allocations now have *capacity*, not a requirement that the active node/triangle count equal the allocation size. A shrinking or growing resident chunk set inside an existing power-of-two capacity does not allocate new storage buffers. The GPU uniforms still set exact logical `triangles` and `nodes` counts, so dead tail bytes are not read. If new capacity is needed, both candidate buffers are allocated before publishing new handles; a failed allocation cleans candidates and retains the old handles.
- `GiGpuSparsePatch` returns contiguous byte-range updates. Too many runs cause one whole update for efficiency; the fallback always uploads ALL active bytes and never silently truncates light blockers. Boundaries, duplicate-source revisions, world change safety and completed VkFence/Metal callback are still owned by `GiGpuTicketQueue` and `GiLightingController`, unchanged in contract.
- This is **persistent resident GPU triangle/node buffers with incremental CPU chunk BLAS**, NOT fully independent per-chunk GPU pages or shader-pointer indirection, and NOT a full Nanite-like streaming GI. The accepted packet is still repacked on each new geometry revision and uploaded by changed ranges; local scene capacity remains 8192 triangles. Future true per-chunk persistent pages require stable GPU BLAS pages/addresses, TLAS relocation/update, per-page fence-safe retire and more validation.

## Risks / next precise agent actions

1. Native compile `aiverse_gi_gpu_sparse_patch_test`, `aiverse_gi_gpu_chunk_bvh_cache_test`, `aiverse_gi_gpu_dynamic_runtime_test`, run CTest for these and fix any actual errors. Record tested SHA and FAIL/PASS logs. Ensure `GiGpuChunkBvhCache::pack` is numerically equivalent to `GiGpuBvh::build` CPU reference on moving foliage, including 64-element GPU stack limits and roots with one child.
2. Under real Metal and Vulkan GPU, test 100+ geometry revisions of changing chunk counts, track `GiGpuSmallScene::GeometryUploadStats` (bytes, regions, full) and backend resource allocations against the old code. Verify GPU-dispatched indirect radiance pixels and no use-after-free on repeated scene mutation.
3. Expand to permanent page-based BLAS/TLAS uploads or validated clipmap cells for near scenes exceeding 8192 **without** ever silently clipping relevant collision; retain Stage 53 opt-in CPU fallback for constrained devices.
4. Reconcile active main/TerraWeave/PHYSIS with conflict review and run all GI + full end-to-end acceptance once features are done; retain GLES3/WebGL2 CPU fallback and low-tier resource budget.
