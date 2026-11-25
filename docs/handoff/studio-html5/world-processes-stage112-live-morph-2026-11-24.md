# Stage 112 — planetary live MORPH
Date: 2026-11-24. Branch: feature/world-processes-stage31-bidirectional-handles.
Base: 7192a116762aaf2db789ce3a32320afb5974f166. Plan: 23df9d03fe8956ca809e176d5dab8b80cfccf602. Implementation: 2aca575513062f15c61b9e2bbce90ec400ed8772. Fixture: 5c9a0025a2fe4b4fb34fda583eb0d064c36a24d7.

## Delivered
- apps/studio-native/StudioPlanetMorphSnapshot.h: bounded read-only node, spring, force, impact and binding diagnostics; explicit owner frame projection and canonical gravity query.
- apps/studio-native/StudioGameSurface.h/.cpp, StudioHost.h/.cpp, StudioScene.cpp: independent validated Live toggle, existing local Play and native Scene line renderer.
- apps/studio-web/src/types.ts, mock.ts, panels/SceneBrushBar.tsx: typed diagnostics and controls.
- tests/world_processes_gamecore_native_test.cpp: frame, residual and pending impact regression source.

## Verification gate
Implementation/architecture: committed source, existing ECS/solver/frame and renderer reused. Native compilation NOT RUN.
Native API: existing public MORPH contracts unchanged, Studio-only read-only helper.
Skill/docs: existing skills and compatibility guide updated.
Studio/Inspector/MCP: existing transient typed RPC extended; no new authoring mutation or inspector schema. Typecheck, interaction and RPC tests NOT RUN.
Persistence/graphs: no new authority, schema or graph; headless regression NOT RUN.
Performance/platforms: bounded sampling; measurements and platform acceptance NOT RUN.
Dependencies: existing targets only; build NOT RUN.
Examples/tests: existing native fixture extended, no test command executed.
Publishing: real captures/release acceptance NOT RUN.
Integration: no main sync/merge; shared Studio paths require integration review.

## Stage 112: planetary live MORPH displacement diagnostics

Enable Live 3D overlay and the independent MORPH node displacement checkbox during local Play current map. Actual completed-tick Node values are read without advancing the solver. Quantized current/rest/displacement/velocity, configured enabled Force sum (not a saturated applied impulse), pending Impact, binding metadata and Spring stiffness/residual are returned. No second deformation state.

Space projection requires an explicit existing GravityBinding with a valid live owner and unitsPerMetre; the same MORPH ownerFrame and simulationVector used by PHYSIS loads project current and rest positions, validate body identity, then subtract the canonical body anchor. A disabled gravity binding can still provide its explicit frame/scale. Unbound or unresolved nodes remain numeric-only; do not guess a metre scale or attach their local coordinates to their own Transform. Spring geometry is only drawn for projected endpoints with the same owner, scale and body; raw solver residual remains visible for other sampled edges with a mismatch count. Amber displacement segments, grey rest markers, cyan anchored/green enabled/grey disabled nodes and purple stiffness-colored springs are diagnostic lines, not model vertex deformation.

For the loaded stock world_processes morphGravity mode, the existing physisNodeLoad resolver reports the current potential quantized gravity load on enabled nonanchored projected nodes, including relative/free-fall configuration. This is not a recorded prior-tick impulse. Unknown/disabled mode or gravity component cap suppresses load sampling; no gravity application or physics integration here. Same local Play/project/revision and selected-body guards as prior stages.

Stable packed-ID sampling: owner component budgets, 256 nodes, 512 springs, 1024 gravity fields for optional load. Counts of mismatch/rejection/omission refer to visited sample candidates. HTML5 reports the first eight nodes and springs and counts. Planar ecology unchanged. Existing MorphModelStream and Box3D remain separate owners; no actual visual mesh attachment, collider deformation or new persistence.

Source fixture covers shared frame/scale, body isolation, unbound nodes, exact residual, canonical gravity query, pending impact preservation, stale tick and cap. Build, CTest, TypeScript typecheck, interactive UI, GPU screenshots and performance NOT RUN. Next: ECHO live acoustic zones/portals/receiver audibility. Physical/render integration and shared Studio merge remain pending.
