# AIVERSE LENS Stage 18E — safe repeated rendering and consumer RT lookup (2026-11-19)

## Branch / commits
- Branch `feature/universal-multicamera-render-targets`; main not merged.
- `41fd2f4f25c75fbfaf23fb7d3924b69487ca41b9`: GameClient avoids replaying mutable RtsView by default.
- `4659e8ac204d0d74249f7ce2acc78593407878a9`: CameraClientAdapter exposes resolved color/depth/info target lookup.
- `f639f4ee0952dc95b3b915b9bae8e698e5324d35`: const CameraRuntimeHost::pipeline() overload.
- `84bc3f734621257b7bb14b867f1d3af4c8bdc1fd`: GameClient public lens target lookup API.
- Files: `apps/client/GameClient.cpp`, `apps/client/GameClient.h`, `engine/render/CameraClientAdapter.h`, `engine/render/CameraRuntimeHost.h`.

## Statefulness discovered
- `RtsView::draw()` is not pure geometry emission. It advances `lastTime_`, `frame_`, animations and presentation caches; clears/recreates `events_` and `effects_`; and calls `handleEvents()`. Replaying it once per LENS camera changes legacy presentation behavior, particularly particle triggers and effect queues.
- The optional GameClient LENS path now draws `worldScene_` scenery by default only. An explicit **experimental** `game.json` setting `lens.experimentalReplayRtsView=true` enables the previous full RtsView replay for debugging; it must not be used in production without extracting a side-effect-free visual frame snapshot and pure geometry submit.
- **Important:** the default texture output therefore currently contains scenery where available, but can be empty/incomplete in RTS scenes. This is a safety trade-off, not full multi-camera world rendering. The generic `CameraRuntimeHost` remains able to render complete scenes when its client provides a side-effect-free `SceneSubmit` callback.

## Consumer API
- `CameraClientAdapter::colorTarget(assetKey)`, `depthTarget(assetKey)`, `targetInfo(assetKey,w,h,hdr)` look up only ready RenderTexture resources in the managed `CameraRenderTargetPool`, returning invalid handles on absent/unready sources.
- `GameClient::lensColorTarget`, `lensDepthTarget`, `lensTargetInfo` forward those lookups to native/UI/material consumers after a successful opt-in.
- Handles are **non-owning and frame scoped**. Never retain across resize/release/project reload, destroy in caller, or sample a texture while it is the active render target. Cross-camera reads must be declared as `CameraDependenciesComponent` edges so scheduler can reject cycles.
- The underlying RHI texture does not have a stable ECS identity; only `targetAssetKey` is persistent.
- These are native lookup APIs, **not** an automatic UI widget or named material-slot binding. The host must actively bind them in its UI/material rendering code.

## Tests actually performed
- Source inspection and GitHub writes only; **no** compiler, CMake, CTest, Metal/Vulkan/GLES shader build or runtime GPU validation. The added declarations need a full compile check.
- Before shipping: validate native GameClient build, screen pass remains unchanged, extra RT is usable by UI/material and does not reenter its own render pass, scenery-only behavior, and that no `RtsView` visual events or animations double-advance.
- No new tests added in this stage. Prior fixtures remain pending one consolidated test batch.

## Remaining blockers
1. Extract immutable `RtsVisualFrame` or equivalent from the single primary `RtsView` animation/effect update; submit geometry for each camera without advancing visual state. Until then, RTS offscreen output is incomplete.
2. Provide explicit UI and material-slot binding for RT asset keys rather than only native handles.
3. Audit `Device::waitIdle` implementations and branch compiler errors on Metal, Vulkan, GLES/GL33.
4. Finish one consolidated build/test cycle, fix failures and document actual measured results. Do not merge to main before verified.
