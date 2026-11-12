---
name: aiverse-navmesh
description: Reuse AIVERSE automatic deterministic layered aabb navigation mesh with clearance, erosion and bounded routing in native C++ projects when implementing game bots or navigation.
---

# navmesh

1. Read `docs/bots.md` and this catalog entry. Read `libraries/include/aiverselib/bots/NavMesh.h` for exact APIs.
2. Build immutable regional polygon meshes from current collision and motor dimensions. Rebuild on geometry changes. Bound cell/polygon/candidate/search budgets. No triangle/slope importer or cross-region streaming links. Use navPath/navStep, real motor tests and aiverse_navmesh exporter.
3. Link `AIVERSE::navmesh` from an executable host target; registered OBJECT native modules call `aiverse_native_use_bots(aiverse_native_<id> NAVMESH)` so backend dependencies propagate to the game. Use `examples/native/libraries/BotConsumer.cpp` for include/persistence access and `examples/libraries/bots/main.cpp` for actual host scheduling.
4. Keep configurations immutable and validate JSON at load. Store dynamic state explicitly; save and restore it with simulation before producing further commands. Never retain future state after rollback.
5. Keep sim Fixed/integer-only. Stable ties and deterministic operation budgets must not depend on wall time. Feed commands through the existing authority/network route.
6. Run `python3 tools/libraries/catalog.py check`, `python3 tests/libraries/run.py`, `python3 tests/libraries/native_consumer.py`, root bot/navmesh tests and representative real matches. If changing engine simulation run full checks and all templates.
7. Update package version, tests, examples and documentation together. Report actual coverage and unverified platforms; do not describe the current AABB navigation mesh as a triangle-navmesh importer or a streamed large-world solution.
