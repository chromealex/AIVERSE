# Stage 38Y — stock host root audit and replay preflight (partial)

Branch: `feature/aiv20261124_texture_graph_gpu_slots`
Base: `827dc1411f52bce98a9cf4e05d456536a0223ee3`
Plan: `8382b4285659c7f05f0a11329047faac8ce76aa0`
Implementation: `336e21002f48d3015f1ed832fc99269a827eee3e`

Stage 38Y (partial): Renderer3D exposes a non-mutating replay-detachment preflight for live capture/model/geometry callbacks; GameClient checks both main and inset before either detaches, retaining existing explicit external certificate and GPU retirement owner. Camera snapshot regression covers refusal and callback preservation. Stock GameView::modelOf/modelResources/applyGraphMaterial callbacks, Editor::view/renderer/device and native extension consumers remain raw-access escape boundaries; renderer preflight is not a proof that these references are gone. Editor picking uses ModelReadLease and owned material copies; LENS snapshots retain owned copies but Model/View and GPU lifetime remain host-owned. Do not install a default certificate or detach needed CPU replay. Added tools/texture-graph-stage38-acceptance.py: strict configured-build inventory, 24 CPU/native regression registrations plus selected Metal/Vulkan fixture (25 per run), strict timing flag, fail on missing or skipped tests. Eight exact-commit static source assertions and all 26 test/target registrations across both backend alternatives matched at 336e21002f48d3015f1ed832fc99269a827eee3e. Native compilation, runner execution, CTest, hardware, Studio/MCP and Mac gate NOT RUN.

## Concrete ownership audit

- GameClient owns its GameView, main and optional inset Renderer3D and LENS snapshot. The explicit collector checks all three root sources; the opt-in automatic collector checks them after the external certificate. Previous replay diagnostics cannot be discarded by default: a caller or capture may still consume them. Two-renderer preflight prevents partial detach on a known active callback; owner-thread/no-reentry requirement remains.
- GameView public modelOf and modelResources expose borrowed Model identity/storage; applyGraphMaterial and appearance pipeline validation pass Model/material data to native callbacks. Native consumers can retain raw image indices or handles beyond a local call; no global proof exists. NativeGraphImageRead remains the recommended synchronous generation-checked path.
- Editor exposes view(), renderer() and device() to windows. Stock frame sweep honors an explicit provider but does not independently certify all panels/preview owners; do not install an unconditional provider. Visual picking uses cache-generation check and acquireModelRead before dereference; packet material copies retain pins. Independently owned preview Models and hosted client require separate ownership proof.
- CameraModelSnapshot owns copied materials/pins including attachments, not the Model/View or GPU lifetime. Renderer root inspection rejects borrowed ModelItems, submission provenance, callbacks and raw GPU table references. A successful callback preflight alone cannot certify a later root scan.
- Default collection and recycling remain off. This stage is not a whole-tree audit or proof of absence of external native raw consumers; no format, RPC or GPU ownership changes.

## Changes and evidence

Implementation paths: engine/render/Renderer3D.h, engine/render/Renderer3D.cpp, apps/client/GameClient.cpp, tests/render/camera_model_snapshot.cpp, tools/texture-graph-stage38-acceptance.py. Read back exact implementation files and CMake registrations via GitHub. Eight static checks passed (declaration, callback guard, detach recheck, inset preflight order, geometry callback fixture, CTest inventory, strict timing and no-tests guard); all 26 CMake registration/target pairs matched including both GPU alternatives. No native build or test process ran; no CI success claim. Main comparison at base reported main behind the feature by 790 commits and no new main-side files, so no main merge performed.

## Consolidated Mac acceptance (NOT RUN)

Use a configured native build with AIVERSE_BUILD_TESTS=ON, tools/Studio enabled and AIVERSE_TEXTURE_GRAPH_GPU_TESTS=ON.

```sh
python3 tools/texture-graph-stage38-acceptance.py --build-dir /Volumes/KINGSTON/AIVERSE-builds/texture-graph-stage38 --backend metal
```

Vulkan: configure a native Vulkan build with AIVERSE_VULKAN=ON and the same GPU opt-in, then pass --backend vulkan and its build directory. The runner requires all registrations, builds exact targets and rejects skipped CTest output. Run aiverse_material_batches --unit separately from its configured executable location; record exact SHA, hardware/driver and logs. Live Client/Editor/Studio/MCP, strict feature gate and merged-tree Mac quick suite remain separate mandatory gates.

## Next

Stage 38Z: migrate stock raw model access call sites to scoped readers or explicit immutable material pins; inventory native extension callbacks and editor panel roots with concrete lifetime boundaries. Add a host-owned collection policy only where all replay/raw consumers are demonstrably closed; preserve opt-in for extensions. Run the consolidated native suite and fix failures before readiness or any main merge.
