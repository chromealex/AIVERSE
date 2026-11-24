---
name: aiverse-turn-search
description: Generic game-tree search for any turn rules: iterative-deepening alpha-beta with a node budget and seeded MCTS, both explaining the chosen move.
---

# Turn search bots

Header: `<aiverselib/bots/TurnSearch.h>`. Library catalog id `turn-search`, CMake target `AIVERSE::lib_turn_search`.
Capability area T4 in `docs/capability-status.json`. Tests: `tests/libraries/policy_mechanics_tests.cpp`.

Deterministic policy: Fixed/integer math, ascending ids, strictly increasing ticks; repeated or invalid input returns nullopt and leaves state untouched. Works in any project: the owner reports facts and applies outputs.

## Use

1. Write a rules adapter with actions, apply, side, terminal and evaluate (see the header).
2. `alphaBeta(rules, state, maxDepth, nodeBudget)` or `mcts(rules, state, playouts, seed)`.
3. `result.why` holds the score of every root action and the principal variation for move explanations.

## Limits

Two-sided games; hidden information needs determinization by the owner (see card-games CardBot).
