# AIVERSE LENS Stage 18 — Studio authoring and native camera project loading (2026-11-19)

## Provenance
- Branch: `feature/universal-multicamera-render-targets`; no merge to main.
- Editor commits: `31c9bbb63a20f30def0466c93c147bb139d477ac`, `65f96e4f8ad52382f5c2a6275c8d2cad79dd6582`, `1fcd094bf420e0bd7e4e3f26d6ac9b9ac0494063`.
- Native startup loader: `95adfd97207aa8208b9d1b6c4177ab4d2129e5d0`.
- Asset roundtrip: `0763f61e8910ebc025d2b860d36dd81f42d67c25`; test fixture `5c9c5b3f1278bb947ded96503ab49cbe5164193e`.
- Changed: `apps/editor/windows/StudioCamerasWindow.cpp`, `engine/sim/CameraSceneAsset.h`, `tests/render/camera_scene_asset.cpp`; added `engine/sim/CameraProjectLoader.h`.
- Previous stage: `docs/agent-handoffs/universal-multicamera-stage17-hdr-resolve-2026-11-19.md`.

## Implemented
- Studio LENS camera inspector now edits camera stack order, opacity and overlay; CameraRig mode/enable/offset/distance/yaw/pitch/smoothing/trackTarget; physical lens FOV, focal length, sensor size, f-number, focus, shutter, ISO. CameraRig modes in the editor match the enum order (Static, LookAt, Follow, Orbit, Rail).
- Newly authored cameras include Rig/Lens/Stack ECS components. Studio preview applies physical FOV when valid. Follow/Orbit/LookAt target bindings and rail control points remain runtime-managed, not serialized as raw Entity handles.
- RenderTexture authoring already exposed key/size/HDR/depth, with a clarification that HDR-to-SDR capture must be executed through the runtime LENS pipeline, not an editor preview screenshot.
- `CameraSceneAsset.h` roundtrips CameraRig.enabled; old v1 assets default to true.
- New `loadProjectCameras(World&,SceneHierarchy&,filesystem::path,vector<Entity>*,string*)` loads `cameras.json` using the existing validated parser. Explicit call required by the native application's initialization code. It does not silently mutate simulation.
- Extended `camera_scene_asset` CTest fixture with Rig/physical lens/stack roundtrip checks.
- No genre-specific code was added.

## Not yet complete
- Full client runtime integration is NOT automatic: game startup still must call `loadProjectCameras` and then drive `CameraRenderPipeline` per frame after hierarchy update. No specific RTS/FPS client was changed here because the common ECS implementation must stay genre-independent.
- Studio's preview is still a separate Editor-managed viewport, not a live LENS pipeline RT preview. It does not currently export screenshots, bind targets to materials, invoke GPU recording, or show capture status.
- Native HDR/EXR export, camera dependency visual graph, persistent named Entity target bindings, XR runtime/session, and complete portal authoring remain outstanding.
- This stage must not be reported as complete end-to-end UI/client integration.

## Tests actually performed
- GitHub code writes only. **NO** CMake compile, CTest, Studio startup, client launch or GPU hardware validation performed. All new functionality is unverified until consolidated test pass.
- Run `camera_scene_asset`, `camera_frustum`, `camera_hdr_resolve`, all `camera_*` tests; build editor and runtime; inspect ImGui behavior, native asset loading and offscreen RT GPU parity.
- The additional preview FOV uses existing `render::validCameraLens` API. Compilation and linkage remain to be confirmed.

## Next actions
1. Add actual LENS pipeline-backed preview/record/export UI to Studio with renderer ownership and safe frame lifecycle; wire editor capture buttons to a real render target.
2. Bind generic project startup to `loadProjectCameras` in the shared application layer, not RTS/FPS code, after reviewing world creation and hierarchy ownership.
3. Audit every stage, run one full build and consolidated CTest, GPU tests where available, and build-only Vulkan/GLES; correct errors, document results, then decide on merging.
