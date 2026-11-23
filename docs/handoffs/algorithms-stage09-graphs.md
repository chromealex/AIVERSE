# Stage 9
Extended existing graphs package in engine/core/Graphs.h, no duplicate library. Dijkstra integer nonnegative weights, path reconstruction, iterative DFS, weak components. Existing BFS/topological unchanged. Skill updated. No build/tests. Next A*, SCC, MST and flows; update catalog summary. Branch feature/world-processes-stage31-bidirectional-handles

Added aStar (admissible uint64 heuristic, only goal path guaranteed optimal), stronglyConnected (iterative Kosaraju, minimum vertex labels), minimumSpanningForest (Kruskal, checked cost). No build/tests. Next flow algorithms.
