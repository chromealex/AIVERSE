# Stage 113 — planetary live ECHO
Date: 2026-11-24. Branch: feature/world-processes-stage31-bidirectional-handles.
Base: 09ce16fce09b4dce78b3bd1aa60ff79fd5f72004. Plan: bb3ecaf7b2dc64ff73c9005ced38f9b083d208f7. Implementation: f20c6b91f35ab5d2dc5657c7bbe8e0aa10c3d83a. Fixture: 9561a2f624a6a6d5e7583208566f9b0df63a7c5e.

## Delivered
- apps/studio-native/StudioPlanetEchoSnapshot.h: bounded read-only acoustic graph and actual completed hearing observations.
- apps/studio-native/StudioGameSurface.h/.cpp, StudioHost.h/.cpp, StudioScene.cpp: independent validated Live toggle and existing native Scene diagnostic rendering.
- apps/studio-web/src/types.ts, mock.ts, panels/SceneBrushBar.tsx: typed controls and read-only summaries.
- tests/world_processes_gamecore_native_test.cpp: directed acoustic graph and hearing regression source.

## Verification gate
Implementation/architecture: committed source, existing ECS/propagation and native renderer reused. Native compilation NOT RUN.
Native API: existing ECHO contracts unchanged; Studio-only helper.
Skill/docs: existing skills and compatibility guide updated.
Studio/Inspector/MCP: existing transient RPC extended, no new authoring mutation or inspector schema. Typecheck, interaction and RPC tests NOT RUN.
Persistence/graphs: no new authority, schema or graph traversal; headless regression NOT RUN.
Performance/platforms: bounded sampling; actual measurements and platform acceptance NOT RUN.
Dependencies: existing targets only; build NOT RUN.
Examples/tests: existing native fixture extended; no command executed.
Publishing: actual captures/release acceptance NOT RUN.
Integration: no main sync/merge; shared Studio paths require integration review.

## Stage 113: planetary live ECHO acoustic graph and hearing

Enable Live 3D overlay and the independent ECHO acoustic graph checkbox during local Play current map. The snapshot reads actual ECS AcousticZone, Portal, Emitter, Receiver and AcousticHearing after a completed tick. Receiver hearing must have the exact completed tick and an in-range intensity; stale or missing outputs remain unavailable, not falsely silent. The reported winning emitter, intensity, threshold and validity come from the existing propagation output, never a second graph traversal. Zone/portal enabled state and loss are current configuration, not recorded path attenuation.

Native Scene uses actual Transform and canonical Surface body-local coordinates for zone, emitter and receiver markers. Unplaced nodes remain numeric-only; no fabricated spatial extent. Directed portal lines show enabled topology and configured loss with directional arrowheads; inactive portals are interrupted. Blue zones, amber active emitters, green audible receivers, muted red silent valid receivers, grey unavailable/inactive markers. Dashed green receiver-to-winner relation is not an acoustic route or a physical ray. Cross-body relations stay numeric and are not drawn; the existing logical ECHO graph is unchanged and may deliberately contain cross-body links. No physical acoustic volume, sound playback, dB conversion, audio propagation, or new persistence.

Stable packed-ID ordering and owner component limits; at most 256 zones, 512 portals, 256 emitters and 256 receivers per snapshot. Counts of unavailable/omitted/cross-body refer to sampled candidates. Existing local Play/map/revision guards, native renderer, typed transient settings and HTML5 summary are reused. Browser reports first eight emitters, receivers and portals with actual values. Planar ecology unchanged.

Source regression exercises directed topology, actual attenuation and threshold, cross-body logical propagation without geometry, unplaced receivers, stale hearing, disabled portal and sample cap. Build, CTest, browser typecheck, interactive UI, real captures and performance NOT RUN. Next: CROWD live movement and slot reservation diagnostics. Actual audio and physical/render integration remain separate owners; no main merge.
