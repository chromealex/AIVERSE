# AIVERSE GI — Stage 40: transactional terrain / foliage / world publication (2026-11-20)

## Source commits and status

Branch `feature/gi-lighting-portable-backends-20261119`, draft PR #47 still targets `feature/hybrid-dynamic-gi`, not main.

- `78eb09d7e23b5883a02d0f141eeeaf0184ff92a0` — `engine/render/GiWorldPresentationSync.h`: new genre-neutral presentation coordinator holding `GiGeometryScene` and `GiWorldDirtyAccumulator`; commits both to a provided `GiLightingController` only after its immutable GPU/CPU geometry revision has actually been accepted.
- `3a34d428608611319720d0665c4cec9b34b912ab`, `8e01d992db943512e574adbb98117fb82f0d7813` — `tests/render/gi_world_presentation_sync.cpp`: CPU GLES3 geometry publication and Metal FakeDevice GPU-fence refusal/retry, preserving old/new dirty regions, source revision, and old snapshot until safe retirement. Test expects 2 distinct nonoverlapping old/new height edit regions.
- `3273090182ccb999b5829dc84b9f31cd5851bea0` — `CMakeLists.txt`: `aiverse_gi_world_presentation_sync_test` + `gi_world_presentation_sync` CTest target.

**Actual verification:** GitHub source reads and commits ONLY. Native C++ compile, CTest, real Vulkan/Metal/GLES GPU fixtures and full acceptance are **NOT RUN**. Source tests were authored but have NOT been executed.

## Architecture / API

`GiWorldPresentationSync::geometry()` returns the existing `GiGeometryScene` that owns stable world-space collision triangles, cached per-chunk BVHs and immutable snapshots; `submit(GiWorldChange)` collects previous/current edit bounds/reason/transport radius in a bounded accumulator (up to 128 merged regions then full-volume invalidation). `publish(GiLightingController&)` performs:

1. If transport is not initialized, returns deferred, preserving edits.
2. If `GiGeometryScene::hasPendingChanges()` or an initial scene has not been published, calls existing `GiGeometryScene::publishGpu(transport)`. This template works with BOTH CPU and GPU controller implementations through `setGeometry`/`setGeometryRegion`. It obtains one monotonic revision and conservatively unions old/new changed bounds.
3. If GPU reports refusal due to in-flight compute/fence/budget, it returns `deferred` and **DOES NOT DRAIN EVENTS, RELEASE OLD IMMUTABLE SNAPSHOTS OR ADVANCE THE PUBLISHED REVISION**. Host retries on later frames AFTER completion has been observed by `updateFrame`.
4. Only after geometry accepted does it flush remaining world/material/light/weather events into `GiLightingController::applyWorldChanges` and clear the accumulator.
5. Retires old immutable snapshots through last published revision - 1 only when `!transport.gpuBusy()`. Never releases current snapshot, which controller also owns. CPU reference is synchronous, GPU sources keep their frame fence lifetime.

Existing `engine/world/GiWorldGeometryBridge.h` already provides stable key sources from exact WorldScene authored collision triangles and TerraWeave TerrainPatch collision (preferred), plus cm grid fallback and dirty rectangle adapters; use those methods to fill `GiWorldPresentationSync::geometry()` after ECS/physics/TerraWeave read barriers. Vegetation/fallen-tree collision triangles can be passed to `geometry().replaceChunk()` under a stable namespace; do NOT implement a second terrain mesh or double-publish both authored and live patch geometry. This coordinator introduces no FPS/RTS game logic, ECS writes or new persistence schema.

### Known limitations

- This is a safe **explicit publication coordinator**, not an automatic observer subscribed to all TerraWeave/PHYSIS/NEREID/scene components; the game/editor must still call it after producer barriers.
- GPU single-scene BVH remains limited (currently 8192 triangles), so a giant resident world can be refused. A refusal is deliberate fail-closed, NOT a silently truncated shadow caster set; large worlds need bounded resident clipmap selection/LOD or a GPU chunk BVH pipeline. Rejected geometry and events remain pending to retry or to reconfigure quality.
- Events for lighting alone may be sent without geometry changes; terrain/albedo/vegetation events MUST also replace/publish the immutable BVH or the tracer still sees old collision. A deferral does not implicitly pause authoritative ECS/Box3D.
- Full per-material shader-graph/terrain/foliage *receiving* GI is not implemented by this coordinator. It only ensures their collision/occlusion affects the shared bounce source if the host supplies the data.

## Exact next steps

1. Native compile `aiverse_gi_world_presentation_sync_test` and all GI tests, run CTest after functional closure; record actual PASS/FAIL and tested commit SHA in a separate handoff.
2. Connect explicit resident TerraWeave/WorldScene producer update hooks and PHYSIS vegetation event snapshots using the exact source fields of their integrated branches, without guessing interfaces. Reject excessive GPU triangle budgets safely, add terrain/foliage LOD/proxy policy and basic benchmarks.
3. Extend GI to terrain/foliage/custom Shader Graph *receivers* using shared sampling/texture binding and sampler caps; do not steal Shader Graph tex13.
4. Run actual Metal/Vulkan/GLES shader compiler/driver/pixel tests and full acceptance once features closed; reconcile with diverged main before merging.
