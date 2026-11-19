# AIVERSE LENS — Stage 4: ECS camera policy scheduler (2026-11-19)

## Branch and code
- Working branch: `feature/universal-multicamera-render-targets` (forked from main `58e29ab052458fffcdebebffe27022b53adb9367`).
- Stage 4 code commit: `9e532e0ec6cc7d6705bcd1f16254e49abd2f878a`.
- No changes to `main`, no force-push.
- Files: `engine/sim/CameraComponents.h`, `engine/render/EcsCameraRender.h`, new `engine/render/CameraRenderScheduler.h`, new `tests/render/camera_scheduler.cpp`, `CMakeLists.txt`.
- Prior handoffs: `docs/agent-handoffs/universal-multicamera-stage{1,2,3*}-2026-11-19.md`; architectural plan `docs/camera-system.md`.

## Implementation and APIs
- Cameras stay ordinary **ECS Entities** with existing `World` + `WorldTransform`, no separate entity registry. ECS `CameraOutputComponent` now includes `hdr` and `depth` flags (appended to preserve old positional aggregate inits); new `CameraDependenciesComponent` stores at most eight stable asset keys sampled by the camera scene.
- The read-only `EcsCameraView` now carries declared input dependencies.
- CPU-only `render::CameraRenderScheduler::buildPlan(world, frameIndex, seconds, renderOrigin)` visits existing ECS camera entities without mutation and returns ordered `CameraRenderPlan.jobs` and `diagnostics`.
- Supports `EveryFrame`, `EveryNFrames`, `FixedRate`, `OnDirty`, `Manual`. Host calls `requestCapture(entity)` / `markDirty(entity)` to trigger capture; `acknowledge(entity, output, frameIndex, seconds, succeeded)` *only after successful rendering* updates schedule state. Failed output allocation/render does not consume a manual request.
- Validates dimensions, nonzero texture asset key, duplicate target writers, declared dependency target existence, duplicate reads and cycles. Rejected views never enter the GPU job list. When a producer is scheduled, it runs before a consumer. Stable priority and entity-order tie breaks.
- Until multi-viewport composition is implemented, selects the highest-priority enabled screen camera (ties by Entity::packed()); remaining screen cameras are suppressed with diagnostics.
- Uses existing generation-aware `Entity::packed()` keys and prunes destroyed entity state on `buildPlan`. The scheduler belongs to **one presentation World**; call `reset()` if replacing a World at an identical address after reload/rollback.
- Render callback must faithfully declare all camera-produced RT reads as `CameraDependenciesComponent`. An undeclared texture feedback loop cannot be detected by this CPU-only layer.

## Source compatibility
- Source-only stage. `CameraOutputComponent` grows by 2 bool fields, so no ABI promise. Added component `CameraDependenciesComponent` needs a stable `PersistentEcs` schema/codec *before* any game persists it. Do not serialize raw structs or graphics handles.
- No camera-to-GPU path in this stage. Existing `gfx::PassDesc` and manually authored RT render paths unchanged.
- `CameraRenderPlan` is presentation-only; no deterministic gameplay or network ownership.

## Tests
- Authored CTest target `camera_scheduler` in `tests/render/camera_scheduler.cpp`: dependency ordering, screen priority, request/acknowledge, every-N, dirty, duplicate output, cycle blocking, destroyed entity.
- **NOT RUN**: CMake configure/compilation, CTest, Metal, Vulkan, GL and editor. This session's container cannot access github.com (DNS resolution failure), and GitHub connector cannot run local CMake. Code commit verified by GitHub write response only.
- When owner checkout is available: `git fetch origin && git switch feature/universal-multicamera-render-targets && git pull --ff-only`; `cmake -S . -B build/lens -DAIVERSE_BUILD_TESTS=ON`; `cmake --build build/lens --target aiverse_ecs_camera_tests aiverse_camera_scheduler_tests -j4`; `ctest --test-dir build/lens -R '^(ecs_camera|camera_scheduler)$' --output-on-failure`.

## Risks / next stage
- Scheduling alone does not ensure that externally read textures are initialized or that a skipped manual producer has an existing image. The GPU execution host needs to validate textures and lifetime. Explicit previous-frame feedback is not yet supported; cycles are rejected.
- Screen overlay/split-screen/viewport composition is not yet implemented.
- Next bounded stage: `RenderTargetPool` for owned RT/depth and a `CameraRenderPipeline` host consuming the plan, reusing `Renderer3D::beginScene -> draw -> render -> endPass`, rejecting unresolved dependencies and preserving per-camera state; make separate code + handoff commits.
- Respect `AGENTS.md`, batching handoff, editor-unification handoff; do NOT merge to main without real CPU/Metal/GL/Vulkan tests.
