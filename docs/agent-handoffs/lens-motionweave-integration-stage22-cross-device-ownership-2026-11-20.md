# AIVERSE LENS Stage 22 — cross-device GPU resource ownership and recovery (2026-11-20)

## Branch and implementation commits
- Repo: `chromealex/AIVERSE`. Branch: `feature/lens-motionweave-integration`; `main` unchanged.
- `1a18eeef624ccdc7c2e70021089cced4b061098e`: `modules/rts/view/Rts2DView.h`, borrowed `resourceDevice_` field.
- `a95a8507a970076be3a32792db3d4b78ab5efe0b`: `modules/rts/view/RtsSprites.cpp`, reject shutdown/guarded recovery with wrong gfx::Device, remember owner even when early initialization throws, clear only after successful teardown.
- `fa4bc6da81f3e3b78ac9f25bd721d849473ed76b`: `tests/render/lens_sprite_gpu_fault_injection.cpp`, extend existing fault test with a failed initial shader allocation + refused idle, cross-device cleanup refusal and correct-device retry.
- Previous: `docs/agent-handoffs/lens-motionweave-integration-stage21-reload-preflight-2026-11-20.md`.

## Design and guarantees
- `Rts2DView::init` checks `ready_`, `rules_`, `resourceDevice_` before a new initialization. The borrowed device pointer is set BEFORE the first GPU allocation, retained after any failed GPU idle barrier and cleared only by completed `shutdown`.
- Both `shutdown(gfx::Device&)` and `shutdownAfterGpuIdle(gfx::Device&)` refuse mismatched device arguments via `std::logic_error`. A wrong device is not asked to wait idle and cannot destroy buffers/textures allocated by the original.
- `shutdownAfterGpuIdle` returns true without waiting when no device/resources are owned, supporting idempotent retry after successful cleanup. On idle refusal it returns false with ownership unchanged.
- No ECS, genre-neutral render API, scene schema or asset changes. All logic is inside the existing genre view adapter. Stage 21 early `RtsView` reload preflight remains in place.

## MotionWeave branch status
- Compared `feature/lens-motionweave-integration` against `feature/universal-ecs-animation-pipeline`: histories are divergent, with 74 commits ahead and 128 behind in the direction of the animation branch at comparison time. `apps/modelview/main.cpp` is a materially changed path.
- No merge of MotionWeave performed in Stage 22. Must reconcile changed files explicitly and keep both branches' contributions.

## Checks actually performed
- Inspected the current branch implementation, added the code and test through GitHub, re-fetched modified files to verify expected edits.
- Native CMake configure/C++ compile: **NOT RUN**.
- Fault injection execution and CTest: **NOT RUN**.
- Metal/Vulkan/GLES backend compile and GPU visual parity: **NOT RUN**.
- Do not interpret source checks as passing tests or verified GPU safety.

## Caveats and next actions
1. Stage 22 adds a test to the pre-existing `lens_sprite_gpu_faults` CMake target. Confirm that `FakeDevice`, `Rules`, and the linked `Rts2DView` symbols compile with the native toolchain, and resolve any linker/build errors.
2. Full transactional hot reload remains incomplete: after Stage 21 preflight succeeds, later assets/fog/terrain/model loading can still fail halfway through. Design staging and atomic publication for remaining resource owners.
3. A borrowed device pointer is not a generation/lease token. If a gfx::Device object is destroyed and its address reused, raw pointer equality cannot prove generation identity. Integrate the engine's existing GPU device-lease/generation contract for long-lived owners before considering device loss recovery complete.
4. Audit host-managed device lifetime, unload/reload and all nested shutdown paths. Stage 19 low-level shutdown still requires proven idle.
5. Resolve latest MotionWeave with conflict-aware merge, then run the user's requested consolidated native compilation, full CTest and Metal/Vulkan/GLES checks. Do not merge into main without validated evidence.
