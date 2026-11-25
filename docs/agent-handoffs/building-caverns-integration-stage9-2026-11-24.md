# Building/Caverns integration — Stage 9 (2026-11-24)

## Commit and scope

- Code commit: `765bdcf59` (`CMakeLists.txt`).
- The PHYSICS=ON TerraWeave native micro compile target now links `aiverse_client_runtime`, like its PHYSICS=OFF companion. This supplies `WorldScene::setReplacedTerrainTiles` from `WorldScene.cpp`.

## Evidence

- Exact Stage 8 merged tree (`0f4394d81`) Building Mac gate: 8/9 targeted native tests passed; `studio_building_preview` failed because the captured Metal frame remained the clear color (`changed=0, nonflat=0`). Diagnostic replay showed 2354 vertices, 7 submitted draw calls, model bounds and camera finite, and no renderer/device error. Double-sided materials did not change the result. Temporary diagnostics were removed.
- Caverns Stage 59 Mac gate on `0f4394d81`: CMake configured with PHYSICS=ON and test inventory contained all six scenario names. The full build stopped at 49% on `aiverse_terraweave_native_micro_physics_compile_test`: undefined `WorldScene::setReplacedTerrainTiles`. Log: `/Volumes/KINGSTON/AIVERSE-builds/caverns-stage59/0f4394d81d798f28bfdf6b5e43991057a9e7636b/test-reports/20261124-195606-15063/build.log`.
- Stage 9 fix has not yet passed the complete Caverns gate; no scenario result is claimed.

## Next step

1. Re-run Caverns Stage 59 Mac gate on this commit and resolve any remaining build or scenario failures.
2. Diagnose and fix Building Metal preview blank capture; repeat the merged-tree Building gate.
3. Finish feature delivery evidence, then squash-integrate the verified branch into `main` per repository contract.
