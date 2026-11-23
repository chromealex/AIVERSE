# AIVERSE Buildings — Stage 23T.7: temporary native Renderer3D building preview

Date: 2026-11-24
Branch: feature/aiv20261123_building-grammar
Draft PR: https://github.com/chromealex/AIVERSE/pull/114

## Goal and constraints

User requested continuation after Stage 23T.6 (saved recipe editing, physical socket inspection and editable 2D layout). Implement actual temporary *interactive 3D preview before GLB creation* using current native StudioScene, RenderDeviceLifetime and Renderer3D, not a fabricated WebGL model, without project Assets writes or genre-specific FPS/RTS modules. User explicitly deferred tests until all features are complete.

## Existing code inspected

- apps/studio-native/StudioScene.h/.cpp: shared Metal owner, frame() branches for isolated facial and material preview, renderer_.beginScene/ModelDraw/render, camera orbit/pan/zoom, FrameScope, releasePreviewModel and device lifetime.
- apps/studio-native/StudioHost.cpp: handleHostMethod dispatch and onScene thread serialization, actual native Scene lifecycle and existing asset-thumbnail logic.
- apps/studio-native/StudioBuildingGeneration.h: native bounded request/produce/applyProjectMaterials, real StudioAssetCatalog for project GUIDs, runtime-generated PBR maps.
- engine/buildings/BuildingMaterialAssets.h: already binds project GUID / deterministic generated actual render::Image data with authored PBR factors to render::Model.
- apps/studio-web/src/panels/generated/BuildingGeneratorPanel.tsx, types.ts and mock.ts: validated request, UI controls and demo safety.

## Implemented files / changes

1. apps/studio-native/StudioBuildingGeneration.h: studioBuildingPreviewCpuModel(engine,project,params) validates the exact same current building request, produces geometry in memory, applies project materials, resolves explicit GUID texture assets through StudioAssetCatalog and bounded non-symlink project content paths (16 MiB maximum), decodes image bytes using render::decodeImage, then creates render::Model using runtimeTexturedBuildingModel. The existing generated surface PBR images bind automatically when no GUID is assigned. No GLB/recipe/.meta file is written.
2. apps/studio-native/StudioScene.h/.cpp: new render-thread-only setBuildingPreview(unique_ptr<render::Model>), hasBuildingPreview(), private buildingPreview_ CPU/GPU model and GPU-residency flag. Uses the same gfx::Device and render::RenderDeviceLifetime generation. On replacement or stop it calls waitForResourceIdle OUTSIDE any render pass, invalidates Renderer3D model caches and releases the old Model. On upload it prepares a dedicated dynamic-safe Model with render::prepareModelUpload and render::uploadModel(keepCpu=true,publishSharedTextures=false). Failure retains a partially uploaded Model if GPU idle cannot be confirmed and never draws it (buildingPreviewGpuReady_=false). Successful preview frames run StudioScene's existing camera/viewCamera, sun/sky/ground lighting, Renderer3D::ModelDraw, postprocessing/FXAA and StudioFrameScope. Model camera target/radius derived from actual mesh bounds. Existing StudioScene destructor also releases the preview after its current GPU-idle step.
3. apps/studio-native/StudioHost.cpp: native-only RPC studio.buildingPreview (validates current project and Scene viewport, builds CPU model, moves it to the onScene graphics thread) and studio.buildingPreviewStop (clears via the same graphics owner). Neither request invokes a new standalone renderer nor alters project content. Native errors propagate through the existing StudioHost answer handler.
4. apps/studio-web/src/types.ts: typed new preview and stop RPC request/result pairs and method name union.
5. apps/studio-web/src/mock.ts: explicitly refuses both native graphics actions in browser-only demo.
6. apps/studio-web/src/panels/generated/BuildingGeneratorPanel.tsx: Preview 3D in Scene and Close 3D preview controls, previewBusy and livePreview state, mounts the real Scene panel before invoking the preview RPC, attempts stop on changing project, and clearly distinguishes geometry seed evaluations from the temporary GPU viewport. It reuses the existing form values and preview-selected seed and does not generate assets.
7. skills/world/aiverse-building-style/SKILL.md: updated instructions and lifetime/ownership contract for the next agent.
8. This separate handoff file is the required stage record.

## Commit provenance

- fdc98b10916e76c305c96a621ab56f9eee69e088 — StudioScene preview API and ownership.
- 95ec0cf28548409c32977a6aa1ba1276fcb42919 — native preview GPU render and safe replacement.
- 42b3f335df2e939fa5d293413eac56a17176c6c8 — complete CPU building/PBR temporary model.
- a33f33579706431b8fb62218598712ed96e21feb — host RPC and render-thread dispatch.
- 050cf458512b8ec2ea4f9af836cbd11a39b1ed9f — browser request/result typing.
- 201578a173ee4ebfba2972aec4d767d07ce94e4a — browser demo explicit rejection.
- da9613cce86ca53e5edda45b476ae421ff374b35 — UI live 3D start/stop buttons.
- fdc98b10916e76c305c96a621ab56f9eee69e088 and eff8b29ef803a25497d3773f2f4a4983bc1a23c0 — safe partial-upload GPU residency protection.
- f92e5f6fba1891a89ff407c52c9fc3a5680682f5 — mount Scene before native RPC.
- 45099f2c539edba0aab9d4f742868218f316a845 — updated master skill.
- This handoff is its own final stage documentation commit.

## Actual validation and limitations

NO C++ compiler, CMake, CTest, Vite/TypeScript, native Studio startup, GPU screenshots, image decode verification, GLB parser, PHYSIS or Metal/Vulkan/GLES test were run, as explicitly requested by user. All features above reflect source committed through GitHub, not proof that live GPU Studio preview works. Any compile/runtime mismatch must be fixed during deferred QA before merge.

GPU cleanup uses a synchronous verified full-device idle wait (may stall UI on repeated preview replacements); future optimization may employ existing async retirement queue with safe Model leases/fences. Model CPU generation currently runs in native host request handling and could block UI for complex 150k-triangle input; a bounded cancellable worker pool would improve responsiveness, but is not claimed here. Scene viewport must exist and be mounted; panel initiates mounting before the RPC, but there can still be a slow native initialization race causing a clear error/retry. Existing shared viewport interactions are used instead of implementing an independent 3D graphics engine. The preview mode is local Studio-only and does not advance ECS/PHYSIS or change gameplay state. Full multi-scene PreviewMode arbitration with facial/material workflows and editor detach/reload should be examined when testing.

## Next steps

Stage 23U: run actual complete native CMake and browser TS builds, CTest + strict style JSON parser for all twelve settings, glTF PNG/PBR/UV validator, preview start/replace/stop GPU captures, and PHYSIS two-leaf rollback under Metal/Vulkan/GLES. Repair failures and inspect PR #114 for divergence from main; keep Draft until tests and integration review pass. Only publish devblog with genuine successful engine screenshots. Continue separate handoff after any fixes.
