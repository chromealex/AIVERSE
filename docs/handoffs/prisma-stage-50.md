# PRISMA Stage 50 — SpectraFX, Terrain, Planet and WorldScene GPU owners; native TAA admission

Date: 2026-11-19.
Repository: chromealex/AIVERSE.
Branch: feature/prisma-postfx.
Draft PR: https://github.com/chromealex/AIVERSE/pull/26, base feature/universal-entity-inspector-fps.
Previous handoff: docs/handoffs/prisma-stage-49.md.
Main: unchanged. Do not merge without explicit approval.
User mandate: complete functionality first; native build, CTest and GPU runtime tests only after features are finished.

## Implementation and exact commits

1. 9701155bc0bbe339261c46e60d3dec0bfe2ca806 — engine/fx/ParticleSystem.h: public ownedGpuMemoryStats, intentionally excluding texture/mesh/graph inputs borrowed from external owners.
2. 4254d1c374a5849f042b61cf7e15b4e4c7cd8a6f — engine/fx/ParticleSystem.cpp: metadata-only owned dynamic particle VB, static IB and builtin texture atlas.
3. 18ee711ed3e737734ac0e65ad2fb2812f02c07ec — apps/client/ClientFx.h: aggregate ParticleSystem core buffers/atlas plus its independent TextureCache effect sprite/atlas owner, no borrowed batch texture duplication.
4. 20c2966a3852e63d38dd894294ccfce857119f70 — modules/rts/view/RtsTerrainRender.h: count all terrain chunk/LOD VBO/IBO, water/apron geometry, cell/vision/splat/heightfield/detail textures and independent terrain material TextureCache.
5. cad0abeabba52f12d7acad51504dc0f40c00554b — modules/rts/view/RtsPlanetRender.h: count resident planet body chunk buffers, vision/detail atlases, globe/atmosphere geometry and planet-owned material texture cache.
6. e2bb4a7e989c361a798eb9661acbd076abaac737 and deb30b60dac7172d530ac8f5739adf8b527e27ef — modules/rts/view/RtsView.h: ownedPresentationGpuMemoryStats includes previously deduplicated Model/ModelTextureCache plus owned terrain/planet owners, even if allocations are hidden but live.
7. a011fe3930e9df8047c87baaf76c98cc2a5fc031 — engine/world/WorldScene.h: public resident streamed-world GPU memory report.
8. 17e09186ba5b1d4a3d4ab620e728c4c54ff8ca38 — engine/world/WorldScene.cpp: enumerate resident models with shared-handle deduplication across cells; intentionally exclude the external shared ModelTextureCache (GameClient passes view_.textureCache()) and GlobalGeometryPool pages, already counted by their separate owners.
9. dd7e967b3a7efc1e5addcb9e536070c20a33c1c9 — apps/editor/Editor.cpp: submit unique RtsView models/terrain/planet, separate gameFx_ particle buffers, and fxTextures_ texture owner to Prisma before Scene/Game renderSizeForOutput.
10. 4e4a20d171afa89463e5005c1a4848e33ee12cc4 — apps/client/GameClient.cpp: supply RtsView presentation assets + ClientFx-owned particles/sprites + optional WorldScene resident models before the native view render-size decision.
11. 9e493e896683ea42c70b17beae7bebe22234730c — engine/render/Prisma.cpp: fix temporal-history admission for ALL native HDR temporal paths, even without temporal upscale or jitter. For native 1:1 jitter, preflight output history, scene/depth/velocity/scratch and fallback shader pipelines before applying projection jitter. A denied/failed preflight is checked in jitteredCamera and again in Prisma::resolve, never silently reallocating rejected history after rendering. Existing retry cooldown and GPU-fenced retirement preserved.
12. 08d7eb40d4752b9859f0a816bf75a1a4c60bde38 — apps/editor/windows/StatsWindow.cpp: show RtsView model+terrain+planet and SpectraFX particle-owned memory separately.
13. 48b10ec7eb85c364ea5982eeeb48dfda821ab11a — skills/visuals/aiverse-prisma/SKILL.md: external owner API and native Temporal Resolve fallback.
14. 1ad3a28f748590cf8ada1ec288960cdac11a1f67 — docs/prisma.md: Stage 50 architecture, preserved ownership boundaries and precise limits.
15. This file — docs/handoffs/prisma-stage-50.md, separately committed after code, skill/design documentation and source inspection.

## Architecture, API and ownership boundaries

The shared engine/render/GpuOwnedMemory.h accumulator performs read-only RHI textureInfo/bufferInfo lookups and counts unique typed GPU handles from each true allocation owner. It never allocates, reads back, waits for GPU or destroys handles. All bytes are known lower bounds, not physical VRAM or a hard memory budget.

ParticleSystem owns only VB/IB and its builtin atlas. Its ShaderGraph and imported effect textures are borrowed; ClientFx aggregates this owner and its independent TextureCache. The editor separately combines gameFx_.ownedGpuMemoryStats with fxTextures_.ownedGpuMemoryStats.

TerrainRenderer counts resident chunk LODs, water/apron geometry, cell/vision/detail/heightfield images and its private layer/cliff texture cache. PlanetRenderer counts body chunks and per-body maps, atmosphere/globe geometry and its private texture cache. RtsView combines those exclusive owners with its Model/ModelTextureCache from Stage 49. A temporarily hidden but allocated terrain/planet remains resident and therefore still counts.

WorldScene owns resident chunk model instances; duplicate GPU handles/models shared across chunks are deduplicated. WorldScene can borrow a ModelTextureCache owned by RtsView, so it never adds that shared cache again. The global geometry pool remains a separate once-per-device owner. Renderer3D/LightSystem/persistent GPU buffers remain owners from Stages 47–49. Avoid summing any borrower references as independent allocations.

Editor and native GameClient use saturating sums of their exclusive owner reports and call Prisma::setExternalPresentationAssetsGpuMemory before renderSizeForOutput. Existing setExternalRendererGpuMemory and setExternalGeometryPoolGpuMemory stay separate. The quality budget is a *known lower-bound* pressure signal; it is not total VRAM.

## Native temporal-resolution fallback

Stage 49 only checked ViewQualityPolicy.maxTemporalHistoryMiB when upscaling was requested. Stage 50 moves the two RGBA16F output-size history quota before the upscaling/jitter feature gates, so even Stage 42 1:1 HDR TemporalResolve obeys admission. A rejected history is retired through existing GPU fences and is not allocated again during the same resolve.

When AIVERSE_PRISMA_JITTER=1 and the active effect wants native 1:1 temporal resolve, renderSizeForOutput invokes the same preflightUpscale(outputW,outputH,outputW,outputH) contract as the scaled path, ahead of Renderer3D::beginScene. A failed shader/resource preflight sets TemporalView.preflightRejected, which prevents the subsequent jittered camera and aborts native temporal history allocation during resolve. A stable full-resolution frame can still be drawn. This is GPU allocation planning, not an assertion of renderer correctness on actual devices.

## Actual verification

Performed: inspected connected GitHub branch, ParticleSystem/FxInternal resource ownership, ClientFx texture-loader/cache ownership, terrain/planet chunk and texture lifecycle, RtsView presentation ownership, WorldScene resident/shared model lifecycle, Prisma preflight/handoff and editor/native call sites. Committed source and docs changes, read back 14 relevant files and confirmed **53/53 structural source-presence checks**, including native history-admission before mode branching, preflight call presence, rejection guard in resolve and owner submission before render-size selection. These are not compiler or runtime tests.

NOT RUN under explicit features-first direction: CMake configure/build, C++/Obj-C++ compile, GLSL/MSL compilation, CTest, Metal/GL/GLES/Vulkan/WebGL execution, GPU device-loss tests, image comparisons, VRAM/latency benchmarks and CI. Never label such tests passed without actually running them.

## Next functional stage: Stage 51 (before any final test phase)

1. Add disjoint owner telemetry for remaining Rts2D/road/shore/fog/UI and other external render modules that allocate RHI resources; where ownership is unknown, report unknown rather than borrowing or guessing a handle.
2. Add explicit per-device hardware capability/fallback policy and diagnostics (requested vs enabled HDR, jitter, temporal history, velocity/reactive, unsupported counters and legacy PrismaScreenLighting conflict) without prematurely turning on features by default.
3. Confirm custom presentation host view IDs cleanly retire per-view policy/history and never cross-contaminate when a viewport is hidden/destroyed. Do not invent GPU completion; always use backend fences.
4. Finalize the remaining user-requested general-purpose Prisma functional catalog and editor/agent documentation. Continue feature work first; only after features are complete transition to a dedicated build/GPU validation and fixes phase.
5. Each stage requires separate docs/handoffs/prisma-stage-XX.md with exact commits, architecture, actual checks vs unrun tests, limitations, branch SHA and precise next task. PR #26 remains Draft; main must not be merged without approval.

## Mac continuation command including branch pull

    cd ~/Projects/AIVERSE
    git fetch origin
    git switch feature/prisma-postfx
    git pull --ff-only origin feature/prisma-postfx
