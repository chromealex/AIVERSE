# AIVERSE GI — Stage 62: large-world GPU BVH AABB containment precision (2026-11-20)

## Repository, branch and exact implementation commits

- Repository: `chromealex/AIVERSE`.
- Isolation branch: `feature/gi-bvh-precision-stage62-20261120`, created from GI portable branch `feature/gi-lighting-portable-backends-20261119` at `3e364b79f681feb4d159af5135b54b4b9326d609`.
- This stage is **NOT** merged into GI portable or `main`. Do not force-merge: GI portable remains far diverged from `main`, and PR #47 is still DRAFT.
- **Source commit:** `b1d08ad436ef95bc0906157e1a2bd6ca16f76cad` — `engine/render/GiGpuBvhValidator.h`.
- **Regression-source commit:** `dde5c9342f9af7c026978eebd65f5776b9940b47` — `tests/render/gi_gpu_bvh_validation.cpp`.

## Actual correctness defect and change

The pre-upload CPU validator `giValidateGpuBvh` formerly accepted leaf vertices and child AABBs outside their enclosing node by `1e-5 * max(1, abs(absolute world position))`. At world X = 100,000,000 the allowed error was around **1,000 world units**, even for a 16-unit node. The Metal/Vulkan ray traversal still prunes rays against the advertised *small* bounds. A leaf with a triangle outside those bounds can therefore be accepted for GPU upload and spuriously missed by a valid ray, leaking light through a real occluder.

The validator now compares each AABB against bounds expanded by **at most one representable float32 ULP per endpoint** using `std::nextafter(bound, ±infinity)`. This maintains a small rounding tolerance appropriate to the shared float32 CPU/MSL/SPIR-V BVH ABI, independent of scene distance from world origin. It checks both parent/child boxes and triangle vertices without modifying the on-GPU ABI.

The existing `gi_gpu_bvh_validation` CTest source now constructs a high-coordinate leaf whose triangle sits at X=100000128 while its box ends at X=100000016. This is rejected under the new rule but was admitted by the old world-scaled margin. After expanding the node to cover X=100000128 the same packet validates. The tests remain Release-safe through `AIVERSE_TEST_CHECK`.

## Scope and compatibility

- Pure preflight hardening: no ECS gameplay/save/replay change; no `Renderer3D`, lighting composition, lightmaps, GI atlas, BVH topology, buffer layout, sampler, shader ABI, API signature or feature default changes.
- Applies identically to the shared Metal and Vulkan BVH packets, including paged/compacted sources. GLES3/WebGL2 retains CPU-probe GI and needs its independent acceptance checks.
- Existing valid source-built BVHs are expected to contain their vertices exactly. If a nonstandard importer relied on out-of-bounds nodes being accepted, it will now fail closed, which is intentional. Check actual terrain/foliage/world-origin source geometry if a previously accepted packet gets rejected.

## Verification — report only what actually happened

- **Done:** GitHub connector read the current GI source and tests; an isolated branch was created; both GitHub source update operations returned real commit SHAs listed above.
- **NOT RUN:** native Clang/GCC C++ compilation, `ctest -R '^gi_gpu_bvh_validation$'`, Vulkan lavapipe + validation layers, Metal GPU image/capture, GLES3 Android/WebGL2 hardware parity, all-tests suite or performance benchmarks. Neither a successful CTest nor GPU frame result is established by GitHub file updates.
- The previous portable branch CI run `44693379411` failed on unrelated Release ECS assertions; the fixes were committed there in Stage 61 but their retest was still queued when this stage started. Do not attribute old CI status to this new branch.

## Exact next action

1. Compile/test this branch on the native Mac or Linux worktree (do not conflate source review with executable validation):
   ```sh
   git fetch origin
   git switch feature/gi-bvh-precision-stage62-20261120
   cmake -S . -B build/gi-precision -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=OFF -DAIVERSE_BUILD_CLIENT=OFF
   cmake --build build/gi-precision --target aiverse_gi_gpu_bvh_validation_test -j8
   ctest --test-dir build/gi-precision --output-on-failure -R '^gi_gpu_bvh_validation$'
   ```
   Confirm the exact CMake target spelling in `CMakeLists.txt` before invoking it. If the target differs, use the registered equivalent.
2. Run the portable GI backend regressions (`gi_gpu_paged_bvh`, `gi_gpu_page_compaction`, `gi_gpu_dynamic_runtime`, `vulkan_backend`) and review if any real BVH publisher requires explicit bounds expansion, not a lax validator.
3. Finish outstanding direct-light LumaWeave backend integration, GI/main reconciliation, Metal/Vulkan/GLES native shader and image checks, and the owner's final consolidated acceptance before touching `main`.
4. After any further bounded code stage, create its own separate handoff with exact source SHA, actual run logs/status, risks and next step.
