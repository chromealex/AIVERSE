# PRISMA Stage 20 — Tilt Shift RenderGraph feature

Date 2026-11-18. Branch `feature/prisma-postfx`, draft PR #26. Prior stage: `docs/handoffs/prisma-stage-19.md`.
Implementation SHA `23fc5d58256b303ddabb0a439507cc15a72bf95d`; docs SHA `f69b505a1b0f4e4a3bd4a564df2aed54da6b0aca`.

## Implemented
`engine/render/Prisma.cpp`: experimental `AIVERSE_PRISMA_GRAPH_TILT_SHIFT=1` flag. Tilt Shift downsample -> horizontal blur -> vertical blur use one three-pass graph and the persistent per-viewport executor. The old A/B texture ping-pong is preserved through distinct logical graph resources with the same external physical A handle at non-overlapping lifetimes. Uses the existing kDown/kBlur programs, matching SimpleU settings, same depth target and pipeline layout. `stats_.passes` counts completed graph passes and final `tiltTex` remains the physical A target. On preflight error it falls back to existing draws; after any partial submission the graph is not replayed. No changes to game JSON, public postfx commands, ShaderGraph, SpectraFX or gameplay.

## Verification
GitHub connector source inspection and file commits only. **NOT RUN**: compiler, CMake, CTest, native Metal/GL, pixel parity, CI (user instructed features first and tests later). No claim of pass or production safety. The graph still compiles per active frame; per-view cache can be generalized later. Color targets remain external; default legacy path unchanged.

## Next
Continue Stage 21 Sun Shafts opt-in graph with prefilter/radial scattering, distinct external targets, and handoff. Then add shared graph topology caches and optional graph-owned targets, while ensuring GPU completion signaling before retiring in-flight resources. Keep tests postponed until requested and do not merge to main.
