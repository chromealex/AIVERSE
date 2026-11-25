# Quake III BSP import

Implementation commit 646035bfead5cf5bc38c287c15a6beb563d53c53 on codex/render-capabilities-20261125.

ModelBsp.cpp reads IBSP version 46 vertex data, shader names and planar/triangle soup surfaces. Model.h and Model.cpp expose the loader through loadModel. CMakeLists.txt includes the source under AIVERSE_LEGACY_MODELS and registers quake3_bsp. tools/models/ModelConvert.cpp classifies BSP as native. tests/render/quake3_bsp.cpp exercises public loadModel with a synthetic triangle and four malformed inputs. The feature manifest is docs/features/quake3-bsp.json and the roadmap is docs/devlog/systems.json.

Focused Mac x86_64 check passed. Rebuilt the loader, dispatcher and test translation units. Linked other objects from the targeted-build-guidance-final/all-tests cache on KINGSTON. Command from the worktree:

```sh
cache=/Volumes/KINGSTON/AIVERSE-builds/targeted-build-guidance-final/all-tests
export TMPDIR=/Volumes/KINGSTON/AIVERSE-builds/render-capabilities-20261125
clang++ -std=c++20 -arch x86_64 -mmacosx-version-min=15.6 -DAIVERSE_RUNTIME_AUTOLOD=1 -I. -Ithird_party/meshoptimizer/src tests/render/quake3_bsp.cpp engine/render/ModelBsp.cpp engine/render/Model.cpp -L"$cache" -laiverse_models -laiverse_ufbx -laiverse_engine -laiverse_meshoptimizer_lod -laiverse_zstd -o "$TMPDIR/quake3_bsp_test"
"$TMPDIR/quake3_bsp_test"
```

Pending: PVS traversal, curved surfaces, shader textures/lightmaps, real capture, tooling and merged-tree gate. The other five capabilities are queued in the roadmap.
