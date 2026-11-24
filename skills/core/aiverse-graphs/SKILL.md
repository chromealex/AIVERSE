---
name: aiverse-graphs
description: Use AIVERSE C++ graphs capabilities for bfs and topological ordering, including native modules of external game projects.
---

# AIVERSE graphs

Resolve paths relative to the engine root. Read `libraries/catalog.json`,
`docs/libraries.md` and `docs/native-modules.md` before integration.

## Select and integrate

- Layer: **simulation**. Current coverage: BFS and topological ordering.
- Read `engine/core/Graphs.h` as the current API authority. No Lua implementation is required.
- Use dense IDs. Test unreachable nodes, cycles and duplicate edges. Weighted search and flows remain planned.
- Call `aiverse_native_module(...)` in the external game native/CMakeLists.txt, then `target_link_libraries(aiverse_native_<id> PRIVATE AIVERSE::libraries)`. Include `"engine/core/Graphs.h"`. The engine defines the target before loading game modules; do not add a second libraries subdirectory.
- Use `examples/native/libraries` as a compiling project-local example.
- Pin with `python3 tools/libraries/catalog.py pin graphs --project /absolute/game` for C++ foundation entries; verify with the same command using `verify` before upgrading.
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

Build selection: `AIVERSE::lib_graphs`; native OBJECT modules call
`aiverse_native_use_libraries(target graphs)`. Only this header library and its
declared dependencies are selected. See `skills/catalog.json` for dependency and responsibility routing.

WeightedGraph/WeightedEdge, dijkstra, graphPath, dfs and weakComponents now live in engine/core/Graphs.h. Nonnegative uint64 costs; UINT64_MAX reserved; overflowing paths ignored. DFS follows declared adjacency order; weak components label by smallest vertex. No tests yet.

Added aStar (admissible uint64 heuristic, only goal path guaranteed optimal), stronglyConnected (iterative Kosaraju, minimum vertex labels), minimumSpanningForest (Kruskal, checked cost). No build/tests. Next flow algorithms.

Flow API: engine/core/GraphFlow.h, graphFlow with Maximum or MinimumCost, integral capacities, edgeFlow in input order, explicit augmentation budget and negative residual cycle rejection. Partial result when budget exhausted. No tests yet.

GraphAnalysis.h: bellmanFord, floydWarshall (max 1024 vertices), graphCuts (iterative undirected multigraph bridges/articulation). Signed paths with negativeCycle=true must not be treated as valid shortest paths. Arithmetic overflow returns nullopt. No tests yet.

## Shared multi-source weighted paths (World Processes Stage 85)
`engine/core/Graphs.h`: `multiSourceDijkstra(WeightedGraph, vector<WeightedSeed>, maxCost)` returns optional MultiSourcePaths (distance and stable uint64 sourceId per vertex). Seeds carry vertex, nonnegative initial cost and source ID. Equal costs prefer the smaller source ID even through zero-cost cycles. Inclusive maxCost must be below kInfiniteCost; invalid vertices, infinite edge costs and out-of-budget seeds reject, overflowing/beyond-cutoff extensions are skipped. Unreachable distance is kInfiniteCost; sourceId is meaningful only for reachable vertices. This is a label query, not a parent tree. ECHO uses it for max-intensity attenuation without a traversal per source. No new graph library target; tests in ecs_echo_acoustics_test.cpp, not run.
