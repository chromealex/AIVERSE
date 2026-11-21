# AIVERSE LENS Stage 31 — cross-owner reload preflight and prepared fog/overlay definitions (2026-11-20)

## Branch and exact commits
- Repository: `chromealex/AIVERSE`; branch: `feature/lens-motionweave-integration`. `main` untouched.
- `2c39eaf93065dda7a3946dc00d6daee88c5f34a9`: `modules/rts/view/RtsOverlays.h`, new additive `initPrepared(const Rules&, OverlaySet, const std::string&)` CPU-publication API.
- `c1bf1a3579c3ee7af52d459f659951dfef46a38c`: `modules/rts/view/RtsOverlays.cpp`, existing `init` delegates to `initPrepared`; common setup consumes the prepared set with move semantics.
- `1ae99c830e7036a2629262789e66a10cbd0f32ef`: `modules/rts/view/RtsView.cpp`, parse `FogLook` and `OverlaySet` from the one immutable incoming rules/game snapshot BEFORE staging and publishing GPU sprite resources. The existing overlay GPU initialization remains in its original later position.
- `bfd63c3a00aa1f9bdc8608cf873b0d663b0467af`: `tests/tests.cpp`, CPU parity test for direct versus prepared overlay publication, including warning/selection/hover state, identifiers and enabled flags.
- `76ac7420c2bd9113631483cd2e038f2726241891`: `modules/rts/view/RtsView.cpp`, strengthen GPU device identity and idle preflight for *all* existing view owners, not only `twoD_`.
- `412c29e4c85eeaac3c078ba90c8bda36825d1fe5` and `b6068c1a099133131cf768fa74640c866b1d2261`: `tests/tests.cpp`, regression for refused reload GPU idle and foreign RHI device preserving current fog/overlay state (and fixture adjusted to use `VisibilityLook::style`).
- Previous: `docs/agent-handoffs/lens-motionweave-integration-stage30-geometry-partial-retirement-backpressure-2026-11-20.md`.

## Architecture / decisions
1. CPU-only `parseFogLook(incomingRules["fogOfWar"],incomingGame["fogOfWar"])` and `loadOverlaySet(rules,incomingRules["overlays"],incomingGame["overlays"])` now happen before GPU replacement. The incoming set is a complete parsed snapshot, and `RtsOverlays::initPrepared` consumes it during normal publication without re-reading or reparsing the definitions.
2. Existing `RtsOverlays::init` still works for all other callers; it forwards to `initPrepared` using the old parser. No breaking API change, ECS schema change, save-format change or extra animation/simulation tick.
3. `RtsView::init` previously rejected foreign device/idle only if `twoD_` existed. But Terrain, Fog, Road/Shore kits and Overlays can remain initialized while `twoD_` is null (e.g., shader failure or a partial load). It now checks `dev_ != &dev` and establishes idle when `dev_ || twoD_`, before changing their active state.
4. Other resource owners' GPU initialization is **not yet transactional**. `RtsOverlays::initGpu` may leave partial shader/pipeline/texture state on failure; `VisibilityField::init/build` require a separate transaction and buffer-retirement review. Road/Shore `init` synchronously calls shutdown on previously bound device, and `TerrainRenderer` has its own lifetime/lease contracts. Do not swap them blindly.
5. Road and shore host owners are `unique_ptr<RtsRoadView>`/`unique_ptr<RtsShoreView>`, but have lazy build, GPU kit textures, linked road/shore map state and explicit idle-gated `shutdown`. A staged replacement requires transferring/retaining previous runtime map/weather/ghost state and a defined failed-stage recovery owner before it can be claimed atomic.

## Authored tests (not executed)
- `overlay_prepared_reload_matches_direct_init` compares direct and prepared overlay initialization, warning vectors and enabled selection/hover/definition state. A moved/prepared definition set is also accepted.
- `view_reload_checks_gpu_idle_before_mutating_overlay_and_fog_state` builds a view with a synchronous fake RHI, refuses idle during reload, verifies overlay definitions and Fog style remain unchanged, and rejects attempting a different device before its idle barrier. After restoring idle, it shuts down the first device.
- Both tests belong to the existing `tests/tests.cpp` suite. The tests were authored and re-fetched; compiling/running them remains mandatory before claiming success.

## Validation actually performed
- GitHub source inspection of `RtsView`, `RtsOverlays`, `RtsRoadView`, `RtsShoreView`, `VisibilityField`, `TextureCache`, and earlier Stage 30 handoff.
- Post-write GitHub readback verified the declarations, code-path rewiring, cross-owner device guard, idle guard and both test names.
- **NOT RUN**: native CMake configure/build, CTest, Metal/Vulkan/GLES backend compile/device tests, per-camera pixel parity, performance benchmarks or a full MotionWeave merge. No green tests are claimed.

## Next concrete actions
1. Stage and atomically publish the first remaining GPU owner with a robust failed-initialization recovery owner — likely `VisibilityField` after providing GPU-idle-gated initialization rollback and a safe move/swap state, or Road/Shore kits with explicit state carry-over. Keep normal initial-load semantics and call sites unchanged.
2. Inspect and fix potential shader/vertex/index/texture allocation errors in `VisibilityField::init/build` and `RtsOverlays::initGpu`; never release in-flight RHI handles without a completion proof. Add fault tests around their actual ownership contracts.
3. Expand to Terrain, Roads, Shores, Overlays GPU-side transactionality, then perform conflict-aware MotionWeave reconciliation against `feature/universal-ecs-animation-pipeline`, preserving changed `apps/modelview/main.cpp`.
4. Run one consolidated native CMake build, CTest and supported Metal/Vulkan/GLES compile/device validation; investigate any Stage 18–31 build issues before merging into `main`.

## Current status
- Universal camera/RT and prior GeometryScene transaction: code authored but native validation pending.
- Stage 31 CPU overlay/fog preflight and cross-owner idle guards: source committed, tests authored, **not executed**.
- Complete RtsView GPU transaction: **not finished**.
- Latest MotionWeave integration: **not finished**.
