# AIVERSE GI — Stage 59: defensive Metal/Vulkan BVH packet validation (2026-11-20)

## Repository, branch, changes and actual verification

- Repository: `chromealex/AIVERSE`
- Working branch: `feature/gi-lighting-portable-backends-20261119`, draft PR #47 into `feature/hybrid-dynamic-gi`, **not** main (which is far diverged).
- `24be23ae1e4e5a8d137d57679dbe83a37efb4dda` — add `engine/render/GiGpuBvhValidator.h`, preflight of root-reachable GPU TLAS/BLAS references before any compute input upload.
- `71ea8f3b729196de782d4ef15038de444416985b` — `engine/render/GiGpuSmallScene.h` rejects malformed, zero, duplicate, or stale geometry revisions without changing CPU/GPU storage state or encoded compute pointers. Normal single-scene, chunk-cached and paged/compacted packet uploads use this common gate.
- `6733c7213844845d58eebee0f954eee0481d3474` — add `tests/render/gi_gpu_bvh_validation.cpp` with valid ordinary and paged BVHs, cycles/out-of-bounds children/triangles, NaN data, negative albedo, 65-level traversal depth, and FakeDevice preservation of prior geometry/allocator identity on rejected updates.
- `1d67821dcaf30dd4b8c0e6a428e60d65bbc39452` — register `aiverse_gi_gpu_bvh_validation_test` / CTest `gi_gpu_bvh_validation` in CMake.
- `9dc48d40240dcb38d9451e99ff444bb32664bb41` — explicitly include `<algorithm>` in the new validator header.
- `eec113052b696a9d68a6273a449bca3fb497fcc5` — additionally validate each reachable child's bounding box is contained in the parent's box, and each referenced triangle vertex lies within its leaf box. Reject malformed spatial bounds even when pointer indices happen to be legal, preventing false skipped occluders/light leaks.
- `cf4ecffc963f13204e63bd50d3ae92d4935c5261` — source test cases for legal pointer but invalid parent/leaf spatial bounds.

**Actually performed:** GitHub source inspection/commits and CMake test registration only. **NOT RUN:** native Clang/GCC/CMake compilation, CTest, Metal MSL shaders, Vulkan SPIR-V/validation layers, GPU ray/pixel parity, GLES3/WebGL2 devices or performance profiling. Do not claim a PASS based on authored source assertions.

## Invariant and architecture

`GiGpuBvh` uses a root at node[0], 48-byte GPU nodes, 64-byte triangles, leaves containing at most `GiGpuBvh::kLeafSize` (=4) indices and internal children of two or one (right=`UINT32_MAX` means a TLAS-to-BLAS reference). Paged suballocators intentionally have unreachable retired node/triangle slots and a reserved TLAS node prefix. The validator visits only root-*reachable* nodes and does not reject holes.

For any nonempty root it rejects empty/missing triangle data, physical over-capacity, invalid/nonfinite node min/max, child indices beyond node storage, self-cycles and ancestor cycles, excessive traversal depth (64-slot Metal/Vulkan shader stack), invalid leaf triangle ranges, nonfinite vertex/albedo data, negative/too-bright material inputs, child AABBs outside parent AABB and triangle vertices outside their leaf AABB. The parent containment uses a tiny relative FP tolerance to avoid rounding false positives. Empty BVH remains a valid zero-geometry scene with zero bounce.

`GiGpuSmallScene::setPackedGeometry` validates the candidate **before** any `updateBuffer` / allocation, and rejects non-monotonic source revisions directly (not only through the host controller). On failure, existing `geometryRevision`, `triangleCount`, buffer handles and previously accepted atlas owner remain unchanged. Stage56/57 logical BLAS page mapping remains transactional and is committed only once the backend accepts the validated packet.

This is a source-side safety check, not a replacement for Vulkan validation-layer testing, actual Metal shader compiler, GPU memory fences, precision parity or malicious-input sandboxing. Validation is O(reachable nodes + leaf triangles) per geometry publication. The 8192 GPU local triangle limit and 4096 reserved TLAS nodes still apply; large-world nearby overflow must fail closed or use explicit Stage53 CPU fallback.

## Next steps for agent

1. Build and run `gi_gpu_bvh_validation`, `gi_gpu_paged_bvh`, `gi_gpu_page_compaction`, `gi_gpu_dynamic_runtime`, and `gi_gpu_chunk_bvh_cache` CTests on native Clang/GCC with a concrete tested git SHA. Fix actual compile/runtime failures and add exact failure logs to a new handoff.
2. Validate real Metal/Vulkan shader jobs and pix/readback using malformed input rejection and one distant/near changed occluder case; ensure no OOB buffer reads, stale GI overlay or false light leaks.
3. Complete remaining current-main/TerraWeave/PHYSIS source integration, run the final full CMake/CTest acceptance plus GLES3/WebGL2 and GPU device parity. Do not merge the draft PR into main until evidence exists.
