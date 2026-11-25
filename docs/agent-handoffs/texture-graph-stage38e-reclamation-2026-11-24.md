# Stage 38E — host-certified procedural image reclamation (partial)

Branch: `feature/aiv20261124_texture_graph_gpu_slots`

Plan: `2541efa1b9aff96b0fa25e53c82cbb85e5d92c11`
Implementation: `7dc33b04f413812abf9b3da8716655507f912da4`
Diagnostics: `313e1a91b78429556e86591a3f2ed8dfb562ae63`

Stage 38E: `reclaimGraphImages` and registry `collectTextureGraphImages` provide opt-in host-certified reclamation of explicitly marked procedural Model images. The caller must enumerate every external live/pinned material copy, queued render packet, LOD/attachment and other raw index/handle consumer (or detach it), join all CPU readers and exclude concurrent writers/image transactions. Registry adds all current snapshots for that Model; base materials and all six PBR and four shader graph image indices are always scanned. Unknown/incomplete roots, stale device lease, invalid indices, borrowed/shared/aliased images and retirement backpressure are refused or preserved conservatively. Detached images move into the existing ModelImageRollbackPool; no direct texture destruction or additional owner. Stable indices leave empty tombstones, never recycle or compact; 24 live images plus conservative host-pool pending retirement admission, 4096 historical graph slots per Model. Old indices remain valid only for roots correctly enumerated; historical cap requires normal Model reload. Both CPU/GPU graph image creation mark provenance. MCP `materialTarget` and Studio expose live count, tombstones and host-wide pending retirement. This is the native reclamation primitive and diagnostics, NOT automatic GameView collection: enabling automatic collection requires verified enumeration/detachment of all external readers/copies at the runtime host boundary. Integrated appearance fixture extended, NOT RUN; static source checks passed, no native build or GPU test.

## Changed paths

- engine/render/Model.h
- engine/render/graph/TextureGraphMaterialBinding.h
- engine/render/graph/TextureGraphGpuTextureTransfer.h
- engine/render/graph/TextureGraphImageReclamation.h
- engine/view/AppearanceAssetRegistry.h
- engine/view/game/GameView.h
- engine/view/game/GameLook.cpp
- apps/editor/EditorPlayOps.cpp
- apps/studio-web/src/graphs/TextureGraphPanel.tsx
- tests/view/appearance_prepared.cpp

## Actual evidence

Read back exact committed sources at 313e1a91b78429556e86591a3f2ed8dfb562ae63; nine source assertions returned true: explicit complete root/readers gate, all material channels, existing retirement pool/no direct destroy, stable tombstone indices, CPU/GPU provenance, registry roots, pending retirement budget, fixture assertions, diagnostics wiring. These are static source checks only. Attempted local checkout access with `git ls-remote https://github.com/chromealex/AIVERSE.git HEAD`; failed: Could not resolve host: github.com. No full checkout, native compile, CTest, GPU, Studio/MCP or Mac quick gate performed. The integrated appearance fixture asserts rejection of incomplete roots/active readers, preservation of old snapshot and shader binding, deferred texture destruction, 19 detached of 24, backpressure until drain and new append indices after collection. These are authored assertions, NOT measured results.

## Next

Complete 38E/38F host integration: audit the actual GameView/native host roots, including copied material vectors, older snapshots and renderer packet/cache ownership. Do not set complete=true from registry enumeration alone. Call reclamation only once the host can prove that every remaining reference is supplied or detached, then run integrated tests including the existing view_appearance_prepared CTest, actual GPU retirement, native compile, Studio and Mac quick gate. Consider bounded slot recycling only after separate generational index proof; no silent index reuse. No merge main until integration gates pass.
