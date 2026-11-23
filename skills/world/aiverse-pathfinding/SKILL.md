---
name: aiverse-pathfinding
description: Use AIVERSE pathfinding (grid A*/HPA*, flow fields, navmesh, hybrid) from any project or native module.
---

# AIVERSE navigation

Resolve paths relative to the engine root. Read `libraries/catalog.json`,
`docs/libraries.md` and `docs/native-modules.md` before integration.

## Select and integrate

- Layer: **simulation**. Current coverage: Existing pathfinding and flow fields.
- Read `docs/pathfinding.md` as the current API authority. No Lua implementation is required.
- Any-angle paths: rules or map `movement.pathfinding: "navmesh"` (flat maps) uses the genre-independent `engine/nav` mesh (`docs/navmesh.md`); grid A*/HPA* stay the fallback and the default.
- Big or mixed maps: `movement.pathfinding: "hybrid"` (HPA* cluster graph, navmesh in open clusters, grid A* in maze clusters, flow fields on the same graph). Measure a map with `aiverse_headless --bench-nav N` before choosing.
- Reuse movement commands; do not move entities from view scripts. Test blocked goals, arrival and planets separately.
- Use the existing C++ engine API documented in the source above. This reference entry is not a standalone library target.
- Use `examples/native/libraries` as a compiling project-local example.
- Pin with `python3 tools/libraries/catalog.py pin navigation --project /absolute/game` for C++ foundation entries; verify with the same command using `verify` before upgrading.
- Do not copy algorithms into the game. Extend the common library if a reusable operation is missing.

## Contracts

IDs and indices are zero-based unless documented otherwise; task worker IDs are positive.
Keep definitions immutable. Store persistent gameplay values in NativeState values/arrays,
reconstruct transient structures locally and write changes back before returning from a tick.
Library containers do not register ECS pools, hash themselves or serialize automatically.
Never retain references across rollback. Validate commands before calling helpers.
No floating-point scores or platform RNG in simulation. Supply sim.rng() through an explicit adapter
with the documented integer range. Resolve ties deterministically; sort entity inputs by index.
Presentation.h is presentation only and must not drive simulation outcomes.

## Verify

Run `python3 tools/libraries/catalog.py check`, `python3 tests/libraries/run.py` and
`python3 tests/libraries/native_consumer.py` after integration changes.
For simulation integration run full engine tests and headless restore/template checks from AGENTS.md.
Coverage is deliberately bounded by the catalog; do not claim BT/GOAP, flow solvers or the entire
category is implemented when only its foundational API exists.

Runtime library: none. This recipe integrates an existing engine subsystem or runs host tools;
it does not provide a separate runtime implementation. Pin the engine revision for that owner.

## TerraWeave Caverns shared navigation (Stage41, 2026-11-24)

For cave volume paths, read `skills/world/aiverse-terraweave-caverns/SKILL.md`,
`engine/world/TerraWeaveCavernsNavigationData.h`,
`engine/world/TerraWeaveCavernsNavigationGraph.h`,
`engine/world/TerraWeaveCavernsNavigationPublication.h`,
`engine/world/TerraWeaveCavernsPathRouter.h`, and
`engine/world/TerraWeaveCavernsNavigationAdapters.h`.

- Cave SDF and its immutable revision are **input data**, not a replacement A* or another duplicated NavMesh. `CaveNavigationDataProvider::sample(point,agentProfile)` exposes clearance, floor support, water flag, area class and integer cost; `canTraverse(a,b,profile)` validates transitions and intermediate samples. Agent profiles include ground, flying (3D void), swimming (requires NEREID callback), climbing (project/physics callback) and custom (registered callback). The 3D cell graph supports signed X/Y/Z neighbors, including vertical transitions.
- `CaveNavigationGridView` provides fixed deterministic 3D cell IDs, `passable(id)`, `neighbors(id,vector<CaveGraphEdge>&)` with bounded integer costs, and `portals(clusterWidth)` for HPA* graph construction. Feed these callbacks to an existing shared A*/HPA*/FlowField/Hybrid solver or a custom solver. Do not duplicate their search algorithms inside Caverns. **Important:** existing `game::MapGrid::findPath/flowAcquire/hybridPath` are presently bound to MapGrid's 2D cell state and do not directly accept an arbitrary 3D callback graph. An explicit native solver adapter is still needed to route these exact implementations over the cave graph; do not claim this is already wired into MapGrid movement.
- `CaveSharedNavSource` adapts the common SDF/walkability provider to the real native `nav::Source` multi-span interface, which existing `nav::Mesh` can build. It is suitable for ground/climbing surfaces only, not arbitrary free-flight volumes. `CaveNavSource`/Stage29 authored-nav stage remains available for existing integrations, with shared indexed SDF.
- `CavePathRouter::registerSolver(name,backend,callback)` routes path queries to an actual existing or custom solver. The callback receives a provider from a **committed** source revision, and its result must report the same revision. Built-in algorithm names `AStar`, `HPAStar`, `FlowField`, `NavMesh`, `Hybrid`, `Custom` are backend IDENTIFIERS, not claims that all six existing engines have been linked to the router automatically.
- `CaveNavigationPublication` notifies subscribers (`AStar`, `HierarchicalAStar`, `FlowField`, `NavMesh`, `Hybrid`, `Custom`) of a revisioned dirty region **after** actual Box3D/SpatialWorld/nav/GPU game publication. Streamed in/out chunks also invalidate graph/cache topology at the same authored SDF revision. Delivery errors retain pending events; the router refuses to answer from potentially stale derived caches until consumers catch up. Each solver chooses its own dirty-grid, portal, flow-seed or native tile refresh policy.
- NEREID flooding and PHYSIS hazards are provided via read-only, same-epoch external callbacks. Never make solver caches depend on unversioned mutable water/physics state or pick a different source revision than the authoritative SDF.
- Tests and native linking remain postponed until all requested features are completed, per user. See `docs/agent-handoffs/caverns-stage41-2026-11-24.md` for ownership, remaining integration work, and accurate non-tested status.
