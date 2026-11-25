# Stage 161D3 — authored forest Scene selection and relocation
Date 2026-11-24. Owner World Processes / Studio. Branch feature/world-processes-stage31-bidirectional-handles.
Base 1fca0143528398aa26c5710a51d20a9adabee211; scope 78fff8539d3509e2f58ac7909b12574888694896; implementation 8fe51c2cb25b729216229cf791eeeef395c1b5bc and 6bd310b495907953c4c4c334ca56d91dee57ba15.

- StudioPlanetForestOverlays.h: bounded stable species/key pick records, authored ground/trunk projection and marker owner; label existing diagnostic lines by plant identity.
- StudioScene.h/.cpp: pick nearest projected trunk within 12 pixels, reject terrain-occluded points, hidden/locked marker owners, stale context and live overlay; selected diagnostic lines highlighted yellow with project/map/revision guard. This is trunk-line picking, not exact mesh picking.
- StudioPlanetForestDocument.h: native move by stable species/key, reject duplicate/missing identity, stale map revision and marker-backed plants; same full validator, single MapDoc transaction, no-op skips history. Marker-bound plants must explicitly convert to direct coordinates before relocation. No ECS writes.
- StudioSession.cpp, StudioHost.h/.cpp: typed studio.planetForestMove and shared one-shot arm pick/move action; plain click, cancel, document switch protection, input isolation, inspector readback. Existing native scene mouse route updated; separate platform paths still require verification. One-shot move via click on destination, not continuous drag.
- studio-web types/mock/PlanetForestPanel: pick and move controls, chosen identity, expand selected row, disable stale/dirty actions. Browser mock cannot fabricate native picks.
- StudioSessionTests.cpp: authored detached MapDoc fixture for direct move between bodies, stale/no-op/marker refusal and undo/redo. Source only; NOT RUN.
- docs/devlog/systems.json: in-progress roadmap.

Evidence: commits and source review only; C++/TypeScript/CTest/Metal, native pointer behavior, MCP coverage and save/reopen acceptance NOT RUN. KINGSTON Mac unavailable. No main merge, no publication. Native mutation is Studio RPC, not independently registered MCP operation; parity remains open. Current inspector is authored diagnostics; no textured meshes or drag preview. Performance of pick under large overlap not measured. Next: Stage 161D4 real textured ARBOR preview reuse native rendering owner, then end-to-end checks and MCP parity.
