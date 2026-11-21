# PRISMA Stage 16 — complete Bloom graph execution (experimental)

**Date:** 2026-11-18. **Working branch:** `feature/prisma-postfx`. **PR:** https://github.com/chromealex/AIVERSE/pull/26
**Previous handoff:** `docs/handoffs/prisma-stage-15.md`
**Upstream integration inherited:** only `main` was merged previously into this branch through PR #31 (`29dd5cd366b45e400c58f91c598d7559fa0613a0`). No other feature branches merged in this stage.

## Commits, files

- `d7de052d8dc1dc6d94e32f3de848aa6833522811` — `engine/render/PrismaRhiExecutor.h`: optional per-pass raster depth attachment resolver; existing executor callers retain their shared-depth behavior.
- `e786ec2b938957a16b45416d04c8a9ed3e30591b` — `engine/render/Prisma.cpp`: whole Bloom chain through opt-in graph, separate target per mip and per-pass depth, original RHI pipelines and uniform values.
- `9cac7d78aa413219fb6c4950735ab615fd7f475f` — `tests/render/prisma_graph.cpp`: graph regression source for 1/2/4 mip levels, SDR/HDR, dependency order, resource bindings, preflight rejection.
- `341e0e9754669551884d831497daf7acfe2453a2` — `tests/render/prisma_graph.cpp`: unique pass names for multiple down/up stages (required by SSA graph validation).
- `18a5dea3d807615bf35524a841dd3d9dec1df2e0` — `docs/prisma.md`: Stage 16 design, fallback and technical limits.

## Implementation and API

1. Set environment variable `AIVERSE_PRISMA_GRAPH_BLOOM_CHAIN=1` before launching the client/editor to opt into the entire Bloom mip chain. By default the legacy prefilter/downsample/upsample code path is unchanged.
2. Build `PrismaRenderGraph` with one external source (the scene / FXAA image), every Bloom down target, every Bloom up target and explicit pass reads/writes. This preserves the dependency order and uses a single execution call. The SSA graph planner rejects in-place feedback.
3. All image targets remain in `Prisma::Sized::targets` (per-view, already allocated/reused by legacy code), passed as external RHI resource handles. **No graph-owned internal allocations are used yet; Stage 12 pool is not integrated into runtime Bloom**. Therefore no new lifetime/retirement race is introduced by this opt-in path, but pooling improvements are also not claimed.
4. Each graph pass invokes the same `kPrefilter`, `kDown`, or `kUp` pipeline and `SimpleU` block as the old chain: matching source/destination sizes, threshold/knee, scatter, uniforms, texture slots, Y-flip and clear semantics.
5. `PrismaRhiExecutor::execute` now has final optional `rasterDepthForPass` callback, returning appropriate depth attachment for the specific mip output. The host prevalidates all required depth/colour texture handles before dispatch. The executor still opens and closes one RHI raster pass per graph pass and does not change the final composite/UI.
6. On graph success, PRISMA updates pass counters and the final bloom output/norm once. Preflight errors use the existing legacy path. If an unexpected dispatch failure happens after submitting passes, the code deliberately skips replay to avoid double GPU submission; the frame may omit Bloom and reports an error.
7. The Stage 15 prefilter-only experiment remains independently available via `AIVERSE_PRISMA_GRAPH_BLOOM_PREFILTER=1`; when Stage 16 graph runs successfully, it owns all Bloom passes and the Stage 15 path is bypassed. Existing FXAA opt-in is unaffected.

## Compatibility

No `postfx` JSON serialization changes, `postfx_*` MCP edits, editor windows, Shader Graph format, SpectraFX changes or gameplay ECS changes. PRISMA stays rendering-only and genre-independent. Full-chain flag OFF by default. No native code or engine API consumers are required to opt in.

## Real verification (DO NOT infer PASS)

**Performed:** inspected current `Prisma.cpp`, Stage 11–13 graph/dispatcher/RHI pool, Stage 15 handoff and regression source through GitHub connector; successfully committed all code, test source and design docs to `feature/prisma-postfx`. Performed source-level readback after editing.

**NOT RUN:** native C++ compile, `cmake`, `ctest`, macOS Metal, GL/GLES, screenshots, pixel parity, texture memory measurements, performance benchmarks, editor GUI, `run-tests.sh`, skill catalog. The environment has remote GitHub source access through connector but no native AIVERSE checkout or usable GPU device. All test additions are **unverified sources**.

## Technical risks, known limitations

- The full graph is rebuilt per opt-in frame; optimize/caching only after profiling.
- All mip images are still externally allocated by `Sized`; the Stage 12 pool is not wired to Bloom because GPU in-flight frame/resource retirement guarantees are not implemented across RHI backends.
- The RHI executor currently cannot prove that the caller's depth attachment dimensions match each pass output, only that all prepared handles are valid; Metal runtime validation still required.
- No fake `gfx::Device` tests or real GPU parity comparisons yet. The pre-existing Stage 13 API also lacks explicit barrier scheduling; its sequential `beginPass/endPass` is the only portable RHI lifetime boundary here.
- When the full graph is successful, it uses existing shader code and parameters but may still differ visually until actual Metal/GL screenshot comparison; do not claim shipping-ready equivalence.
- Existing `PrismaGraphResourcePool` and its history validity contract are independent. Do not release/invalidate graph resources while frames remain GPU-in-flight.

## Exact next actions (Stage 17)

1. Native checkout: `git fetch origin main feature/prisma-postfx && git switch feature/prisma-postfx && git pull --ff-only origin feature/prisma-postfx`.
2. Configure/build: `cmake -S . -B build/prisma -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_HDR_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON`; `cmake --build build/prisma --target aiverse_prisma_graph_tests aiverse_prisma_volume_tests aiverse_hdr_policy aiverse_editor -j4`.
3. Run `ctest --test-dir build/prisma --output-on-failure -R 'prisma_graph|prisma_volume|hdr_policy'`, `python3 tools/skills/catalog.py check`, `./run-tests.sh --quick`; fix failing code/compiler issues, record exact output.
4. Add real/fake gfx Device unit smoke for multiple raster mip sizes, correct depth attachments and clear state; ensure failures do not leave any encoder open. Check graph plans with one and several mips.
5. Run Metal + GL/GLES screenshots with Bloom chain flag unset and set, SDR/HDR, multiple mips, resize and parallel Scene/Game viewports; compare pixels with tolerances and check GPU timing/target allocations. Also test FXAA+Bloom combined flags.
6. Only after verified parity, add optional persistent per-view graph allocation/executor ownership and safe deferred RHI retirement (wait/fences), then integrate transient texture pool and measure memory improvement. Avoid replacing default pipeline before proven.
7. Write a separate `docs/handoffs/prisma-stage-17.md` with exact SHA, tests actually run, limitations and next implementation. Do not modify main or SpectraFX implicitly.
