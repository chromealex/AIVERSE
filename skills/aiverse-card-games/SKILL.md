---
name: aiverse-card-games
description: Reuse AIVERSE card/board game rules (card and rule-variant JSON, shuffled decks with draw/mulligan, phases, legal action generation, a LIFO effect stack with priority passing and fizzling, per-seat hidden information with a non-leaking view and redacted events) when building native CCG, deckbuilder or board-game style games.
---

# card-games

Use when a native/host owner needs authoritative card rules beyond the bounded `card-runtime` duel: data-defined
cards (units, spells, instants with targets and ordered effects), rule variants as data (hand/health/energy curve,
draw per turn, first-player skip, fatigue, overdraw burn, board limit, turn limit, separate combat phase, rush,
partial/full mulligan), legal action lists for UI and bots, response stacks with counters and fizzling, reveals.
It composes `card-runtime`'s zone vocabulary (`cardRuntimeZone`); keep `strategy/Cards.h` for the existing
five-effect duel. Not a deck builder, collection, trigger/replacement language or network transport.

1. Read `docs/genre-packages.md` (Cards and turn strategy section) and `libraries/include/aiverselib/genre/CardGame.h`
   (+ `CardGameRunner.h`).
2. Data: `readCardLibrary(json, cardRulesFromSettings(settings))` and `readCardDecks(json, lib)`
   (`genre/CardGameData.h`, example `examples/libraries/genre/cards-turns/duel.json`). Errors are path-qualified
   `JsonError`s (`cards.cards[3].kind: ...`, cross-field `cards.card 7.effects[0]: ...`). The optional generated
   `cardGame` settings section supplies default hand/health/turn limit and bot budget.
3. Setup: `cardGameSetup(lib, decks, firstPlayer, sim.rng())` shuffles each deck, assigns stable instance ids in
   shuffled order, deals and opens the mulligan (or turn 1). RNG is consumed only by setup and mulligan shuffles.
4. Every player or bot input is a `CardCommand{seq, seat, action}` applied by `stepCardGame(lib, state, cmd, rng)`:
   strictly increasing seq, seat holding priority, action in `cardLegalActions(lib, view)`. Invalid/repeated
   commands return nullopt and leave state and RNG untouched. Playing or attacking puts an item on the stack and
   passes priority; all living seats passing in a row resolves the top item (LIFO); passing with an empty stack
   advances Main -> Combat (separateCombat) -> next turn. Items whose target left fizzle.
5. Hidden information: send each seat only `cardViewFor(lib, state, seat)` and `cardEventsFor(report.events, seat)`
   (viewer -1 = spectator). Decks are counts only; hands are visible to the owner plus seats a `reveal` effect
   showed them. Legal actions are computed from the view, so they never depend on hidden cards. RTS lockstep
   replicates full state: confidentiality needs an authoritative server transport (not provided).
6. Persist `CardGameState` with `genre::save/load` (`genre/CardGamePersistence.h`, record kind 96; 96..103 are the
   cards+turns range) in its own NativeState key; feed `cardGameHash(state)` into the checksum and bind
   `cardLibraryHash(lib)` in the save envelope.
7. Example: `examples/native/libraries/CardsTurnsConsumer.cpp`, executable duel in
   `examples/libraries/genre/cards-turns/main.cpp`. Run `tests/libraries/genre_cards_turns_tests.cpp`
   (CTest `library_cards_turns`, `library_cards_turns_example`), `catalog.py check`, `generate_gameplay.py --check`.

Build selection: `AIVERSE::lib_card_games` (depends on `lib_card_runtime`, `aiverse_engine`); native OBJECT
modules call `aiverse_native_use_libraries(target card-games)`. Bots: `aiverse-cards-turns-bots`.
