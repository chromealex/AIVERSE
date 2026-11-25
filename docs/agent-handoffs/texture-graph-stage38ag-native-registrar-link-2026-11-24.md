# Stage 38AG — NativeRegistrar link dependency

Branch: `feature/aiv20261124_texture_graph_gpu_slots`
Base: `d48941045c6b029129f563952d5882993276b652`
Plan: `940813bf06e7de228c709ebe6cb9ef5aa631f125`
Implementation: `79043f35c14ee12e63973e903dbdebf0e8ab8fec`

Stage 38AG: user Mac rerun advanced to aiverse_camera_client_adapter_tests link, then failed with undefined NativeRegistrar constructor from generated NativeRegistry.cpp. Native module configuration attaches that source to direct runtime users; Native.cpp is compiled into aiverse_game. Explicitly link the existing aiverse_game owner for camera_client_adapter_tests. No production source, fixture coverage or standalone snapshot dependency changed. 7/7 exact-source checks passed at 79043f35c14ee12e63973e903dbdebf0e8ab8fec; corrected Mac link and full CTest remain NOT RUN.

## Evidence

User log: Mac Release x86_64 link of camera_client_adapter_tests exited 2 after compiling runtime and NativeRegistry.cpp. Missing symbol is aiverse::game::NativeRegistrar::NativeRegistrar(std::vector<NativeDefinition>). CMake engine/game/native/Build.cmake adds Native.cpp to aiverse_game and generated registry to native_modules, automatically linked to runtime executables. The existing camera adapter target lacked aiverse_game. The change is restricted to its target link line and explanatory comment in CMakeLists.txt. No global runtime dependencies changed. Verified exact source at implementation commit; no local build execution.

## Next

Fetch and switch to latest exact HEAD, rerun the existing 31-test acceptance runner with the same Mac build directory. CMake regenerates on the changed CMakeLists.txt. Capture actual link/test results. Main remains blocked by required integration gates.
