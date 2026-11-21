# AIVERSE LENS Stage 3 — ECS camera entities / read-only render bridge (2026-11-19)

## Branch, commit, ownership
- Feature branch: `feature/universal-multicamera-render-targets`; based on `main` `58e29ab052458fffcdebebffe27022b53adb9367`.
- Implementation: `391c657f5a4b1f24323aa9faa2660348f15801b4`.
- Scope/API clarification and source cleanup: `cb87c31de6c7662780cd36a1758125020ce99018`.
- **No changes to main.** All edits are in the feature branch.
- Changed: `engine/render/Math3D.h`, `engine/sim/CameraComponents.h` (new), `engine/render/EcsCameraRender.h` (new), `tests/render/ecs_camera.cpp` (new), `CMakeLists.txt`, `engine/render/Renderer3D.h` (comment), `docs/camera-system.md` (architecture).
- Previous handoffs: `universal-multicamera-stage1-2026-11-19.md`, `universal-multicamera-stage2-2026-11-19.md`, `universal-multicamera-design-2026-11-19.md`.

## Architecture / API
1. **An authored camera is a native `aiverse::Entity` in the existing `aiverse::World`**, not a separate CameraRegistry entity. It uses `SceneHierarchy` and `Parent` + `LocalTransform` with a derived `WorldTransform`. Existing `Entity` is generation-safe; no second entity allocator.
2. `engine/sim/CameraComponents.h` holds GPU-free, fixed-point `CameraComponent` (projection, FOV, ortho half-height, clip planes, visibility mask, priority, enabled); `CameraOutputComponent` (screen/texture, stable asset key, size override); `CameraUpdateComponent` (EveryFrame, EveryNFrames, FixedRate, OnDirty, Manual). These are POD-like value components, no gfx handles or renderer pointers.
3. `engine/render/EcsCameraRender.h` defines `EcsCameraView`, `validCameraComponent`, `resolveEcsCamera(world, entity, out, renderOrigin)` and `visitEcsCameras(world, submit, renderOrigin)`. Read-only presentation integration converts fixed-point authoritative world transform to floats once, after hierarchy update. Skips disabled/invalid/missing-transform cameras. Missing optional output/update components use defaults.
4. `render::Camera` adds `setPose(worldPosition, quaternion)`, `usePose`, `position`, `rotation`, and `orthoHalfHeightOverride` for fully positioned/rotated ECS cameras (including roll). Orbit camera's `target/distance/yaw/pitch` path remains default and untouched when `usePose=false`.
5. `docs/camera-system.md` now states ECS ownership as canonical and non-authoritative GPU/temporal/runtime state should be keyed by `World identity + Entity::packed() + view variant`, not a duplicate identity registry. C++ tools may submit a temporary `RenderView` snapshot without an authored camera Entity.

## Source compatibility / serialization
- Stage 3 extends `render::Camera` (value type) without removing existing fields or changing its old path; C++ binary layout changes, **no binary ABI guarantee**.
- `CameraComponent` schema is new. **Persistence and native codec registration have NOT yet been implemented.** Refer to `engine/sim/PersistentEcs.h` for stable schema IDs and explicit registration. ECS COW memory snapshots can retain pools; on-disk serialization requires registration.
- `CameraOutputComponent::targetAssetKey` is not a `gfx::TextureHandle`; GPU textures, render passes, transient histories and update clocks belong to the presentation renderer.
- Neither camera component nor resolver requires RTS, FPS or editor dependencies.
- SceneHierarchy must be updated before resolving ECS cameras; using stale `WorldTransform` yields stale pose.

## Test artifacts and actual status
- New `aiverse_ecs_camera_tests` with CTest `ecs_camera` in `tests/render/ecs_camera.cpp` covers two cameras, different projection and asset keys, update frequency, skip invalid entities, render-origin rebasing, direct pose, roll and deletion/gen.
- **NOT RUN**: native compilation of new test, full AIVERSE build, CTest, macOS Metal, Vulkan, GL, editor smoke, hardware RT parity. Only GitHub source read/write/commit and branch verification were executed; no runnable Mac checkout was available.
- Do NOT call the new test `passed` until it runs and results are recorded.
- Suggested commands:
  `git fetch origin && git switch feature/universal-multicamera-render-targets && git pull --ff-only`
  `cmake -S . -B build/lens -DAIVERSE_BUILD_TESTS=ON`
  `cmake --build build/lens --target aiverse_ecs_camera_tests -j4`
  `ctest --test-dir build/lens -R '^ecs_camera$' --output-on-failure`
- Check all consumers of renamed `render::Camera` and Stage 2 portrait extraction in one later full build before merging.

## Known risks / next agent action
- `render::Camera` still models older orbit/orthographic projection conventions for legacy hosts. For pose cameras the new orientation is local -Z forward and +Y up; verify FPS and editor parity, clipping and coordinate-handedness.
- Camera render settings are ECS-authored but **no generic scheduler/RT target allocation or camera-to-RT automatic rendering exists yet**. Manual `gfx::PassDesc` paths already exist.
- Duplicating full `World` into a second camera registry would violate ownership intent. Runtime GPU state must be separate and generation-checked against live ECS.
- Next **bounded implementation stage**: generic camera render scheduler consuming `EcsCameraView`, with generation-safe lifecycle, screen and RT outputs, manual/event-rate scheduling, graph dependency/cycle guards, isolated postprocess state and GPU-safe output lifetime; stage code commit then a separate handoff commit.
- Do not force push, do not merge to main before actual build/CTest and Metal/GL/Vulkan validation. Keep RTX / XR / portal features opt-in until core is proven.
