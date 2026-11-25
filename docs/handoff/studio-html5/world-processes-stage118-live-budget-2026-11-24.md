# Stage 118 — aggregate planetary Live diagnostic budgets
Date: 2026-11-24. Owner: World Processes. Branch: feature/world-processes-stage31-bidirectional-handles.
Base: 956993282de2791f36020e41f424b37cbdfb3988. Plan: e11fe1eec5822d3983747d1c86a5d001e12c516f. Implementation: 25d7bb06e5d73ee0f32bb08a4835742807092250.

## Delivered
- apps/studio-native/StudioPlanetLiveBudget.h: shared 768 KiB compact snapshot budget including 4 KiB root reserve, divided equally by requested sample layers; deterministic round-robin complete-row prefixes across top-level arrays, precise compact serialization size check, transport-trimmed and omitted layer reports. Pre-transport counters intentionally retain sampled meaning. Nested rows are atomic (including whole PHYSIS body records). Unused allocations are not redistributed.
- apps/studio-native/StudioGameSurface.cpp: proportional owner sample row caps across concurrently requested layers (single layer retains its owner cap), including CROWD route points. Live layer off skips IMPRINT/EVOLVE; default browser-only snapshot still samples both. Gravity retains canonical full field and face safety caps. No change to ECS scheduling or gameplay entity limits.
- apps/studio-native/StudioPlanetEchoSnapshot.h: explicit emitter row cap, default 256, consistent with other ECHO row families.
- apps/studio-native/StudioScene.cpp: 4096 Live line submissions maximum per frame, equally divided among drawable Live layers. Authored gizmos, selection and links remain outside this budget. Explicit status if lines or payload are capped. Segment-level cutoff can leave a partial glyph; this is read-only diagnostic geometry, not physical topology.
- apps/studio-web/src/types.ts and panels/SceneBrushBar.tsx: typed aggregate budget, transport-trimmed/omitted names and explanation that counters refer to sampled candidates.
- tests/world_processes_gamecore_native_test.cpp: source regression for line cap, share, deterministic complete-row transport trimming, retained counters and too-small payload.

## Verification and limitations
GitHub source read/write and branch lease succeeded. Source reviewed all eleven line branches and compact JSON serializer separators. Build, CTest, browser typecheck, UI interaction, actual RPC payload size, Metal/GL/Vulkan captures, CPU measurements and merged-tree Mac quick NOT RUN (owner deferral). Payload limit applies to compact snapshot, not enclosing RPC or JS escaping. Owner scans/sorts and PHYSIS queries remain subject to their own limits, not a measured time budget; no hard CPU/frame-time claim. Renderer upload buffer capacity/growth and total authored+Live line volume require actual backend validation; no GPU safety claim from submission cap alone. Fixed presentation budgets are not gameplay limitations and can be tuned after profiling. 

Architecture/dependencies: header-only Studio presentation helper; no new target, solver, component, persistence, graph or runtime owner. Native gameplay API unchanged. Studio RPC response gains optional diagnosticBudget; existing fields retain type. No new MCP mutation or inspector authoring. Skill and compatibility docs updated. Headless/persistence regression, actual platform and publishing acceptance NOT RUN. No main sync/merge; shared Studio reconciliation pending.

## Next
Stage 119: compare shared Studio HTML5 changes with current studio-html5 and identify a safe integration boundary; preserve both agents' work and do not merge main without gate. Then implement outstanding physical owner integrations: planetary terrain mutation, GRANULAR scale/render/collider, EVOLVE vegetation/weather, FRACTURE debris, MORPH model streams, physical locomotion. Tests deferred until owner requests; all source-only.
