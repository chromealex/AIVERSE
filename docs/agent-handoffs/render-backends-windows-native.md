# Native Windows rendering acceptance

Tested commit `e328e4cfceaed102f43460afcdf6813e82f98c7a`, Windows Server 2022 x64, MSVC Release. All six CTests passed in 0.54 s on the hosted Windows runner. [Successful workflow and JUnit artifact](https://github.com/chromealex/AIVERSE/actions/runs/45608568734).

The first run at `637b9fac335740b35f9286af0ee448b041324d7c` passed native D3D9 HAL and all three D3D11 WARP cases. Two D3D9 Device cases failed at shader/pipeline creation. The fix compiles legacy HLSL without the strict-language flag and checks shader creation before pipeline creation in the executable and model capture consumer. [First run](https://github.com/chromealex/AIVERSE/actions/runs/45608063639).

The accepted suite exercised native offscreen drawing, indexed textured drawing through gfx::Device and presentation through Win32 HWND for both backends. RHI cases reused GPU handles across two frames, updated the sampled texture, resized from 17 × 15 to 23 × 19, and verified background and center pixels. Native calls and original CTest output are in tests/render/direct3d_device.cpp, tests/render/direct3d_rhi.cpp and docs/evidence/direct3d-windows-native.log.txt. D3D11 hardware mode remains outside this WARP gate.

On macOS the rebuilt CPU device and textured OBJ capture tests passed. A separate 512 × 512 arch.obj capture using aiverse_cpu_capture completed in 1.43 seconds elapsed, 0.19 seconds user CPU, 15,290,368 bytes maximum resident set (/usr/bin/time -l, single native process including loading and PNG writing). See docs/evidence/cpu-renderer-win-ci-regression.log.txt and docs/evidence/cpu-renderer/arch.png.

Remaining release work includes publication of representative images and Direct3D performance measurements on a representative workload. No main integration or release publication is part of this stage.
