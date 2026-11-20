# AIVERSE GI — Stage 60: Vulkan sequential compute storage-image memory barriers (2026-11-20)

## Exact commits, branch and actual verification

- Repository: `chromealex/AIVERSE`.
- Working branch: `feature/gi-lighting-portable-backends-20261119`, DRAFT PR #47 targeting `feature/hybrid-dynamic-gi`, NOT current divergent main.
- `b892598a97649b909aefd5e72498a7d4f1b32427` — `engine/gfx/vulkan/VkCompute.cpp` fixes sequential storage-image compute synchronization. When multiple dispatches operate on the same output within `beginCompute/endCompute`, issues a compute shader `VK_ACCESS_SHADER_WRITE_BIT→VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT` memory barrier before the next dispatch. When `setComputeTexture(0,newImage)` switches the target after an earlier dispatch, returns the OLD output from `GENERAL` to `SHADER_READ_ONLY_OPTIMAL` before binding the new image. The new image transitions from `SHADER_READ_ONLY_OPTIMAL` to `GENERAL` before dispatch and ends in sampled layout.
- `6dee57c7d14815e7870dc7c8d706e2e4cb32d97b` — `tests/render/vulkan_tests.cpp` extends the existing REAL offscreen Vulkan compute fixture: dispatch on atlas A (red), dispatch again on A (blue-ish `.25,.5,.75`), switch storage image to atlas B (`.75,.25,.5`), then sample A and B as fragment textures in two side-by-side viewports and assert different captured RGB pixels. Four storage buffers, one image descriptor, 80-byte push constants and 64-thread workgroups match the Vulkan GI backend contract.

## Bug / API contract

The Stage30 Vulkan compute RHI used one `computeImageWriting_` bool. It transitioned `SHADER_READ_ONLY_OPTIMAL→GENERAL` on the first dispatch only. A second dispatch on the same image had **no compute-to-compute memory dependency**; Vulkan command ordering alone is not a memory visibility guarantee for read-modify-write or overlapping GPU storage writes. The storage image stayed in GENERAL for multiple kernels, but callers could not safely rely on first-dispatch writes. A second `setComputeTexture` could also change the target without restoring the previous written image to its expected shader-read layout. The final `endCompute` transitioned only the most recently selected image, potentially leaving the previous image permanently GENERAL while fragment descriptors advertised SHADER_READ_ONLY_OPTIMAL.

The corrected Vulkan path inserts the necessary memory barrier on repeated same-image writes and explicitly returns the old target to shader-read layout on texture changes; the final target is returned by `endCompute`. Distinct descriptors are allocated per dispatch and remain fenced to their owning frame. This is generic Vulkan RHI behavior, not tied to an RTS/FPS genre; GLES3/WebGL2 continue to use CPU-probed GI and Metal's existing compute implementation remains unchanged.

## Verification and next actions

- **Performed:** GitHub source inspection/commits, source test extension registration in the existing `vulkan_backend` CTest.
- **NOT RUN:** native C++ build, `ctest -R vulkan_backend`, actual glslang SPIR-V compilation, Vulkan offscreen compute dispatch/pixel readback or validation layers, Metal GPU and GLES3 mobile tests, full AIVERSE acceptance. GitHub Actions were queued, and a Linux ARM64 job was briefly in-progress without accessible logs; do not infer success from this.
- Next agent must build `aiverse_vulkan_tests` on a Linux/Windows Vulkan SDK + lavapipe or real GPU, run with `VK_LAYER_KHRONOS_validation` and confirm both output colors and no storage-image layout hazard. Include compute→compute and image A→B cases in the tested SHA report. Fix real compilation/driver errors before any main merge.
- Verify other GI page/BVH test targets (stages 56–59) and all Metal/GLES variants only after remaining functional integration; continue writing a separate actual-test handoff with command, exit code, artifact/log evidence.
