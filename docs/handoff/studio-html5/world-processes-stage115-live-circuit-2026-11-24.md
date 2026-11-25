# Stage 115 — planetary live CIRCUIT
Date: 2026-11-24. Branch: feature/world-processes-stage31-bidirectional-handles.
Base: 85688343e9871d5d6de15a4945a70b4dd352dd2f. Plan: 83b48d620a170bec20bd1353d56f13bebf314192. Implementation: 65c5986517bc1f27b8402a6bbb9d71b15ffaa7fa. Fixture: e87d26c9062f94bbb295ea43eb8766244d6c7d24.

## Delivered
- apps/studio-native/StudioPlanetCircuitSnapshot.h: bounded read-only actual power, signal, topology and device transition diagnostics.
- apps/studio-native/StudioGameSurface.h/.cpp, StudioHost.h/.cpp, StudioScene.cpp: independent validated Live setting and existing native diagnostic renderer.
- apps/studio-web/src/types.ts, mock.ts, panels/SceneBrushBar.tsx: typed controls and summaries.
- tests/world_processes_gamecore_native_test.cpp: source regression fixture.

## Verification gate
Implementation/architecture: source committed; existing ECS, power/logic and renderer owners reused. Native compilation NOT RUN.
Native API: existing CIRCUIT contracts unchanged; Studio-only helper.
Skill/docs: existing skills and compatibility guide updated.
Studio/Inspector/MCP: existing transient typed RPC extended; no new authoring mutation or schema. Typecheck, interaction and RPC tests NOT RUN.
Persistence/graphs: no new authority, graph or persistence schema. Headless regression NOT RUN.
Performance/platforms: bounded sampling; actual measurements and platform acceptance NOT RUN.
Dependencies: existing targets only; build NOT RUN.
Examples/tests: existing fixture extended; no test command executed.
Publishing: actual captures/release acceptance NOT RUN.
Integration: no main sync/merge; shared Studio paths require integration review.

## Stage 115: planetary live CIRCUIT power and logic diagnostics

Enable Live 3D overlay and the independent CIRCUIT power / logic checkbox during local Play current map. Read the existing ECS Node supply, demand, drive, powered and signal values; output availability requires exact completed-tick stamps. Switch input state, gate operation and references, device kind, requireSignal, actual active/valid and transition counter/changedTick are read from canonical components. Gate output means the actual destination Node output, not recomputation of its operation from current signals (the authoritative gate solver uses prior-tick inputs). Stale outputs are unknown, not falsely unpowered. No extra power allocation or logic evaluation.

Native Scene uses real Transform and canonical Surface body-local positions. Unplaced elements remain numeric-only. Explicit directed/bidirectional Wire topology and gate/switch/device bindings are shown only when endpoints share a body. Cross-body links remain numeric; no changes to authoritative topology. Yellow high signal, green powered/active, muted red unpowered/inactive, purple valid-input gate and grey unavailable/disabled markers. Green wire means both endpoints report powered, NOT measured current, allocation amount or electricity flow. Direction arrows show authored topology; dashed auxiliary lines are logical bindings.

Stable packed-ID ordering and owner component caps; maximum 256 nodes, 512 wires and 128 each gates, switches and devices per snapshot. Counts of powered/high/active and rejected/omitted/cross-body refer to sampled candidates. Existing local Play, map/revision and selected-body guards, typed transient settings and native renderer reused; flat ecology unchanged. Browser shows bounded diagnostic lists and actual output states. No second simulation, persistence, physical device actuation, wire creation or economy changes.

Source regression covers directed supply, deliberate cross-body logical power without spatial line, switch synchronization, one-tick NOT gate delay, device activation transitions, stale output and cap. Build, CTest, browser typecheck, interaction, actual captures and performance NOT RUN. Next SOCIETY live relationships, messages and NPC trigger diagnostics. No main merge.
