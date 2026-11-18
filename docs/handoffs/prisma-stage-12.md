# PRISMA Stage 12 — GPU-agnostic physical resource pool

**Date:** 2026-11-18
**Branch:** `feature/prisma-postfx`
**Prior handoff:** `docs/handoffs/prisma-stage-11.md`
**Main integration:** PR #31 (main -> feature/prisma-postfx), merge SHA `29dd5cd366b45e400c58f91c598d7559fa0613a0`
**Stage 12 code SHA:** `be4698ce74328f09601e28ba0256ab6a84540f38`
**Stage 12 test SHA:** `a4459645f3210eef4a4b5a5a40d8159856ffac02`
**Stage 12 design doc SHA:** `9d26472c66f28afcf83d9e7b3e37a98fd3cec407`
**Active feature PR:** https://github.com/chromealex/AIVERSE/pull/26

## Integration completed before implementation

Merged **only** the latest `main` into the PRISMA feature branch using GitHub PR #31.
The GitHub API returned merged=true and merge commit
`29dd5cd366b45e400c58f91c598d7559fa0613a0`.
A subsequent `compare_commits(base=main, head=feature/prisma-postfx)` returned
status=`ahead`, behind_by=0, confirming main is an ancestor of the feature branch
at the time of verification. No other feature branch was merged. The `main`
branch itself was not changed by these operations.

## Code and API

1. `engine/render/PrismaGraphResourcePool.h` (new, header-only):
   `PrismaGraphResourcePool<Handle>::prepare(graph,plan,create,release)`
   matches compiled physical graph slots to host-owned allocations (format,
   dimensions, history name); never reuses a single allocation for two different
   physical slots in one prepared frame.
2. `Result.slots` is directly usable as
   `PrismaGraphBindings<Handle>::slots` for Stage 11 dispatch.
   `Result.historyValid` tells the caller whether previous history pixels are
   safe to sample. History validity is false on newly allocated/resized targets.
3. `markHistoryWritten(resourceName)` explicitly confirms that a successful GPU
   frame wrote the temporal output; `invalidateHistory()` resets the valid bits
   for camera cuts/teleports while retaining compatible storage.
4. `pruneUnused(release)` and `clear(release)` release host resources; both
   **require GPU fence completion** before freeing in-flight targets.
5. Partial allocation failure releases only resources allocated during that
   failed `prepare` and leaves previously owned allocations intact. No graphics
   device calls occur inside this class.
6. `tests/render/prisma_graph.cpp`: extended existing CTest `prisma_graph`
   with cross-frame reuse, validity after successful write, camera cut reset,
   resize, old-size release, partial allocation rollback and final teardown.
7. `docs/prisma.md`: added Stage 12 design and limitations.

## Compatibility and ownership

No JSON/schema/MCP/editor window changes. Existing `postfx` and PRISMA
RHI fixed chain untouched; no SpectraFX, ECS or FPS/RTS dependencies.
An existing renderer must explicitly opt in to the new resource pool.

The host remains responsible for all actual RHI allocation, resource barriers,
GPU command encoder scope, render target clears, async GPU fences and output
history state. A separate pool is required for each camera/viewport whose
temporal history should remain independent. Avoid sampling a history texture
when `historyValid=false` even if its handle is nonzero.

## Actual verification status

- **GitHub actions actually performed:** code file added, tests modified,
  documentation updated, merge of `main` confirmed through GitHub PR #31;
  compare API confirmed main ancestry at the check point.
- **NOT RUN:** C++ compile, CMake, CTest, `./run-tests.sh`, macOS Metal,
  GL/GLES GPU smoke, editor Scene/Game screenshots, CI green verification.
  Tool environment has no native AIVERSE checkout and container cannot
  resolve github.com. Do not imply test sources mean passing tests.
- **Current limitation:** the pool does not track multiple in-flight frames.
  Caller must fence before `pruneUnused` and `clear`; a future GPU host adapter
  should retain retired allocations until safe. No runtime integration or
  performance gains have been measured.
- **Source review needed:** ensure `prisma_graph` compile passes with project's
  exact compiler flags, and check runtime API types on real `gfx::Device`.

## Next actionable step (Stage 13)

1. Native checkout: `git fetch origin main feature/prisma-postfx && git switch feature/prisma-postfx && git pull --ff-only`.
2. Configure: `cmake -S . -B build/prisma -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_HDR_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON`.
3. Build: `cmake --build build/prisma --target aiverse_prisma_graph_tests aiverse_prisma_volume_tests aiverse_hdr_policy aiverse_editor -j4`.
4. Test: `ctest --test-dir build/prisma --output-on-failure -R 'prisma_graph|prisma_volume|hdr_policy'`; `python3 tools/skills/catalog.py check`.
5. Fix every build or unit-test regression and record exact commands/output.
6. Add opt-in RHI integration with real texture ownership and transition/barrier
   policy, per-view pool and history invalidation. Verify Metal/GL numerical
   and screenshot parity with existing PRISMA pipeline before default enable.
7. Write a separate `docs/handoffs/prisma-stage-13.md` after the next bounded
   code change; preserve other agents' work and do not force push.
