# Stage 110 — planetary live AETHER diagnostics
Date: 2026-11-24. Branch: feature/world-processes-stage31-bidirectional-handles.
Base: b96c71af7c33436e32044eca93ab0c6996da24f0. Plan: 394f9b841e60c1321e7a92d0d05fa3a2f27b733d. Native implementation: c3febc0ebe480a4bb2b01be49ccc37d22330c6e7. Browser/fixture implementation: 928e7fb0fdde3a35299cf01a7f0c2b56b03787cc.

## Delivered
- engine/game/Native.h: read-only loaded immutable module config accessor.
- libraries/include/aiverselib/world/AetherPhysisStratification.h: shared signed effectiveHead policy, no new transport.
- apps/studio-native/StudioPlanetAetherSnapshot.h: actual mass, configured rates, same-body topology and isolated potential.
- apps/studio-native/StudioGameSurface.h/.cpp, StudioHost.h/.cpp, StudioScene.h/.cpp: existing local Live bridge, validated species selector, native markers/arrows.
- apps/studio-web/src/types.ts, mock.ts, panels/SceneBrushBar.tsx: typed layer setting and diagnostic summaries.
- tests/world_processes_gamecore_native_test.cpp: regression fixture source.

## Verification gate
Implementation and architecture: committed source, owner ECS/transport and renderer reused. Native compilation NOT RUN.
Native API: read-only config accessor and shared head extraction; no new persistent state.
Skills/docs: existing recipes and compatibility guide updated; no new catalog ID.
Studio/Inspector/MCP: existing transient read-only RPCs extended, no authoring mutation or new MCP tool. Typecheck, interaction and RPC tests NOT RUN.
Persistence/graphs: no schema or new graph, existing directed GasDuct owner retained. Headless regression NOT RUN.
Performance/platforms: explicit bounds; actual CPU/GPU measurements and all platform runs NOT RUN.
Examples/tests: source fixture extended; no test command executed.
Publishing: captures/release evidence NOT RUN.
Integration: no main sync/merge; shared Studio and Native.h paths require integration review.

## Stage 110: planetary live AETHER gas and duct diagnostics

During local Play current map, enable Live 3D overlay and select Smoke, Toxin or Oxygen in the independent AETHER layer. Scene renders actual body-local GasCell Transform positions as species-colored markers with logarithmic mass display size. Same-body enabled ducts are lines, colored arrows show the signed isolated-duct transfer potential from current completed-tick masses. They are not a measurement of previous-tick transfers; actual edge transfer history is not retained by the owner. Grey ducts have zero or unavailable potential. Markers/ducts are a point-and-link diagnostic, not a filled gas volume or a CFD rendering. Game-quantized equal-volume masses are not physical concentration units.

The native host exposes a read-only copy of the loaded module configuration through NativeModules::loadedConfiguration; Studio identifies the stock world_processes diffusion or default PHYSIS stratification policy, with unknown policy disabling potential. AETHER's existing stratification solver and diagnostic reuse the exact same effectiveHead helper (including stable endpoint orientation, optional co-located temperature and gravity projection). Potential is the isolated half-head capped by configured duct rate and donor mass; reverse flow is blocked by one-way ducts. Competition among donors/ducts, later source/sink application and next-tick changes are not predicted. Configured source/sink rates are separate from actual cell mass. No extra simulation tick, mutation, persistence, forces or competing solver.

Read-only snapshot bounds: owner component budgets, 256 sampled cells, 512 sampled ducts, max 1024 gravity components for stratified potential; full-generation IDs, stable order, local-body checks, stale/disabled/invalid or unplaced cells skipped, cross-body ducts counted but not drawn. Unknown policy or gravity cap preserves mass/topology readback without inventing potentials. Browser shows first eight sampled cells and ducts, full sampled counts, rejected/omitted counts, mode, source rate and signed species potentials/bias. Existing map/project/revision guards, local Play restriction and renderer reused; planar ecology unchanged.

Source fixtures cover two bodies, equal-mass diffusion, opposing stratification tendencies, one-way suppression, configured sources, stale/unknown/cap and unchanged ECS. Build, CTest, TypeScript typecheck, interactive UI, real GPU screenshots and performance NOT RUN. Next: FRACTURE support, bond state and pending collapse events; actual stress and per-duct historical transfer telemetry require owner outputs. No main merge.
