---
name: aiverse-puzzle-solvers
description: Reuse AIVERSE puzzle rules, bounded solvers, hints and the puzzle bot (match-3 with specials/cascades/shuffles, flood fill and pipe connectivity, sliding tiles and blocks, polyomino packing, Sokoban sessions with deadlocks, sudoku/nonogram/CSP with uniqueness checks) when building native casual-puzzle, logic or escape-room games.
---

# puzzle-solvers

Use when a native/host owner needs authoritative puzzle state with player commands, hints, solvability checks or
an AI player for: match-3, color flood, pipes/wires, region counting, 15-puzzle, sliding blocks, polyomino packing,
Sokoban, sudoku, nonograms or other small-grid constraint puzzles. Sokoban movement, undo/redo and the push solver
stay owned by `grid-puzzles` (`strategy/Puzzles.h`); this package adds sessions, move numbers and the bot on top.
Programming puzzles use `puzzle-machine`. Not a renderer, input mapper, level editor or procedural generator.

1. Read `docs/genre-packages.md` (Puzzle solvers) and the catalog entry. Headers:
   `libraries/include/aiverselib/genre/Puzzle*.h` (entry `PuzzleSolvers.h`) and `bots/PuzzleBot.h`.
2. Commands: every family has one command struct (`Match3Swap`, `PuzzleFloodPick`, `PuzzlePipeRotate`,
   `PuzzleTilesSlide`, `PuzzleBlocksMove`, `PuzzlePackCommand`, `PuzzleCspAssign`, `PuzzleSokobanCommand`) with a
   `move` number that must exceed `state.lastMove`. Route player input and bot output through the same `*Apply`
   in the authoritative command handler (after checking who owns the puzzle). Rejected commands return
   false/nullopt and leave the state untouched; never patch state directly for previews: copy and apply.
3. Budgets: solvers/hints take `PuzzleBudget{nodes}`; `PuzzleSearch::Unknown` means the budget ran out (no proof
   either way), `Unsolvable` is a proof. Never call a solver in a frame without a budget sized for that frame.
4. Randomness: pass `sim.rng()` (or an RNG saved with the simulation) to `match3Begin/match3Apply/match3Shuffle`,
   `puzzleTilesShuffle` and the bot. Match-3 refill order is column by column, top to bottom; shuffles try
   `shuffleAttempts` permutations and then a three-color pattern that `valid(Match3Rules)` guarantees has a move.
5. Match-3 data: specials are rules (`line4`/`line5`/`corner` -> `stripe`/`stripeRow`/`stripeColumn`/`bomb`/
   `colorBomb`); goals = move limit, score, per-color collection. Cascade order is documented in
   `PuzzleMatch3.h`; hints: `Match3HintMode::First` or `Best` (refill-free preview, earlier swap on ties).
6. Data: `genre/PuzzleData.h` readers (`readMatch3Level`, `readSokobanLevel`, `readSudoku`, `readNonogram`,
   `readCsp`, `readFloodLevel`, `readPipeLevel`, `readTilesLevel`, `readBlocksLevel`, `readPackLevel`) throw
   path-qualified `JsonError`s (`puzzle.match3.rows[2][4]: ...`). Example: `examples/libraries/genre/puzzles/levels.json`.
7. Bot: `bots::puzzleBot*(level, state, PuzzleBotSkill, rng)` returns the next command (`lastMove + 1`);
   difficulty = `budget` + `mistakeMilli` (random legal move; Sokoban bots undo out of deadlocks, CSP bots guess a
   value and repair it later). Presets: `puzzleBotSkillPreset(0..2)`. A perfect bot never draws from the RNG.
8. Persist with `genre::save/load` (`genre/PuzzlePersistence.h`, record kinds 112..119) under dedicated NativeState
   keys, re-check with `match3Compatible` / `puzzleFloodCompatible` / ... against the level after loading, and feed
   `puzzleHash(state)` into the checksum. Bind the level data revision in your save envelope.
9. Verify: `tests/libraries/genre_puzzles_tests.cpp` (CTest `library_genre_puzzles`), the example
   (`library_genre_puzzles_example`: completion + identical output in two processes),
   `examples/native/libraries/PuzzlesConsumer.cpp`, `python3 tools/libraries/catalog.py check`.

Build selection: `AIVERSE::lib_puzzle_solvers` (depends on `grid-puzzles` and `aiverse_engine`); native OBJECT
modules call `aiverse_native_use_libraries(target puzzle-solvers)`. Only this header library and its declared
dependencies are selected. See `skills/catalog.json` for dependency routing.
