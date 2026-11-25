# Stage 111 — planetary live FRACTURE
Date: 2026-11-24. Branch: feature/world-processes-stage31-bidirectional-handles.
Base: 343785a8fd7069571d8eafc274ae9c064ef41f8e. Plan: fcbc59c70d6f8cab042348f41e102b56d82ee1bf. Implementation: c6033069505a0f1a9b844ed8323b3fb283fac7d7. Fixture: 81bef90153d25a6b16750d64064db7f3869a2f40.

## Delivered
- apps/studio-native/StudioPlanetFractureSnapshot.h: bounded read-only member/bond state, optional existing PHYSIS stress and both outbox states.
- apps/studio-native/StudioGameSurface.h/.cpp, StudioHost.h/.cpp, StudioScene.cpp: independent validated Live toggle, existing local Play and renderer.
- apps/studio-web/src/types.ts, mock.ts, panels/SceneBrushBar.tsx: typed diagnostics and controls.
- tests/world_processes_gamecore_native_test.cpp: cascade/outbox regression source.

## Verification gate
Implementation/architecture: committed source; canonical existing ECS and renderer reused. Native compilation NOT RUN.
Native API: existing public FRACTURE/PHYSIS contracts unchanged, Studio-only diagnostic helper.
Skill/docs: existing skills and compatibility guide updated.
Studio/Inspector/MCP: existing transient typed RPC extended; no new authoring mutation or inspector schema. Typecheck, interaction and RPC tests NOT RUN.
Persistence/graphs: no new authority or schema, no duplicate support graph. Headless regression NOT RUN.
Performance/platforms: bounded sampling; measurements and platform acceptance NOT RUN.
Dependencies: no new targets. Build NOT RUN.
Examples/tests: existing native test fixture extended, no command executed.
Publishing: real capture/release evidence NOT RUN.
Integration: no main sync/merge; reconcile shared Studio paths with studio-html5 owner.

## Stage 111: planetary live FRACTURE structural diagnostics

Enable Live 3D overlay and the independent FRACTURE structural state checkbox during local Play current map. The existing Scene renderer shows actual ECS Member positions and same-body Bond connections in body-local coordinates. Cyan active anchors, green intact members, orange broken members, red detached members, yellow pending-event crosses. Bond colors represent stored damage/strength; severed, disabled or inactive support edges are interrupted. Active edge eligibility reuses fracture::validBond and existing member broken/detached flags, not a second support traversal. Intact flags alone are not proof of independently verified connectivity.

The read-only snapshot reports generation-safe member IDs, actual latched structural state, strength/damage, pending/acknowledged Collapse delivery and event key/tick. Optional PHYSIS NatureStressOutput and NatureFractureOutbox on those same members are reported independently, with their own climate tick, normalized load, accumulated damage, wind force and bending moment. PHYSIS stress is not synthesized for missing components. A pending event remains pending; the diagnostic never acknowledges, spawns debris, applies forces or repairs a member. No duplicate graph, ECS state, persistence or physics solver.

Bounds: owner member/bond budgets, maximum 256 placed members and 512 sampled bonds, stable packed-ID ordering. Global member status counts include unplaced members before the output cap; cross-body and omitted bond counts describe the visited candidates, not an exhaustive audit after truncation. Cross-body visual edges are omitted without changing authoritative graph topology. Native local Play/map/revision guards retained. HTML5 shows sampled counts and first eight members and bonds, optional stress and delivery. Planar ecology unchanged.

Source fixture exercises existing support cascade, damage/severing, pending outbox, climate-clock distinction, body isolation and caps. Native build, CTest, browser typecheck, interactive UI, GPU screenshots and performance NOT RUN. Next: MORPH constraint/node displacement diagnostics. Physical debris delivery, full force/structural stress solver, actual terrain mutation and shared Studio integration remain separate owner work.
