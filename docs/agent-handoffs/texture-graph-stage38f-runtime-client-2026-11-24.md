# Stage 38F — native client frame integration

Branch: `feature/aiv20261124_texture_graph_gpu_slots`

Plan: `1cdd5d69c02046456e9deba3310adf53cd6ee410`
Implementation: `4387bdeba5082e5246ec08149ee5907ce5406994`
Final implementation: `b66014af3a1b3875a08c206709ee1dd5f09af908`

Stage 38F: native GameClient now has one TextureGraphGpuOwner for its distinct presentation host and an immutable request/target task; public startRuntimeTextureMaterial, runtimeTextureMaterialStatus, cancelRuntimeTextureMaterial and runtimeTextureMaterialTarget. The frame hook ticks before new rendering and cancels requests when match/target publication changes; completion applies only matching accepted textures at the existing GameView frame-boundary adapter, used by editor as well. No simulation state or persisted changes; a type/choice material override is shared by that choice, not per-entity. Shutdown proves resource idle before releasing owner and Model cache. No frame work for unused feature. Native generic hosts can continue using TextureGraphGpuOwner directly; client convenience route supports one active auto-apply task, not a multi-target scheduler. Stage 38E follow-up: GameView and GameClient expose explicit collectGraphMaterialImages / collectRuntimeTextureMaterialImages. GameView adds its current model/LOD/stage material copies and snapshots to externally certified roots and checks the current target identity and joined readers. Automatic reclamation remains disabled: renderer packet caches and external historical pins are not yet completely audited; no remote caller is allowed to claim complete roots through MCP. Existing editor Studio/MCP material actions use shared publication adapter and retain diagnostics; no live hosted-client MCP routing yet. Isolated task/identity fixture passed on GCC and Clang. Native build, runtime integration, GPU and Mac gate NOT RUN.

## Native use

```cpp
render::texture_graph::ResidentMaterialOptions options;
options.acceptance=render::texture_graph::ResidentAcceptance::FenceOnly;
options.incremental=true;
auto queued=client.startRuntimeTextureMaterial(type,choice,material,graph,Json::object(),options);
auto request=queued.getStr("applicationRequest");
// Existing GameClient::frame services the task. Poll runtimeTextureMaterialStatus().
// applicationState=applied is the actual publication result, not GPU-ready alone.
```

Collection is explicit and only for trusted native owners able to certify all external roots. Refresh runtimeTextureMaterialTarget after changes. Do not use an empty external root span unless all external references truly have been detached or are absent.

## Actual evidence

Reconstructed exact files and checked git hash-object:
- engine/view/LiveAppearanceMaterialTarget.h: ae916e74c0273b46773b5e30d1f91ce4dac48167
- engine/view/RuntimeTextureGraphTask.h: d61cdfb624489d20343e2555742abdbccb19a923
- tests/view/live_appearance_material_target.cpp: 27b079d06a5dac7de9aa2dd3df56e6d60dcdfc92

```
g++ -std=c++20 -Wall -Wextra -Werror -I. tests/view/live_appearance_material_target.cpp -o test-gcc && ./test-gcc
clang++ -std=c++20 -Wall -Wextra -Werror -I. tests/view/live_appearance_material_target.cpp -o test-clang && ./test-clang
```

GCC 14.2.0 (Debian 14.2.0-19) and Clang 17.0.0 both exit 0. Task decisions cover queued, ready but busy, accepted, unaccepted, mismatched request, failed, inactive host. Files unchanged by final client-only commit. Isolated Linux fixture, not full checkout evidence report. `git ls-remote https://github.com/chromealex/AIVERSE.git HEAD` failed DNS in this environment. Native client build, integrated editor/Studio/MCP, runtime application, automatic collection and actual GPU/Mac gate NOT RUN.

## Next

Complete renderer/cache root lifetime audit and automatic reclamation (38E remains partial); exercise client/hosted-client application and shutdown on Metal/Vulkan. Stage 38G hardware acceptance and profiling. No merge until integrated and Mac quick gates pass.
