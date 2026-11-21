# PRISMA Stage 21 — Sun Shafts RenderGraph feature

Date 2026-11-18. Branch `feature/prisma-postfx`; draft PR https://github.com/chromealex/AIVERSE/pull/26. Previous stage `docs/handoffs/prisma-stage-20.md`.

## Code changes and commits
- `8e582665ff06efcdb1c3c904ad08de9abaa58be2` — `engine/render/Prisma.cpp`: add `AIVERSE_PRISMA_GRAPH_SUN_SHAFTS=1` opt-in two-pass graph. Retain existing sun position, off-screen fallback, visibility gate, radial UV setup, uniforms, quality-tier sampling and color math. Declare source/mask/output as separate logical graph resources, backed by the same existing per-viewport textures. Dispatch prefilter and radial-scatter pipelines through the persistent per-view executor; preserve legacy direct draws if preflight fails. Do not replay partially submitted graph work. `stats_.passes` tracks graph passes.
- `7a924d6f7616f806ffc91c4d53eee636b9f8fe81` — `docs/prisma.md`: Stage 21 API and constraints.
- This handoff is an additional documentation-only commit on the same branch.

## External API / compatibility
Optional environment variable `AIVERSE_PRISMA_GRAPH_SUN_SHAFTS=1` is OFF by default. The same `postfx` JSON and existing MCP/editor identifiers remain. This pass is presentation-only and independent of ECS or game genre. SpectraFX remains the owner of scene emitters, PRISMA owns the screen-space sun shafts.

## Actual validation
GitHub connector: examined current `Prisma.cpp` Sun Shafts and Tilt Shift sections; wrote and read branch files as needed. **NOT RUN**, per user request to implement features first: CMake, native C++ compile, CTest, Metal/GL GPU execution, screenshots, timing, CI. Do not mark as passing.

## Known limitations / next stage
Sun Shafts graph recompiles per active frame; targets are existing externally allocated `Sized` textures, not internal graph allocations. No completed-fence backend contract is connected to Stage 18 retirement. No visual parity is established. Next function: factor out common cached small-pass graph topology for Tilt Shift/Sun Shafts and add optional graph-owned resources where GPU lifetime can be managed without invalidating in-flight frame data. Then implement real DoF, AutoExposure and motion-vector based effects separately, each with a handoff. Do not enable experimental paths as default before validation.
