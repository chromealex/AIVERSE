# AIVERSE Lighting main integration — Stage 1: isolated contracts on current main (2026-11-20)

## Source and branch provenance

- Repo: `chromealex/AIVERSE`; new **isolated integration branch** `integration/lighting-main-stage1-20261120`.
- Exact immutable base: `main` SHA `f9aed9a705982e2c6b77b29869e309995ce8b163`. No current-main files except additive CMake test registration were replaced; active renderer, shader and RHI code in main is untouched.
- Luma source branch examined: `feature/lumaweave-massive-lighting` at `7943124126796ea03a1d10a70a1d800a696a9669`.
- GI source branch examined: `feature/gi-lighting-portable-backends-20261119` at `770c7ac1d96ab752cb68bebdf34fbb26896cd335`.
- Code commit `9a7c927e6e79ac272241cab310d79904698152d1`: selected independent header-only contracts and test source imported without bulk-merging either heavily-diverged feature branch.
- Test-registration commit `255ee5ddadc43382395017e1e28a4884f17abce6`: append a guarded standalone CTest loop to current main `CMakeLists.txt`. No unrelated CMake or feature tests rewritten.

## Integrated files and functionality

- `engine/render/LumaWeaveLights.h`: genre-agnostic presentation-only stable-key light source registry; atomic publication and change edits, point/spot validation and explicit `contributesToGi` flag.
- `engine/render/LumaWeaveClusters.h`: conservative world-grid source candidate index with overflow full-scan policy, CSR export and generation independent of stable source revision (camera follow correctness).
- `engine/render/LumaWeaveGpuPacket.h`: opt-in packed source/CSR snapshot with fixed 80-byte light, 64-byte grid uniform and 16-byte cluster ABI. **No GPU upload/lighting driver integrated yet.**
- `engine/render/LumaWeaveTextureRetirement.h`: bounded 48-handle deferred texture release and graphics-use gate from experimental Luma Stages26–28. The newer main `Device` exposes real GPU fence APIs; this helper is not yet wired into current main's shared `GpuResourceRetirement` or any scene render pass.
- `engine/render/GiLightingComposition.h`: one selected static GI base owner (hemisphere/lightmap/probes/grid), optional dynamic+screen bounce and validated AO; independently owned direct Luma energy is NOT double-added.
- `engine/render/GiPresentationPolicy.h` and `GiTextureBudget.h`: pure policy for CPU-GI into LDR GLES3 and fragment texture budget admission; **not a GLES shader implementation**.
- CPU-only regression sources `tests/render/lumaweave_clusters.cpp`, `tests/render/lumaweave_gpu_packet.cpp`, `tests/render/gi_lighting_composition.cpp`, and new `tests/render/lighting_gles_admission.cpp`. Four CTest names are `lighting_lumaweave_clusters`, `lighting_lumaweave_gpu_packet`, `lighting_gi_lighting_composition`, `lighting_lighting_gles_admission`, all labeled `Lighting`.
- The old feature's runtime `LumaWeaveNativeDriver`, shaders, `GiLightingController`, BVH/clipmaps, Metal producer and modified `Renderer3D` are **NOT** ported. They remain on separate source branches; do not claim this stage produces visible light or enables GI.

## Platform and lifecycle boundaries

- All imported contracts are opt-in, simulation/ECS authority unchanged; no save/network format or game genre dependency.
- Existing current-main `gfx::Device::gpuSubmissionFence` covers current GPU frame; `gpuCompletedFence` proves contiguous completion only. On future runtime integration prefer current-main `GpuResourceRetirement`, not a duplicate generic owner. A CPU frame index or compute-only callback must never authorize destroying sampled HDR textures.
- Unknown token stays quarantined and requires explicit full GPU idle before destruction. The retirement helper included here exists to preserve regressions/ABI expectations, not to replace main's shared retirement subsystem.
- GLES LDR policy checks only boolean admission. No actual Vulkan, Metal or GLES GPU shader has been compiled or executed by this port.

## Actual verification

- **PERFORMED:** connected-GitHub source inspection, main/feature header dependency review, creation of isolated branch from exact main SHA, successful Git tree/commit/CMake update acknowledgements. Two branches remain distinct and intact.
- **NOT RUN:** native CMake configure, Clang/GCC/AppleClang compilation, the four CTests, full project CI, Metal shader compile, Vulkan lavapipe/validation, GLES/WebGL device test, screenshots, profiling and runtime renderer integration.
- No source commits imply tests passed. CI status must be checked on the new exact HEAD before any production claim.

## Next exact action

1. Compile and execute the four isolated CTests on a C++20 native toolchain against this integration branch; fix any real compilation failures, separately documenting the exact tested SHA and outputs.
2. Build the next bounded port of shared GI BVH/scene geometry and Luma direct CPU reference, with tests, preserving `main` `Renderer3D` and `gfx` APIs. Add per-stage handoff after code.
3. Introduce explicit modern `GpuResourceRetirement` adaptation and one direct-light owner into current-main rendering, then stage GI compute and mobile CPU fallback. Do not claim Metal/Vulkan/GLES parity until real shader/GPU tests.
4. Reconcile with any newer main HEAD using lease/merge, not overwrite or force-push. Keep `main` unchanged until full acceptance.
