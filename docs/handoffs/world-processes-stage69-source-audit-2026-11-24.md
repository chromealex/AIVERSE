# Stage69 — source audit and acceptance
Branch: feature/world-processes-stage31-bidirectional-handles; main unchanged.
2f81b827/f1cd39ee fix demo normalization and const ECS access. 14e50ed1 serializes UI layer requests. 0e981955 adds tools/evolve-demo-acceptance.py.
Reviewed signatures: Simulation serialize/deserialize/saveState/loadState/checksum; TerraWeave terrain/forest resources; PHYSIS registration; map hydration position; Studio session flags and web bridge.
Run after feature work: build target aiverse_evolve_forest_demo with tests enabled, then python3 tools/evolve-demo-acceptance.py <path-to-binary>. Runner checks rollback, save/load checksum, resumed vs uninterrupted simulation and SVG parse. Also build Studio native and run existing web typecheck/build and World Processes CTest suite. No commands executed in this stage. Static review is not compilation or acceptance; fix any errors before merge.
