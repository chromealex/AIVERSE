# PRISMA — handoff Stage 11: typed GPU-free graph dispatch

**Date:** 2026-11-18. **Branch:** `feature/prisma-postfx`.
**Prior handoff:** `docs/handoffs/prisma-stage-10.md`.
**Code commit:** `af82d368522eeb37cffe0acccdbe6264daf27e16`.
**Test commit:** `a5859e5657544eb351d6ff9114f60fed81bfa00c`.
**Documentation commit:** `94b457a7564311bc27800167a1d62e4da11cd77b`.
**Draft PR:** https://github.com/chromealex/AIVERSE/pull/26

## Implementation

1. Added `engine/render/PrismaGraphDispatch.h`: header-only presentation-only dispatcher using the compiled `PrismaRenderGraph` plan. `PrismaGraphBindings<Handle>` maps graph resources to external caller-managed handles and non-external graph resources to physical alias slots. `PrismaGraphDispatchView::resource(id)` resolves them without owning GPU resources.
2. `dispatchPrismaGraph(graph, plan, bindings, callback)` checks the plan and all bindings **before** executing any callback; runs each callback in deterministic compiled topological order. A false callback return aborts and reports executed pass count and failing pass name. The graph remains independent of FPS/RTS, ECS, gfx::Device and platform.
3. Extended `tests/render/prisma_graph.cpp` with positive topological dispatch, external/transient binding resolution, preflight failures for missing transient and output handles, callback early-stop and tampered plan duplication.
4. Updated `docs/prisma.md` Stage 11 contract. No CMake changes: tests are in pre-existing `prisma_graph` executable.

## API and compatibility

```cpp
#include "engine/render/PrismaRenderGraph.h"
#include "engine/render/PrismaGraphDispatch.h"

auto plan = graph.compile();
aiverse::render::PrismaGraphBindings<MyGpuHandle> bindings;
bindings.external.resize(graph.resources().size()); // host-managed external resources
bindings.slots.resize(plan.slots.size());             // physically allocated slot handles
const auto outcome = aiverse::render::dispatchPrismaGraph(
    graph, plan, bindings,
    [&](const auto& pass, auto passId, const auto& view) {
        // Host chooses raster/compute/copy encoders and binds view.resource(id).
        // Manage transitions, history and render-target lifetime outside dispatcher.
        return executePass(pass, passId, view);
    });
```

`Handle{}` is reserved as invalid. The host must fill **all** graph bindings before dispatch; a missing one produces an error rather than partial execution. The dispatcher does not allocate RHI textures, insert transitions or synchronize resource hazards. It assumes the `plan` came from this exact unchanged graph; it validates sizes, IDs and bindings, but does not recompile the whole graph or prove dataflow against a mutating graph. Keep the graph immutable between compile and dispatch.

No changes to `postfx` serialized JSON, `postfx_*` commands, `Prisma::resolve()`, the renderer's fixed pass implementation, SpectraFX, authoring/editor window or ECS.

## Actual checks and limitations

**Performed:** GitHub connector successfully created the header, updated the existing regression test and documentation on the same PRISMA branch; source-level review of existing graph API and CTest registration.

**NOT RUN:** C++ compiler, CMake, `ctest`, `./run-tests.sh`, macOS Metal, GL/GLES, editor, frame screenshots, full repository CI. A GitHub write is not a test PASS. Container network cannot resolve github.com and no checked-out repository is available locally.

**Risks:** current preflight insists all externally declared resources be bound even if unused by any pass (conservative); no robust plan fingerprint or GPU resource-state tracking yet; callback failure does not roll back already submitted GPU commands, so host must discard/repair the frame.

## Next agent — exact steps

1. Rebase/merge latest upstream cautiously; do not force push or overwrite concurrent PRISMA/SpectraFX work.
2. On native checkout: `cmake -S . -B build/prisma -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_HDR_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON`; `cmake --build build/prisma --target aiverse_prisma_graph_tests aiverse_prisma_volume_tests aiverse_hdr_policy aiverse_editor -j4`; `ctest --test-dir build/prisma --output-on-failure -R 'prisma_graph|prisma_volume|hdr_policy'`; `python3 tools/skills/catalog.py check`; record real outputs in Stage 12.
3. Add host-owned `PrismaGraphResourcePool` allocation/reuse with persistent history and size/format keys, and explicit per-pass access/transition declarations; unit-test alias lifetime and resize/camera cut cleanup. Keep GPU-free tests distinct from RHI smoke.
4. Only then bind an **opt-in** experimental GPU executor to existing Prisma passes (HDR, bloom, TAA/resolve), with actual Metal/GL screenshots and numerical parity before replacing fixed `Prisma::resolve`.
5. Add `docs/handoffs/prisma-stage-12.md` and code/tests/doc commits for the next bounded stage.
