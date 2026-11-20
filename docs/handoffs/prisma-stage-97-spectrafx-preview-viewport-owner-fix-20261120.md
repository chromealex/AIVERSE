# PRISMA Stage 97 acceptance — SpectraFX preview viewport owner integration build fix

Date: 2026-11-20
Repo: chromealex/AIVERSE
Branch: `feature/prisma-main-acceptance-stage96-20261120`
PR: https://github.com/chromealex/AIVERSE/pull/84 (Draft)
Previous: `docs/handoffs/prisma-stage-97-gl33-multiview-hardware-pass-20261120.md`
Previous source feature HEAD before this specific fix: `f7a52f7de08788c3433a0f3b264e7e3af805f912`

## User-reported actual Mac build results

After Stage97's Metal and GL33 focused multiview tests, the user invoked
`bash tools/prisma/acceptance.sh build/prisma-stage97` on their Mac Studio.
The complete Release build reached 97% before failing in TWO editor windows:

```text
apps/editor/windows/SpectraFxGpuAssetsWindow.cpp:473:
  error: too few arguments to function call, expected at least 4, have 3
  previewViewport_.ensure(*previewDevice_,int(width*dpi),int(height*dpi));

apps/editor/windows/SpectraFxGraphWindow.cpp:241:
  error: too few arguments to function call, expected at least 4, have 3
  previewViewport_.ensure(*device_,int(width*dpi),int(height*dpi));

apps/editor/Editor.h:91:
  void ensure(gfx::Device& dev, int pw, int ph,
              ViewportGpuRetirement& owner, const char* debugName="Viewport");
[prisma] FAIL build: build.log (exit 2)
[prisma] SKIP ctest_full: build failed; stale binaries rejected
PRISMA acceptance exit=1
```

The simultaneous `apps/editor/windows/SpectraGraphWindow.cpp` warnings for
unused `ed` parameter and unused `validId` function were non-fatal
diagnostics, NOT the reason for the exit code.

## Confirmed root cause and correct owner

`apps/editor/Editor.h` gives `Viewport::ensure` a mandatory fourth
argument: the frame-owning `ViewportGpuRetirement&`. The Editor owns
`viewportRetirement_` and exposes `ed.viewportRetirement()`. Its
`Editor::shutdown()` clears `windows_` before the retirement manager
is drained after a verified GPU idle. The implementation in
`Editor.cpp` stores this owner in each Viewport and uses it to defer
old color/depth targets, including when ImGui may still be referencing
the previous frame's textures. Supplying a dummy, temporary, or
window-owned retirement manager would violate that lifetime contract.

The SpectraFX asset preview and graph preview still used the older
three-argument call, despite their existing `drawPreview(Editor& ed,...)`
providing direct access to the proper owner.

## Bounded source changes — no new owners or RHI APIs

1. `2564e7cabd551fd2dba70e4d9c70475c26cc618f`:
   `apps/editor/windows/SpectraFxGpuAssetsWindow.cpp`:
   ```cpp
   previewViewport_.ensure(*previewDevice_,int(width*dpi),int(height*dpi),
                           ed.viewportRetirement(),"SpectraFX GPU Assets Preview");
   ```
2. `5224326639be103dd2edf8a2d49f344fd2eff848`:
   `apps/editor/windows/SpectraFxGraphWindow.cpp`:
   ```cpp
   previewViewport_.ensure(*device_,int(width*dpi),int(height*dpi),
                           ed.viewportRetirement(),"SpectraFX Graph Preview");
   ```

The human-readable debug names also distinguish previews in GPU-retirement
diagnostics. No change to ECS, PRISMA temporal rendering, SpectraFX
simulation, editor serialization, JSON, GPU fences, teardown order or
`Viewport::ensure` API. Existing preview window destructors still
release Viewport targets through their registered Editor owner.

## Validation performed vs pending

- Read actual changed files and matching `Editor.h` /
  `Editor.cpp` declarations/lifetime and `Editor::shutdown` order.
- GitHub acknowledged both source updates and subsequent readback
  confirmed **both** now supply the fourth owner reference and debug name.
- Source-contract checks: the fixed two calls match the mandatory
  parameter signature and the existing `ed.viewportRetirement()` accessor;
  no new retirement owner was created.
- **NOT RUN by assistant:** Objective-C++/C++ compile, Editor link,
  complete CTest, hardware Metal/GL screenshots or the new
  consolidated acceptance run.
- The real user-run acceptance with exit 1 remains a FAIL until
  the corrected source is actually pulled and built on Mac.
  The earlier focused Stage97 Metal and GL33 multiview fixtures
  passed on an older exact source; these unrelated Editor changes
  do not retroactively certify a new full run.

## Next Mac commands (preserve every unrelated worktree)

```bash
cd ~/Projects/AIVERSE-prisma-stage97
git status --short
git fetch origin
# Only on a clean detached worktree:
git switch --detach origin/feature/prisma-main-acceptance-stage96-20261120

# Focused compile/link of the previously failing target:
cmake --build build/prisma-stage97 --parallel 4 --target aiverse_editor

# If successful, restart the one consolidated acceptance gate:
bash tools/prisma/acceptance.sh build/prisma-stage97
echo "PRISMA acceptance exit=$?"
```

Retain `build/prisma-stage97/prisma-acceptance/summary.md`, `build.log`
and `ctest_full.log` for true failure/success classification. A further
compiler error must be fixed with another narrow commit plus handoff,
not papered over by skipping the Editor or full build. Long-running CTests
may legitimately exceed 900s; the runner accepts
`AIVERSE_PRISMA_CTEST_TIMEOUT` (default 1800s per test).

## Remaining gates

Full current-branch Release build/complete CTest, any new Editor
GPU-preview lifetime exercise, actual completed-fence destruction and
GPU residency stress, temporal per-pixel ghosting, Vulkan/GLES parity.
Keep PR #84 Draft, main unchanged until native acceptance and owner approval.
