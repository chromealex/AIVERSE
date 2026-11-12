---
name: aiverse-graphs
description: Use AIVERSE C++ graphs capabilities for bfs and topological ordering, including native modules of external game projects.
---

# AIVERSE graphs

Resolve paths relative to the engine root. Read `libraries/catalog.json`,
`docs/libraries.md` and `docs/native-modules.md` before integration.

## Select and integrate

- Layer: **simulation**. Current coverage: BFS and topological ordering.
- Read `libraries/include/aiverselib/Graphs.h` as the current API authority. No Lua implementation is required.
- Use dense IDs. Test unreachable nodes, cycles and duplicate edges. Weighted search and flows remain planned.
- Call `aiverse_native_module(...)` in the external game native/CMakeLists.txt, then `target_link_libraries(aiverse_native_<id> PRIVATE AIVERSE::libraries)`. Include `<aiverselib/Graphs.h>`. The engine defines the target before loading game modules; do not add a second libraries subdirectory.
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
