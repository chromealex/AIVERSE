# Direct3D native surfaces

Commit 01ad1eaa6b5d633945adf8959f4faacb3158368b. The implementation in engine/gfx/d3d/Direct3D.{h,cpp} owns native D3D11 or D3D9 devices, color surfaces and CPU staging surfaces through COM ownership. D3D9 optionally owns a hidden focus window. Native clients can issue draw calls between begin and readRgba. tests/render/direct3d_device.cpp draws a red triangle into a blue background twice, then checks center and corner pixels. CMake registers independent WARP and D3D9 HAL cases on Windows.

Cross-build on macOS succeeded. From the feature worktree:

```sh
export TMPDIR=/Volumes/KINGSTON/AIVERSE-builds/cpu-d3d-render-20261125/tmp
x86_64-w64-mingw32-g++ -std=c++20 -Wall -Wextra -Werror -I. engine/gfx/d3d/Direct3D.cpp tests/render/direct3d_device.cpp -static -ld3dcompiler -ld3d11 -ld3d9 -luser32 -o "$TMPDIR/../direct3d_device.exe"
x86_64-w64-mingw32-objdump -p "$TMPDIR/../direct3d_device.exe"
```

Import table includes d3d11.dll, d3d9.dll, D3DCOMPILER_47.dll, and USER32.dll. Earlier build diagnosed NOMINMAX redefinition, fixed by guarding its declaration. Native Windows execution and pixel results are pending. Run aiverse_direct3d_device_tests 11 and 9 on Windows with WARP and a D3D9 HAL available respectively. Remaining integration includes gfx::Device, shaders, window presentation and device loss.
