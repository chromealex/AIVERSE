# AIVERSE Building Generator — Stage 23A: multi-graph changes

Date: 2026-11-23
Repo: chromealex/AIVERSE
Branch: feature/aiv20261123_building-grammar
PR: https://github.com/chromealex/AIVERSE/pull/114 (Draft)

## User request and design

User explicitly requested graph support beyond NavMesh. Inspected
existing `engine/nav/NavMesh.h`, `engine/game/MapGrid.h`,
`docs/pathfinding.md`, `skills/gameplay/aiverse-zone-graph/SKILL.md`,
`engine/buildings/BuildingNavSource.h` and generator graph metadata.
Existing systems have different graph semantics; it would be incorrect
to force all of them into nav::Mesh or claim every graph can be rebuilt
from one door event. Therefore the shared layer only emits revisioned
authoritative committed-state invalidations; each graph owner implements
its own adaptation.

## Implemented

- NEW `engine/buildings/BuildingGraphChanges.h`:
  - `BuildingGraphDomain`: Navigation, Spatial, Hierarchy,
    Transport, Gameplay, Presentation, Audio, Simulation.
  - `BuildingGraphChangeKind`: Created, Removed, GeometryChanged,
    ApertureChanged, AccessChanged, Moved, StructureChanged.
  - `BuildingGraphChange`: sceneId, revision, featureIndex, kind,
    exact per-domain notification mask, conservative world bounds,
    authoritative open flag.
  - `BuildingGraphChangeRouter`: explicit per-domain consumers,
    monotonic per-scene revisions, deterministic dispatch order,
    refusal of newer revisions while any domain of an older change
    remains unacknowledged; retryable pending domain masks.
  - `buildingNavInvalidator`: native NavMesh callback using existing
    `markBuildingNavCollisionChanged`; no second navmesh.
- `engine/buildings/BuildingRuntime.h` exports multi-graph change API.
- Existing `skills/world/aiverse-building-style/SKILL.md` updated
  with domain ownership and integration contract; its catalog entry
  already exists.

## Dispatch ownership contract

The game/PHYSIS owner commits the real door/gate/roof state and dynamic
collision first. Only then does it publish a `BuildingGraphChange`
with a revision, affected bounding box and precise domain mask. Every
registered consumer must accept the event by marking its actual owned
graph dirty, scheduling a bounded rebuild, or updating its own source
data. A callback cannot substitute a fake graph state for the real
owner. The event router deliberately leaves unregistered domains
pending, instead of pretending they were integrated.

A game with no roads/rail, no ZoneGraph and no audio dependency graph
must omit those domains from its event mask. To implement all relevant
systems for a game, owners must register callbacks separately for:
- native NavMesh + building landing links
- MapGrid grid A*, HPA* cluster entrance graph, flow-field cache
- room/spatial graph, zone/checkpoint/access graph
- RoadGraph, railway or transport network
- gameplay/quest/logic graph dependencies
- renderer ShaderGraph, VFX and scene dependency invalidation when
  actual authored appearance depends on the feature
- audio acoustic/occlusion graph and power/utility/environment networks

The router itself does not mutate those graphs. It is intentionally
independent of their otherwise unrelated storage, semantic validation,
network authentication and genre. Existing authoritative game code
owns topology, physics, serialization, rebuild ordering and rollbacks.

## Provenance

- `d85d83576814dfa84e012fc6a6428a722afb4c9d` — router.
- `81e55f8bb0aca2015728d1143b3f27a619538be5` — correct existing
  NavMesh collision invalidation include.
- `7d8eadaf146c9d6ad8745c91587d833371552288` — runtime umbrella.
- `a2975aa7070364e0b707706f66a26cf37c3ab770` — skill.
- Handoff recorded in separate documentation commit.

## Status and explicit limitations

The adapter is committed as source only. No concrete MapGrid, RoadGraph,
ZoneGraph, shader, VFX, audio or power system owner has yet been wired
to the router. The door/roof/gate authoritative animation/collider
state machine also remains to be implemented in Stage 23B. The event
delivery contract does not imply completed full graph support, or
physical open/close verification.

User requested tests later; no build, CTest, TypeScript, PHYSIS,
macOS GPU, in-game pathfinding or multiplayer testing was executed
for Stage 23A. Keep draft PR, reconcile divergent main carefully.

## Next concrete step

Stage 23B: create authoritative interactive aperture/roof state and
collider transaction controller using existing SpatialWorld;
publish BuildingGraphChange only after successful collider commit.
Register the actual existing MapGrid/HPA/flow-field and room/zone
owners where relevant, with bounded rebuild ordering and explicit
backpressure. Make renderer/physics revisions agree and update
skill+handoff per stage. Finally compile and run focused/native tests
when user lifts deferral.
