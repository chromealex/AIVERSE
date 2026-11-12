---
name: aiverse-cards-turns-bots
description: Reuse AIVERSE turn and card bot policies (a tactics turn planner scoring moves/attacks/overwatch with a bounded deterministic search, and a card evaluator with bounded lookahead) that see only their own seat/team view, when native card or tactics games need opponents.
---

# cards-turns-bots

Use for computer opponents in `card-games` and `tactics-grid` games. Bots return the same action a player submits;
the owner applies it through `stepCardGame` / `stepTactics`, so bots can never bypass validation. Real-time bots
(melee/shooter/squad) are `genre-bots`; RTS AI is RtsAI.

1. Read `libraries/include/aiverselib/bots/CardBot.h` and `bots/TacticsBot.h`, and `docs/genre-packages.md`
   (Cards and turn strategy section).
2. Card evaluator: `cardBotChoose(lib, cardViewFor(lib, state, seat), cardBotConfig(settings))`. It reads only the
   view: legal actions come from it and lookahead runs on `cardStateFromView` (unknown hands/decks absent). Each
   candidate is simulated with the real rules (others pass until the stack is empty), scored by `cardEvaluate`
   (health, board, hand weights), then a bounded follow-up ply (`nodeBudget`, `depth` 1..4). Mulligan replaces
   cards above `mulliganCostAbove`. No RNG; ties keep the earlier legal action (Pass first).
3. Turn planner: `tacticsBotChoose(scenario, tacticsViewFor(scenario, state, team), objectiveCell,
   tacticsBotConfig(settings))` for the team's active unit. Scores each reachable destination (only seen units
   block) by enabled expected damage, kill chance, seen threats, cover and advance toward the objective cell (map
   data, never an unseen enemy position); `searchBudget` bounds evaluations; no RNG. Call again after every
   accepted action. If stepTactics rejects a move (an unseen unit holds the cell), submit EndTurn.
4. Bots are stateless: nothing extra to save; determinism follows from state + view. Verify with the bot-vs-bot
   games in `tests/libraries/genre_cards_turns_tests.cpp` and `examples/libraries/genre/cards-turns/main.cpp`
   (identical hashes across runs; choices unchanged when hidden cards/unseen units change).

Build selection: `AIVERSE::lib_cards_turns_bots` (depends on `lib_card_games`, `lib_tactics_grid`);
native OBJECT modules call `aiverse_native_use_libraries(target cards-turns-bots)`.
