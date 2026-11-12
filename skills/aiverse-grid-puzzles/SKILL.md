---
name: aiverse-grid-puzzles
description: Use AIVERSE Sokoban rules, undo/redo and bounded solution search in native C++ puzzle games. Does not replace match-3 or provide a universal puzzle solver.
---

# Push puzzles

Read `libraries/include/aiverselib/strategy/Puzzles.h` and
`docs/strategy-libraries.md`. `PushBoard` owns immutable walls/goals;
`PushState` owns the player cell and sorted, unique box cells.

- Cell IDs are row-major. Directions are 0 left, 1 right, 2 up, 3 down.
  Use `valid(board,state)` at load boundaries, including after restoring state.
- `pushMove` is the only move implementation for play, previews and search.
  Invalid moves preserve state. Do not separately approximate pushes in a hint UI.
- `PushHistory` implements undo/redo with an explicit bounded history. A new move
  clears redo. Route all player moves/history changes through Commands and check
  who owns the puzzle before invoking the helper.
- `solvePush` returns a shortest sequence within its visited-state budget.
  Distinguish Solved, Unsolvable, BudgetExceeded and Invalid. Budget exhaustion is
  not a proof of impossibility. The solver supports at most 256 cells/16 boxes;
  runtime boards support up to 64×64. Don't run unlimited search in a frame.
- Link `AIVERSE::lib_grid_puzzles` from a registered native module. Persist PushHistory
  with `strategy/Persistence.h` in NativeState; include the board revision.
  Board legality must be rechecked against those definitions after decoding.
- Follow `examples/native/strategy`; run `strategy_libraries` and
  `python3 tests/libraries/strategy_native.py`. Test row edges, blocked pushes,
  true deadlocks versus budget exhaustion, undo/redo and solution replay.

Generator/editor/hint presentation and other puzzle families remain separate.

Build selection: `AIVERSE::lib_grid_puzzles`; native OBJECT modules call
`aiverse_native_use_libraries(target grid-puzzles)`. Only this header library and its
declared dependencies are selected. See `skills/catalog.json` for dependency and responsibility routing.
