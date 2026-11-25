# CPU device and geometry capture

Stage commit 2b95a59f3952e22043aec44de80fd2a6cacd3002 on codex/cpu-d3d-render-20261125, based on 54ca01d1b. Changed paths are recorded in docs/features/cpu-renderer.json. API and limitations are in docs/cpu-renderer.md.

Focused native Mac pixel checks passed with the following command, from the worktree with `TMPDIR` on KINGSTON:

```sh
clang++ -std=c++20 -Wall -Wextra -Werror -I. engine/gfx/cpu/GfxCpu.cpp tests/render/cpu_device.cpp -o "$TMPDIR/cpu_device"
"$TMPDIR/cpu_device"
```

Also compiled apps/modelview/cpu_capture.cpp, GfxCpu.cpp, Model.cpp and ModelBsp.cpp using clang++ x86_64, C++20, macOS 15.6 deployment and AIVERSE_RUNTIME_AUTOLOD=1. Linked aiverse_models, aiverse_ufbx, aiverse_engine, aiverse_meshoptimizer_lod and aiverse_zstd from targeted-build-guidance-final/all-tests on KINGSTON. Ran aiverse_cpu_capture examples/world/streamed/arch.obj docs/evidence/cpu-renderer/arch.png. Decoded all 512×512 pixels, found 70,800 non-background pixels and four distinct RGBA colours. PNG was losslessly recompressed. An earlier capture was blank because the OBJ loader omits optional contiguous vertex ranges. The command now computes the actual indexed range.

Pending: interactive presentation, sampling/scene shaders, other platforms, final manifest evidence and merged-tree quick gate. No claim of graphics parity.
