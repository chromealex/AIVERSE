# PRISMA Stage 66 — macOS all-target build at 96%: Modeling viewport owner

Date: 2026-11-19
Repo: chromealex/AIVERSE
Branch: feature/prisma-postfx
Draft PR: https://github.com/chromealex/AIVERSE/pull/26
Base: feature/universal-entity-inspector-fps
Previous handoff: docs/handoffs/prisma-stage-66-fix-client-lobby-config.md
Main unchanged; no merge without explicit permission.

## Actual local Mac failure received from the user

Worktree:
`~/Projects/AIVERSE-prisma-validation-20261119-222509`
build: `build/prisma-final`, full CMake Release with tests/editor/HDR.
After fixing the VFX, RtsRoadView/ShoreView and Lobby
compiler blockers, the all-target build reached 96%.

```text
apps/editor/windows/ModelingWindow.cpp:306:113: error:
too few arguments to function call, expected at least 4, have 3
viewport_.ensure(*device_, std::clamp(int(avail.x), 64, 1600),
                 std::clamp(int(height), 64, 1000));
apps/editor/Editor.h:83:10: note: 'ensure' declared here:
void ensure(gfx::Device& dev, int pw, int ph,
            ViewportGpuRetirement& owner,
            const char* debugName = "Viewport");
make[2]: *** [CMakeFiles/aiverse_editor.dir/.../ModelingWindow.cpp.o] Error 1
make: *** [all] Error 2
Exit code: 2
```

One additional non-blocking native warning:
```text
engine/render/Prisma.cpp:781:35:
warning: lambda capture 'maximum' is not required to be captured
[-Wunused-lambda-capture]
```

Full CTest and HDR GPU tests had NOT yet started because
the native all-target build failed.

## Root cause, ownership and source changes

The editor owns a GPU-fenced `ViewportGpuRetirement`
instance; `Editor::viewportRetirement()` exposes it as
a mutable reference. `Viewport::ensure()` was recently
changed to require its owner explicitly so old ImGui color
targets and depth images are retired only behind backend
submission/completion fences.

`ModelingWindow.cpp` still invoked the older 3-argument
`Viewport::ensure`. Do not fix by making the owner optional,
constructing a local queue in the window, or immediately
destroying in-flight render targets: those would bypass the
current editor lifecycle.

1. Source commit
   `2e4021586ecb3628ac3e3829f8689a78452c0b57`:
   `apps/editor/windows/ModelingWindow.cpp`
   passes the fourth argument
   `ed.viewportRetirement()` and a fifth diagnostic label
   `"Modeling preview"`. The old width/height calculations
   and other model rendering logic are unchanged. The window
   destructor still calls `viewport_.release(*device_)`,
   which uses the same queue.
2. Source commit
   `ac49e52bdc3406e700c5265ab7e11221becf76ca`:
   `engine/render/Prisma.cpp` removes the unnecessary
   `[maximum]` capture in a GPU memory-saturating-add
   lambda by declaring `constexpr uint64_t maximum`
   in the lambda body, with exactly the same comparison
   `a > maximum-b ? maximum : a+b`. This fixes the
   one reported Prisma warning without changing
   memory/presentation behavior.

These are strictly compile/diagnostic fixes, not new PRISMA
GPU functionality, entity state changes or additional effects.

## Actually performed source checks

- Compared current `ModelingWindow.cpp`, `Editor.h`,
  `Editor.cpp` and `Prisma.cpp` on the connected
  GitHub feature branch.
- **12/12 source-structural checks PASSED**:
  4th owner argument, actual public Editor owner accessor,
  own lifetime, `Viewport::ensure` contract, GPU-fenced
  retirement of both color/depth, retained ModelingWindow
  release, no stale unnecessary lambda capture and identical
  saturating arithmetic.
- NO local C++ compiler or GPU execution was available
  through connected GitHub tools, so this is not a
  verified native build PASS.
- The user's most recent local full build failed
  (exit code 2 at 96%) before this source fix.
  Full CTest/Metal HDR/OpenGL HDR, screenshot
  capture, GPU resource fence and perf tests are NOT RUN.

## Continue existing Mac build without deleting caches

```bash
cd ~/Projects/AIVERSE-prisma-validation-20261119-222509
git status --short
git fetch origin
git switch --detach origin/feature/prisma-postfx
git log -1 --oneline
bash run-prisma-validation.sh
```

The switch is safe only if tracked files do not contain
conflicting local changes. Do NOT use `reset --hard`.
Keep `build/prisma-final` for incremental native rebuild,
then the scripted full CTest and native HDR/GL fixtures.
Send the new `prisma-validation-logs.zip` or the first
actual error if build/CTest/GPU fails again.

Next bounded task: finish the native all-target build,
then fix exact CTest errors and HDR fixture failures.
After each independent fix, commit a fresh agent handoff
including changes, actual tests vs NOT RUN and next steps.
