# AIVERSE LENS Stage 10 — Studio ECS camera asset authoring (2026-11-19)

## Provenance and ownership
- Branch: `feature/universal-multicamera-render-targets`.
- Implementation commit: `0dbc1c92727b3978568f468617e40f0ff82585cb`.
- New `engine/sim/CameraSceneAsset.h`, `apps/editor/windows/StudioCamerasWindow.cpp`, `tests/render/camera_scene_asset.cpp`; changed `CMakeLists.txt`.
- No changes to main. Previous Stage 9 handoff: `docs/agent-handoffs/universal-multicamera-stage9-capture-2026-11-19.md`.

## Architecture / API
- Added explicit project `cameras.json` authoring format (version 1), independent of any genre or runtime GPU handles.
- `CameraNameComponent`, `cameraSceneToJson(const World&)`, `loadCameraScene(World&,SceneHierarchy&,const Json&,std::vector<Entity>*,std::string*)` write/read native ECS cameras, fixed-point local transforms, projection, output RT asset keys, update policy, screen viewport/stack, rudimentary rig settings and physical lens parameters.
- Camera scenes have bounded camera count and reject invalid/duplicate stable IDs, invalid clip planes and missing RT output asset keys before creating entities. Any native C++ game can use this parser to instantiate camera entities into its own World and then use the existing CameraRenderPipeline.
- Studio new dockable `StudioCamerasWindow` (editor windows CMake glob) offers create/select/delete, projection/FOV/clipping/priority, RT key/size/HDR/depth, update mode, viewport, save/reload and rendered preview through `ed.renderScene`. Uses an **Editor-owned ECS World**, NOT the live Simulation/World, to avoid mutating gameplay authoritative state. Cameras saved explicitly to project `cameras.json`.
- Studio Scene camera may take a selected camera's view via orbit conversion (roll not retained in Scene freefly orbit controller). Independent model preview renders exact ECS pose including roll.
- New `camera_scene_asset` test fixture exercises JSON round trip and invalid-ID rejection.

## Compatibility / known limitations
- This project-level asset is distinct from automatic `PersistentEcs` persistence; it can be loaded at game startup by a native host. No implicit registration of camera ECS pools into network/rollback serialization.
- The `StudioCamerasWindow` edits an isolated scene authoring World. Runtime game client does **NOT** automatically load `cameras.json` yet; explicit code integration into game project startup still needed.
- Camera rig target Entity references are not serializable by raw entity ID across re-created worlds. This v1 asset intentionally omits cross-world target references; native game must resolve target bindings using stable authored object IDs after both worlds are loaded.
- This first editor UI does not yet expose advanced CameraRig and physical-lens controls beyond basic view settings, nor input picking/preview RT materials, GPU profiling, cross-scene hierarchy parent assignment.
- Read/write tests are authored but not executed; compile errors may exist. The next source pass should inspect JSON mutability in the invalid-asset test and texture teardown before hardware validation.

## Tests actually performed
- Remote GitHub source inspection and successful commit only.
- **NOT RUN**: CMake, build, CTest `camera_scene_asset`, Studio startup, Metal/GL/Vulkan image capture, FPS/RTS client, serialization parity. Deferred as user requested.
- Next verification after features: `cmake --build build/lens --target aiverse_camera_scene_asset_tests`; `ctest --test-dir build/lens -R 'camera_scene_asset'`.

## Next stage
- Improve camera authoring validation and source-level test accuracy without executing the suite yet; connect native runtime asset load and optional editor selection.
- Implement capture export/sequence and panorama, optional XR bridge and robust portal recursion/oblique clipping; add one separate code commit + handoff per bounded stage.
