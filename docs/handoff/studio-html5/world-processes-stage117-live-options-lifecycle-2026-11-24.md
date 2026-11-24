# Stage 117 — shared Live settings and snapshot lifecycle
Date: 2026-11-24. Owner: World Processes. Branch: feature/world-processes-stage31-bidirectional-handles.
Base: c9fe1f16ca62bf69a5de5f9235778866e1fa8cc7. Plan: 03d3317c00c61f233f4513a991130f74de7bfac3. Implementation: 2925bed6e496f1e5a4df8f126d15f55f820d01a1, 93313c89266233a393b11fbd88865539572b35db.

## Delivered
- apps/studio-native/StudioWorldProcessLiveOptions.h: one transient value with atomic typed patch validation, defaults and RPC serialization. Partial updates preserve unrelated settings; explicit invalid layer strings/types fail closed. No-op patches do not rebuild.
- apps/studio-native/StudioGameSurface.h/.cpp: named sampling options instead of positional flags; cheap planet-match query and current-epoch/local-control snapshot check.
- apps/studio-native/StudioHost.h/.cpp: use the common options for RPC, sampling and rendering; clear stale snapshots before and after game frame, at project open, restart and checkpoint restore; reset sample timer when invalidated. Planetary Live sampling is suppressed outside the original saved Play-current-map project/path/revision and when map is dirty. Browser cached read uses the same guard. Live disabled retains existing default browser diagnostics; planar owner/sampling unchanged. Menu-launched planetary matches are not eligible for the current-map Live overlay.
- tests/world_processes_gamecore_native_test.cpp: source fixture for defaults, partial patches, roundtrip, rejected values and no-op options.

## Verification
- GitHub reads and branch lease writes succeeded. Source inspection confirms old positional flags removed from the two Host calls. No compiler, CTest, Studio/browser typecheck, actual RPC, interaction or visual capture executed (owner deferred tests). No claim of runtime parity.
- Architecture: presentation-only; no added ECS authority, persistence schema, dependencies or solver.
- Native API/skill: gameplay library API unchanged; internal Studio helper signature changed. Existing skill and compatibility docs updated.
- MCP/Inspector: existing transient Studio RPC keys retained; no new mutation or component schema. Registry/interaction NOT RUN.
- Persistence/graphs/headless: no new persistence or graph; regression NOT RUN.
- Performance/platforms: periodic sampling cadence retained (250ms); invalid map avoids planetary sampler, but aggregate CPU/JSON/line measurements NOT RUN.
- Examples/tests: existing native fixture extended; NOT RUN.
- Publishing: capture and release acceptance NOT RUN.
- Main sync/merge NOT RUN. At initial inspection main was one commit ahead of the merge base (site/index.html and site/ru/index.html only); branch contains 960 commits beyond merge base. Preserve ongoing Studio ownership and review against studio-html5 at integration.

## Next
Stage 118: introduce an aggregate planetary diagnostic line/payload budget across all enabled layers, with explicit truncation reporting and deterministic handling. Then reconcile shared Studio paths and address physical owner integration gaps (terrain mutation, GRANULAR physical scale/render/collider, EVOLVE vegetation/weather, debris delivery, MORPH model stream and locomotion). Keep tests deferred until owner requests, then execute focused plus merged-tree Mac quick acceptance. Do not mark all gameplay features complete.
