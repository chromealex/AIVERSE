# AIVERSE GI — Stage 51: Vulkan FP16 format admission + executable compute readback (2026-11-20)

## Branch and exact commits

Repo `chromealex/AIVERSE`; branch `feature/gi-lighting-portable-backends-20261119`; PR #47 remains DRAFT and targets `feature/hybrid-dynamic-gi`, NOT the diverged current `main`.

- `aa2425d028b13a78750ebb8a53da69d556e631dc` — `engine/gfx/vulkan/VkResources.cpp`: Vulkan `Device::supports(PixelFormat)` tests the sampled-image capability only, not sampled+color-attachment for *all* RGBA16F uses. Actual `createTexture(d.renderTarget)` now independently requires `VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT`. Storage-image allocation still explicitly requires `VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT` and an RGBA16F sampled+storage compute-capable queue. This prevents a valid sampled+storage compute GI atlas from falling back to CPU simply because the GPU cannot use it as an HDR color framebuffer.
- `b3ae9b33db55a43bf99ab9a8d93dccdb5824ba4e` — `tests/render/vulkan_tests.cpp` existing HDR framebuffer fixture no longer assumes every sampled RGBA16F format is color-renderable: it uses the RT handle only when actual creation succeeded, otherwise reports the missing independent cap.
- `18c321f1be2a1f60f9f141f5740ce414985616a4` — `vulkan_tests.cpp`: new offscreen *actual GPU-executable* compute→RGBA16F storage atlas→shader readback fixture. It matches the GI compute descriptor ABI (four SSBOs, one storage image, 80-byte push constants, 64 threads) and checks an 8×8 atlas filled by Vulkan compute for approximate pixel RGB (64,128,191) through a regular fragment sampling pass. Runs only if Vulkan offscreen device reports compute/storage capability. This is a test **source**: the GPU pass has not been executed here.
- `f518ba56758cb72b055be595e1a16838400fb299` — `vulkan_tests.cpp` calls actual `vk::compileComputeProgram(gi_gpu::kVulkanSmallScene,` ...) in `translation()`, so the full real BVH first-bounce GLSL 450 is compiled to SPIR-V with glslang even on a machine without a Vulkan driver. This is not a substring test; future CTest results can prove or disprove compiler validity.

## ABI / resource lifecycle

`GiGpuSmallScene` owns RGBA16F sampled+storage atlas and four storage-buffer inputs. Its shader descriptor set 0 binding 0..3 SSBO, binding 4 `rgba16f image2D` and 80-byte sun/sky push constants are enforced by the Vulkan compute RHI; original Metal compute remains separately supported. Vulkan uses the same physical graphics+compute queue, waits for actual completed VkFence serial before accepting queued GI probe output, and transitions the atlas `SHADER_READ_ONLY_OPTIMAL→GENERAL→SHADER_READ_ONLY_OPTIMAL` around compute. The added GPU fixture tests resource creation, shader compilation, descriptor encoding, dispatch, that transition and immediate fragment sampling in one Vulkan frame when executed.

`supports(PixelFormat::RGBA16F)` now means *sampleable*, not `COLOR_ATTACHMENT`. All Vulkan `createTexture` RT use must check for a valid returned handle (as newly corrected in the existing HDR test); a strict FP16-sample-only format is allowed for GI but must fail color render-target allocation cleanly. GLES3 uses CPU probe ray tracing + sampled RGBA32F nearest atlas with no compute requirement. Metal's compute and texture-format policy is unchanged.

## Actual verification (critical)

- GitHub files inspected and source commits recorded.
- GitHub Actions has appeared **queued** on prior GI-branch commits, but no completed PASS/FAIL logs are established for this stage.
- **NOT RUN:** native CMake/Clang/GCC, `aiverse_vulkan_tests`, actual glslang compilation, Vulkan offscreen GPU/validation-layer shader execution, any Metal/GLES3 GPU capture, full CTest, mobile performance.
- The new code and assertions are intended to execute on CI/lavapipe and physical Vulkan devices once a runner picks up the branch; do NOT label them passed merely because test cases exist. Driver-free Vulkan backend test retains its existing skip behavior for GPU paths, but the real GI shader glslang compile is always attempted in `translation()`.

## Immediate next actions

1. On Linux/Windows with Vulkan SDK or Mesa lavapipe, configure `-DAIVERSE_VULKAN=ON -DAIVERSE_BUILD_TESTS=ON`, build `aiverse_vulkan_tests`, run `ctest --test-dir <build> --output-on-failure -R '^vulkan_backend$'`. Read glslang logs first and fix any actual SPIR-V kernel syntax/ABI issues. Run with `VK_LAYER_KHRONOS_validation` and inspect validation messages, actual submitted VkFence completion and offscreen RGBA16F atlas pixels.
2. Run all GI source/CPU CTests, verify Metal compute MSL and Android/WebGL2 fallback, including Stage47 strict16 terrain shader pixels. Record exact tested SHA + PASS/FAIL in a NEW handoff, not this source-only document.
3. Complete native TerraWeave/PHYSIS publisher wiring, robust near-scene overflow handling/GPU paging, clipmap large-world quality, then full acceptance and careful main reconciliation before PR promotion.
