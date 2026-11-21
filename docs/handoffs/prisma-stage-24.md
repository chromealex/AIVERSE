# PRISMA Stage 24 — camera-isolated HDR exposure and center-weighted metering

**Date:** 2026-11-19. **Branch:** `feature/prisma-postfx`. **PR:** https://github.com/chromealex/AIVERSE/pull/26. **Previous handoff:** `docs/handoffs/prisma-stage-23.md`.
No upstream merge or other feature branches imported; `main` not modified.

## Code commits / changed files

- `4a532fd4d44d1bd3d19bba62ca78293e13a9a109` — `engine/render/Prisma.h`: stable `setViewId(uint64_t)`/`viewId()` plus `resetAutoExposure(uint64_t)`; per-view history state moved out of the size-keyed `Sized` into `map<uint64_t,ExposureView>`.
- `0c3ae3d077e0d1df3aaba68c55a0138c8ba5f8bf` — `engine/render/Prisma.cpp`: each view creates/owns two distinct RGBA16F 1x1 history textures and tracks its previous time, index and validity; shutdown destroys them. All-view or single-view history invalidation, legacy default ID 0.
- `b86e8f0b8bbca8d015fdb26d35fcbe64ef1c5be2` — `apps/editor/Editor.cpp`: Scene uses stable ID 1, Game uses stable ID 2 before applying PRISMA volumes/render.
- `e8a0275d8b4c8340f7a800845226fd58d49c8444` — `apps/client/GameClient.cpp`: client camera sets ID 3 before PRISMA rendering.
- `7f7db08d2aa43d7d5d75601c71441ff7716169fb` — `engine/render/PrismaData.cpp`: new metering parameters centerWeight, centerRadius, minMeterLogL, maxMeterLogL in existing autoExposure catalog.
- `df97e43066ff6ef6e270aa01890216613d4b2070` — `engine/render/PrismaShaders.h`: GPU measurement writes weighted log-luminance sum in R and total weight in G, reduction averages both, adaptation divides to recover weighted mean log luminance.
- `be78959ec5bd19e5453b28a49a20f49cf4800898` — `engine/render/Prisma.cpp`: first log-luminance pass receives center weighting and minimum/maximum metering log clamps in SimpleU; 1x1 special case also receives these settings.
- `acb07465e8584a98066e17e1ae93b2671fb74805` — `docs/prisma.md`: Stage 24 API, settings, limits and usage example.

## Public interface and ownership

A native caller with more than one camera should use `prisma.setViewId(stableId)` **before** rendering that camera. The identifier must remain constant across frames for that camera; ID 0 remains the legacy default. To clear a camera after a cut or teleport call `prisma.resetAutoExposure(stableId)`. `resetAutoExposure()` invalidates every camera's exposure without immediately releasing its textures.

Scene/Game/editor IDs are local to their own `Prisma` object. Ordinary size-keyed scene targets remain unchanged; only *temporal exposure* history was separated by camera ID. Two different cameras at the same viewport resolution no longer consume one another's 1x1 history targets, time deltas, or validity.

AutoExposure `postfx` entry accepts `centerWeight` (0=uniform, 1=center biased), `centerRadius` (normalized width), `minMeterLogL` and `maxMeterLogL` (per-sample log2 luminance clamp). Existing values are backward compatible and keep uniform measurement by default. `AIVERSE_PRISMA_AUTO_EXPOSURE=1` is still required and HDR/High quality gating stays unchanged. The weighted average of log luminance is NOT a histogram or exact percentile calculation.

No gameplay ECS, SpectraFX or per-genre logic was added. Shader inputs stay inside the existing RGB/alpha HDR pipeline and UI remains post-composite.

## Actual verification and limitations

**Performed:** read connected GitHub code for render, editor/client call sites, ShaderData and shaders; wrote commits and docs via GitHub connector.

**NOT RUN:** compiler/CMake, CTest, GPU shader compilers/Metal/GL execution, screenshots, CI, performance profiling, editor smoke, as explicitly requested by user (features before tests). The code is *not proven* to compile or run on supported graphics devices.

Known limitations: history entries are retained for the life of `Prisma` (no per-view release API or backend fence-completion retire hook); stable IDs are supplied by host, not derived from an ECS camera entity automatically; hardware shader text for both R and G channels requires native validation. Current weighting is Gaussian in screen UV, and min/max log clamping prevents unbounded outliers but does not implement percentile histogram trimming. The exposure luminance pyramid has the same sample/edge limitations as Stage 23.

## Next features / precise handoff

1. Add an explicit `releaseView(id)` or GPU-completion-safe history retirement contract and lifecycle linkage for closing/replacing editor panels without freeing in-flight textures. Avoid CPU frame index as a GPU fence.
2. Implement advanced metering alternatives (spot, mask texture or genuine histogram percentile) as a separate stage, preserving UI/ShaderGraph slot ownership.
3. Move on to camera/object motion vectors and Motion Blur via read-only presentation API with camera-cut reset; do not modify deterministic ECS state.
4. When user allows tests: build `aiverse_prisma_graph_tests`/editor/client, compare two same-resolution cameras with independently adapted histories, check weighted metering transitions and HDR/GL/Metal, then update handoff with actual PASS/FAIL results.
5. Create `docs/handoffs/prisma-stage-25.md` after the next stage, with exact SHAs, tested vs untested status and next action. Do not merge to `main` while unverified.
