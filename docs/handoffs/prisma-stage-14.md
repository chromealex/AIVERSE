# PRISMA Stage 14 — first opt-in RenderGraph GPU pass (SDR FXAA)

**Date:** 2026-11-18
**Branch:** `feature/prisma-postfx`
**Previous handoff:** `docs/handoffs/prisma-stage-13.md`
**Code commit:** `843545c6387803f96b0dc86f74b5ddf18eb76bab`
**Test commit:** `4f03457a31ddaab9da4b72db655e98c743c7c3fa`
**Design docs commit:** `3760961c02f25569f9e27a6e1d5f71b13dc18498`
**Active PR:** https://github.com/chromealex/AIVERSE/pull/26
**Main sync:** Only `main` was previously merged into this branch, GitHub PR #31, SHA `29dd5cd366b45e400c58f91c598d7559fa0613a0`; no other feature branch cherry-picked.

## Implemented

- `engine/render/Prisma.cpp` includes the new `PrismaRhiExecutor.h` opt-in path inside the existing SDR FXAA prepass. With env `AIVERSE_PRISMA_GRAPH_FXAA=1`, builds a small graph with two external RGBA8 resources and one Raster pass; compiles the graph and executes the **existing** FXAA shader/pipeline, vertex buffer, uniform data and source texture through `PrismaRhiExecutor::execute`. Uses the same per-size color/depth allocation and single draw as the old implementation.
- If opt-in is absent, the prepass remains the direct legacy `draw()` call. If graph construction, preflight or execution fails, fallback is `draw()`; graph error is retained in `lastError()` if no existing error. The graph is only invoked when the existing FXAA prepass would run, SDR only, with a valid target and depth.
- Updates `stats_.passes` by one for a successful graph FXAA draw; direct pass already increments it. No duplicate rendering on successful pass.
- `tests/render/prisma_graph.cpp` adds `fxaaGraphContract()`: SSA external scene/output RGBA8, no internal physical slots, valid compiled order, binding resolution and a single callback invocation.
- `docs/prisma.md` records the flag, exact scope and limitations.

## Public contract and unaffected functionality

**Opt-in:** `AIVERSE_PRISMA_GRAPH_FXAA=1`. Default OFF. No serialized format change: `postfx`, `postfx_*` MCP commands, editor window ID and shader graph unchanged. No new render targets or history allocations in Stage 14: the runtime passes existing `Sized::targets` to an empty-slot graph. All other effects, HDR, final composite and UI ordering retain their existing pipeline.

PRISMA remains genre-independent and distinct from SpectraFX. No authoritative gameplay ECS was modified.

## Verification actually performed

- **Done:** GitHub connector fetched/reviewed Stage 13 adapter, `Prisma.cpp` control flow, and wrote source+test+docs commits.
- **NOT RUN:** CMake/clang, `ctest`, CI, Metal or GL/GLES shader validation, automated screenshots, numeric readback comparison, editor/scene/game view visual smoke, GPU timing. This is an opt-in experimental code path, **not** proven image parity. Do not mark PASS from source inspection.
- **Risk:** current `PrismaRhiExecutor` wraps raster callback with `beginPass/endPass`; the fallback only addresses a false return and cannot undo already-submitted draw commands. Current FXAA callback always returns true after draw. Metal depth attachment compatibility and no-clear full-screen coverage require GPU validation. Pass target and input bindings are identical to the original path, but clear policy differs (old `draw()` clears before full-screen rendering). Per-view multiple sizes and window resizing remain to be verified. Header-only RHI adapter is not independently unit-tested with a fake `gfx::Device`.

## Next precise actions for Stage 15

1. On macOS checkout: `git fetch origin main feature/prisma-postfx && git switch feature/prisma-postfx && git pull --ff-only`.
2. Configure: `cmake -S . -B build/prisma -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_HDR_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON`; build `aiverse_prisma_graph_tests aiverse_prisma_volume_tests aiverse_hdr_policy aiverse_editor`.
3. Run `ctest --test-dir build/prisma --output-on-failure -R 'prisma_graph|prisma_volume|hdr_policy'` and `python3 tools/skills/catalog.py check`. Fix actual compilation/regression failures first.
4. On Metal and GL/GLES capture **the same** SDR FXAA scene with flag unset and set to 1. Compare RGBA8 readback with tolerance and check frame hash, source flip, alpha, transparent edges, viewport resize and Scene/Game view dual-camera behavior. Also check no-FXAA cases remain byte-identical (flag ignored).
5. Build a fake RHI test covering pass balancing and failure paths. Once parity passes, migrate next single effect pass (e.g. bloom prefilter) behind its own flag; do not turn on the full graph or replace final composite prematurely.
6. Record exact commands and results in `docs/handoffs/prisma-stage-15.md`; keep PR #26 draft until verified.

**Important:** No compiled/GPU validation is available through GitHub connector alone. Never advertise this work as tested on Metal or shipping enabled.
