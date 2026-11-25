# Stage64 — headless demo target
Branch: feature/world-processes-stage31-bidirectional-handles; main unchanged.
7d9ad743 adds EvolveForestDemo.cpp: native GameModule hydrates map, registers TerraWeave authority and forest persistence, plants 27 trees on generated ground, registers EVOLVE-aware forest tick, prints region state and checksum. c08f0c3e adds CMake target aiverse_evolve_forest_demo in existing test-enabled block.
Intended invocation after build: aiverse_evolve_forest_demo examples/native/world_processes/evolve-forest-demo.json 1024.
No build/run performed. PHYSIS cell and NEREID water setup, rendering, disk save/load CLI remain pending. Headless source target only, not verified executable. Next add those integrations and audit compilation when authorized.
