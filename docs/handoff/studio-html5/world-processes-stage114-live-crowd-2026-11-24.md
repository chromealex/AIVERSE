# Stage 114 — planetary live CROWD
Date: 2026-11-24. Branch: feature/world-processes-stage31-bidirectional-handles.
Base: 62fb6e17cdc8d60418a96902d0f95a82e11e15b1. Plan: c96ca2c85dbdaa4ec50af388d3922cd9a8d29560. Implementation: 1ee4a01fbb95ec15b2c71a20a9bcb79a62ad904c. Fixture: 5f564dd1ff89fc275ea637984d975a110b6604a3.

## Delivered
- apps/studio-native/StudioPlanetCrowdSnapshot.h: read-only bounded agent/slot/reservation, native path and current gravity support query.
- apps/studio-native/StudioGameSurface.h/.cpp, StudioHost.h/.cpp, StudioScene.cpp: validated independent Live setting and native diagnostic line renderer.
- apps/studio-web/src/types.ts, mock.ts, panels/SceneBrushBar.tsx: typed controls and summaries.
- tests/world_processes_gamecore_native_test.cpp: regression source.

## Verification gate
Implementation/architecture: source committed, existing ECS, navigation, support and renderer owners reused. Native compilation NOT RUN.
Native API: existing CROWD and GameCore contracts unchanged; Studio-only helper.
Skill/docs: existing skills and compatibility guide updated.
Studio/Inspector/MCP: existing transient typed RPC extended; no new authoring mutation or schema. Typecheck, interaction and RPC tests NOT RUN.
Persistence/graphs: no new authority, schema or pathfinder. Headless regression NOT RUN.
Performance/platforms: bounded sampling; measurements and platform acceptance NOT RUN.
Dependencies: existing targets only; build NOT RUN.
Examples/tests: existing fixture extended; no command executed.
Publishing: real captures and release acceptance NOT RUN.
Integration: no main sync/merge; shared Studio paths require integration review.

## Stage 114: planetary live CROWD reservations and native routes

Enable Live 3D overlay and the independent CROWD reservations / routes checkbox during local Play current map. Snapshot reads completed-tick CROWD Agent allocation and actual canonical Transform/Slot locations, enabled state, ordered preference candidates (up to 16 per sampled agent), and occupancy of valid same-body reserved slots across all current agents. Invalid, stale, disabled or cross-body allocation is not drawn as a valid reservation. Numeric rows for unplaced entities are retained.

The movement owner remains GameCore. The diagnostic reads existing Mobile moving/flow flags, actual velocity, path cursor/count, current goal, remaining path points and Orders queue size. Legacy Agent x/z/goal and planar CROWD Route are never interpreted as planetary positions. Native path points are body-local samples joined with lifted diagnostic segments, not newly computed routes; invalid or incompatible points break the polyline. A flow-field agent need not have explicit path points. Slot relation lines represent allocations, not paths or evidence that a CROWD-issued move is in progress. Current owner eligibility is read through the existing surfaceCrowdEligible policy when the host is available.

When loaded stock world_processes config enables crowdGravitySupport, the snapshot calls existing samplePlanetGravitySupport for placed agents and slots. It returns current supported/airborne/weak/outward/steep status, clearance, terrain normal and acceleration. It is not a historical rejection reason, does not prove navigation passability, and does not cancel routes, change gravity or issue Move commands. Above 1024 gravity fields support sampling is unavailable while other readback remains.

Native Scene uses green native path lines, amber dashed reservations, cyan free slots, amber occupied slots, red duplicate slots or unsupported agents, muted disabled markers and purple goals. HTML5 panel reports first eight sampled agents/slots, counts, cursor and support. Stable packed-ID ordering, owner component budgets, max 256 agent rows, 512 slot rows, 64 path points per agent and 2048 total path points. Counts except occupancy refer to visited candidates. Existing local Play and map/revision guards remain; flat ecology unchanged.

Source fixture checks seam-crossing native waypoint, invalid cross-body reservation and route break, zero-gravity support, occupancy, stale Agent tick, output caps and read-only behavior. Build, CTest, browser typecheck, interaction, real captures and performance NOT RUN. Next CIRCUIT power/signal/device diagnostics, then SOCIETY. No main merge.
