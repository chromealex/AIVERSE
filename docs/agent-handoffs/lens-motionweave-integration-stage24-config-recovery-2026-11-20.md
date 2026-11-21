# AIVERSE LENS Stage 24 — consistent reload config and retained failed sprite staging (2026-11-20)

## Branch and code commits
Repository: `chromealex/AIVERSE`. Branch: `feature/lens-motionweave-integration`; `main` unchanged.

- `5de218b376b066e5f88b50fdc2c9a00d6ff48dee` — `modules/rts/view/RtsView.cpp`, resolve `incomingGame` / `incomingRules` **once** via one `incomingRead` and share the parsed values with staging and fog/overlays/road/shores configuration.
- `31cd4089003858a40d551b640c101b426e3f4595` — `modules/rts/view/Rts2DView.h`, expose additive `ownsGpuResources() const noexcept` as a host recovery signal.
- `cb54378ae6816ecd77c896840a11f892e21ca41c` — `modules/rts/view/RtsView.h`, add `std::unique_ptr<Rts2DView> pendingSpriteRecovery_`.
- `9074e8345ecefea103bd14fb6ea5282407564b03` — `modules/rts/view/RtsView.cpp`, preserve failed staged sprite owner when cleanup cannot establish GPU idle; retry recovery at next init before state changes; release pending owner during final `shutdown` after proven GPU idle.

Previous: `docs/agent-handoffs/lens-motionweave-integration-stage23-staged-sprite-reload-2026-11-20.md`.

## Design
1. The initial config read now parses `game.json` and composed rules into `incomingGame` and `incomingRules`. Staged sprites and subsequent fog/overlays/roads/shores use the same snapshot. The `read_` callback is copied from `incomingRead`; no second `game.json` / composed rules parse for these consumers.
2. During `nextSprites->init()` exceptions or returned failure, a failed staged instance that still owns the original GPU device is moved into `pendingSpriteRecovery_`, not deleted by the local `unique_ptr`.
3. At entry to a later `RtsView::init`, the pending owner must pass `shutdownAfterGpuIdle(dev)` before normal reload may proceed. On idle refusal it remains owned and the previous published scene is not mutated.
4. At final `RtsView::shutdown`, the pre-existing full-device idle barrier also gates cleanup of the pending owner. No GPU handles are disposed via a foreign device.
5. These changes **do not** make the entire RtsView reload atomic. Failure after `twoD_` has been published can still leave other resource owners partially updated; remaining owners need independent staged construction/swap.

## API and lifetime
- Additive public `Rts2DView::ownsGpuResources()` API. No format/schema changes, no new genre coupling in the renderer and no changes to MotionWeave animation.
- The pending sprite owner must not outlive the device. Normal host `shutdown` is required before destroying `RtsView`; direct destruction of an abandoned owner without proper shutdown is still unsafe.
- No background retirement or device-loss queue is introduced. A permanently lost GPU/device needs separate host handling.

## Actual validation
- GitHub fetch/readback of edited `RtsView.cpp`, `RtsView.h`, `Rts2DView.h`; checked that single parsed config is used by both paths, and recovery is retained and released only behind idle.
- CMake configure, C++ compiler, CTest (including LENS fault injection), Metal/Vulkan/GLES builds and GPU tests: **NOT RUN**.
- No performance measurements; no verified runtime behavior.
- No MotionWeave branch merge performed.

## Risks and next actions
1. Add a fault-injected integration test where staged sprite init throws after refusing GPU idle, then succeeds in recovery on a later host call; verify the old published scene remains intact. Existing isolated fake-RHI coverage does not exercise this ownership path.
2. Inspect Stage 18 local `RestoreCamera` class for C++ access control compliance and initial load exception paths in actual compiler; do not claim compilation success until checked.
3. Full transactional staging for fog, geometryScene, terrain, roads, shores and overlays remains open. Avoid mutating old owners until replacement resources are fully initialized, with explicit lifetime barriers.
4. Synchronize current MotionWeave history with a real conflict-aware merge and handoff; validate via one consolidated native build/CTest and GPU backend checks before merging into `main`.
