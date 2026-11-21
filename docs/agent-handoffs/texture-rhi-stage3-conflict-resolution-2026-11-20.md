# AIVERSE Texture RHI — Stage 3 main conflict reconciliation

Date: 2026-11-20
Repository: `chromealex/AIVERSE`
Worktree branch: `integration/texture-rhi-main-stage3-20261120`
Pre-merge HEAD: `bbda0febbbc9cca26c735046c4cffe9270944c20`
Pinned merged `main` parent: `f9aed9a705982e2c6b77b29869e309995ce8b163`
Prior verified integration ancestor: `d1c2df7ffe7b25b6fc14a415a8b8d63bfe73d5c6`
PR: https://github.com/chromealex/AIVERSE/pull/86 (Draft)

## Input and integration state

The Mac Studio ran the Stage 3 merge script on a clean stage branch; `git merge --no-ff --no-commit` stopped on **nine conflicting files / fourteen content hunks**. The user supplied the exact Git index stages `:1` base, `:2` Texture RHI and `:3` pinned main, plus the current working-tree files with conflict markers. All nine files in this handoff are resolved versions of that uploaded local merge state. Other automatically merged files in the worktree **must be preserved**.

The application script checks branch, HEAD, MERGE_HEAD, all nine unmerged paths, SHA256 hashes of the original conflict-marked working-tree files, and the output file hashes. It backs up the originals under `build/stage3-merge-backups/`, stages the nine resolved files and this handoff, and never commits/pushes/updates `main`. It refuses to overwrite any local changes made since the conflict archive was collected.

## Resolutions, architecture and API decisions

1. **`engine/gfx/Gfx.h` (2 hunks)**: keep the Texture RHI per-command `GpuIcbVirtualMaterial`/`GpuIcbPbrMaterial` descriptors, ensure both close as complete C++ structs, and preserve main's `BufferInfo` and virtual `textureInfo(TextureHandle, TextureInfo&)` CPU metadata API. Preserve the opt-in texture views, samplers, storage texture capabilities, region updates, async readback and unchanged existing device ABI.
2. **`engine/gfx/gl/GfxGL.cpp` (4 hunks)**: retain GL33/GLES3 dimensional texture capability and view restrictions, GPU submission/completion GLsync fences and `waitForSubmittedGpuWork()`, texture async readback cleanup, retirement fence cleanup, `textureInfo()` and WebGL2 acceptance-only readback, multicolor resolve and related private fields. All GLsync categories remain distinct and are cleaned up on destruction.
3. **`engine/gfx/metal/GfxMetal.mm` (1 hunk)**: preserve `textureInfo()` from main and Texture RHI's parent-view destruction, binding clearing, and GPU texture handle teardown. Existing Metal writable `RGBA32F Texture2D` SpectraFX fix remains intact in the automatically combined code.
4. **`engine/gfx/vulkan/GfxVulkan.cpp` (2 hunks)**: preserve async texture readback staging resource cleanup and callback delivery and main's queue-ordered retirement fence teardown plus `waitForSubmittedGpuWork()`; these are distinct functions and both require their own closing brace.
5. **`engine/gfx/vulkan/VkDevice.h` (1 hunk)**: keep Texture RHI region uploads and main's `textureInfo()` declaration, matched to the existing auto-merged `VkResources.cpp` implementation.
6. **`engine/render/Model.cpp` (1 hunk)**: prefer dedicated reduced-size `virtualFallbackMaxDimension` textures, never borrowing full-size cache entries for VT fallbacks; retain the new speculative `borrowResident` / `publishSharedTextures` transactional residency policy, resource validity checks and exception-safe CPU memory release. Preserve material revision bump whenever binding changes.
7. **`engine/render/Renderer3D.cpp` (1 hunk)**: reset both VT feedback custom draws and per-scene motion reactive extension hooks in `beginScene()`.
8. **`engine/render/Renderer3D.h` (1 hunk)**: keep all VT feedback scheduler/pipelines/binders/resolver and use the new canonical `Camera3D cam_` from main; do not leave two camera fields.
9. **`tests/tests.cpp` (1 hunk)**: preserve opt-in flushed `[ RUN ]` and `[ DONE ]` crash-localization messages, normal output and substring filtering; retain main's new recording-RHI `waitForResourceIdle()` fake-device override outside the conflict hunk.

No new engine feature is invented; the work reconciles existing two-sided implementations, respecting production boundaries. Preserve the existing Metal Shader Graph, SpectraFX/PRISMA, GPU retirement, ECS, PHYSIS, TerraWeave and other main-side changes already auto-merged.

## Tests and checks **actually** performed on the resolution archive

- Parsed all nine files from the uploaded 3-way ZIP, examined all **14** Git conflict hunks and reconstructed the combined files.
- Validated none of the nine outputs contains Git conflict markers; preserved API anchors from both branches by explicit static assertions.
- `clang++ -std=c++20 -fsyntax-only` **PASS** for the resolved `engine/gfx/Gfx.h` using an isolated minimal stand-in for the missing `GpuTimingRouter.h` dependency. This is limited header grammar validation, **not** a real project compilation or ABI check.
- Constructed a synthetic Git repository with stage-2 snapshots and resolved file outputs; `git diff --cached --check` **PASS**, no introduced whitespace errors in the nine prepared files.
- `python3 -m py_compile apply_stage3.py` and `python3 apply_stage3.py --self-test` **PASS**; bundled hashes verified.
- **NOT RUN** on Mac: real Stage 3 CMake build, full CTest, native Metal GPU pixel assertions, Vulkan/GLES builds, WebGL2 browser tests, editor UX tests. Do not mark Stage 3 PASS until real Mac logs are available.

## Remaining validation and next agent step

1. Apply archive using `python3 apply_stage3.py` from the Stage 3 worktree. Review any staged whitespace diagnostics from *automatically merged* main files and correct them with the optional targeted utility; never blindly discard a side of a conflict.
2. Run `git diff --name-only --diff-filter=U`, `git diff --cached --check`, `python3 tools/skills/catalog.py check` and `python3 tools/skills/audit.py --strict`.
3. Configure/build full AIVERSE with `-DAIVERSE_BUILD_TESTS=ON`, run the CTest suite serially, and explicitly run all seven Texture RHI Metal GPU pixel tests plus SpectraFX Metal tests using Mac Studio.
4. Fix any actual C++/Metal/Vulkan incompatibilities in the local integration worktree. Update this handoff with changed files, exact source SHA, actual test counts and results, limitations and next step.
5. Only after acceptance, create a two-parent merge commit, verify the parents include pinned `main` `f9aed9a...`, then push **only** the Stage 3 integration branch. Keep PR #86 Draft until it targets the latest main and all required gates pass. Never push directly to main at this stage.
