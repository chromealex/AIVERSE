# AIVERSE LENS Stage 14 — universal frame lighting snapshot (2026-11-20)

## Branch and exact implementation commits
- Branch: `feature/lens-motionweave-integration`, `main` remains unchanged.
- `dc87d43cca99d8686191afa00d518738ce4a8890`: `engine/render/Lights.h` — generic `LightSystem::PreparedFrame` and API declarations.
- `edecd533301f81ddef718bb6702401a3ffd12150`: `engine/render/Lights.cpp` — capture/replay point lights, contact shadows, cloud settings, unlit tint, night glow, water reflections.
- `a56f1b12a4431ab39679a2ba3bc04107f8399102`: `apps/client/GameClient.cpp` — snapshot after all primary scene light producers, replay immediately after each secondary `Renderer3D::beginScene`, before secondary scene render.
- Previous: `docs/agent-handoffs/lens-motionweave-integration-stage13-sprite-camera-geometry-2026-11-20.md`.

## Architecture and behavior
- `LightSystem::PreparedFrame` is a CPU-only scene-light packet independent of gameplay genre. It includes all accepted `PointLight` records (object glows, projectiles, map, transient flashes, particle emitters), contact shadow definitions, night emissive multiplier, unlit tint, cloud shadow uniforms and water-reflection enable flag.
- Primary game update remains authoritative for light creation, event handling and animation. Capture occurs immediately before its `Renderer3D::render` call; secondary camera executes `lights.replayPrepared(snapshot)` after `Renderer3D::beginScene` resets the transient light arrays.
- The earlier Stage 12 `ClientFx::addPreparedLights` is intentionally *not* invoked on secondary cameras any more: particle lights are already present inside the universal snapshot. Calling both would double their intensity. Keep Stage 12 API as optional legacy fallback, not a second producer.
- Camera-specific light-map bounds, precise-light selection, halo/beam geometry and spotlight shadow pass are still computed in each camera. Only camera-independent inputs are shared.
- No ECS schema or save file changes; additive public C++ API `LightSystem::capturePrepared()` / `replayPrepared(const PreparedFrame&)`. No RTS/FPS dependencies introduced into `engine/render`.

## Validation actually done
- Inspected current `Lights.h/.cpp`, `RtsLights.cpp`, `RtsView.cpp`, `GameClient.cpp` on the feature branch and committed the edits via GitHub.
- CMake configure/build: **NOT RUN**.
- CTest: **NOT RUN** (user requested consolidated tests later).
- Metal, Vulkan, GLES compile/runtime or visual parity: **NOT RUN**.
- The source changes require native compilation to confirm type compatibility and behavior; do not mark green.

## Caveats and next actions
1. Snapshot capture is controlled by `lensCamerasActive_`. Secondary render is guarded by `sim_`, GPU health and map-loading state. No stale snapshot should be replayed into secondary captures.
2. Light quality and GI/static-grid settings are still owned by the same persistent `LightSystem`; they are not copied in `PreparedFrame`. That is intentional. A separate renderer per camera would need explicit quality/grid synchronization.
3. Prepared contacts are replayed through `addContactShadow` so camera-independent world geometry is reconstructed each pass; spot shadows and display-space effects depend on each camera.
4. Low-tier light-map reuse, transient GPU buffer lifetimes and multiple secondary-camera performance still need native/GPU verification.
5. Next: optimize prepared-frame packet and multi-camera CPU/GPU costs; inspect Stage 13 `SpriteLook` copies and secondary scratch batches. Add targeted source-level tests, then one consolidated native build/CTest and Metal/Vulkan/GLES backend validation. Sync new MotionWeave head with conflict-aware merge first.
