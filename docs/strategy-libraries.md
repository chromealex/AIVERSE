# Strategy libraries for native game modules

This is the first implementation batch from the 47-area genre audit. It adds six
bounded runtime packages, not six complete genres. `genre-implementation.json`
tracks the original acceptance criteria separately from package delivery.

| Package | Public include | Implemented contract | Remaining scope |
|---|---|---|---|
| turn-scheduler | `aiverselib/strategy/Turns.h` | Stable initiative order, owner/AP checks, nested LIFO reactions, round resets | Initiative changes, reaction eligibility, actual effects, tactical grid/UI and planning AI belong to integration |
| card-runtime | `aiverselib/strategy/Cards.h` | Deck/hand/board/discard/exile, instances, priority passing, response stack, five ordered effect kinds, private projections | Arbitrary trigger/replacement language, creature targeting, automatic triggers, deck builder and authoritative transport |
| grid-puzzles | `aiverselib/strategy/Puzzles.h` | Sokoban movement, sorted boxes, undo/redo, bounded shortest-path solver | Other puzzle rule families, generator, editor and hint UI; existing match-3 is not duplicated |
| big-numbers | `aiverselib/strategy/BigNumber.h` | Exact nonnegative integer parse/format/compare/add/subtract/multiply/divide-by-small | Signed/fractional/log/tetration numbers, prestige graph and display abbreviations |
| offline-production | `aiverselib/strategy/Offline.h` | Exact simultaneous affine chains by matrix exponentiation, atomic failure | Caps, consumption, unlock transitions and event-driven piecewise laws; existing LiveOps tier rewards remain authoritative |
| production-networks | `aiverselib/strategy/Production.h` | Atomic multi-input recipes, buffers/capacity, deterministic contention and one-hop transfers | Pressure, heat, electrical networks, rail dispatch, routing UI and physical transport |

## Connect from a game's native folder

```cmake
aiverse_native_module(ID my_rules VERSION 1 ENTRY createMyRules SOURCES Rules.cpp)
target_link_libraries(aiverse_native_my_rules PRIVATE AIVERSE::strategy)
```

The target is defined before game-native CMake runs. It adds headers only: no Lua,
dynamic loader, renderer or circular dependency on `aiverse_game`. Pure policies
can also be used with `AIVERSE::libraries`; Persistence needs the engine include
root provided by `AIVERSE::strategy`.

Copy `examples/native/strategy` into a project native folder for a complete
registration/lifecycle example. Its `strategy_demo` module uses all six packages.
The example is an acceptance harness alongside an RTS map, not a card/puzzle UI.
Its fixed test definitions are deliberately small and are not production balance.

```json
"nativeModules": [{"id":"strategy_demo", "command":32768, "config":{"rate":1}}]
```

Game commands reach `command()` through `Simulation::submit`. The example validates
player ownership, reconstructs local typed state, and writes accepted changes back.
The tick callback advances automatic production. An actual game must also validate
its targeting/range/reaction rules and route results into its existing damage and
inventory owners. Do not run a second health/inventory model for RTS entities.

## Persistence and authority

Include `aiverselib/strategy/Persistence.h`, then use
`storeState(nativeState, "key", typedState, definitionRevision)` and
`restoreState(...)`. The envelope contains a magic, codec version, type ID and
definition revision. Loading is transactional and rejects trailing, truncated,
wrong-type, wrong-revision and noncanonical data. Definitions remain immutable;
validate loaded state against the actual board/rules as well as the envelope.
The engine saves/hashes NativeState automatically. Mutable policy state never
belongs to NativeModule members. Change module VERSION/config when definitions
change; the codec does not bypass engine compatibility checks.

`cardView(state, viewer)` exposes only that player's hand and public zones/counts.
It is a projection, not a network-security implementation. Full CardState must stay
on an authority for hidden-information PvP; ordinary replicated RTS lockstep is
not made secret by using this helper. Spectators use viewer `-1`. Server-side
shuffle uses the engine RNG, returning `[0,bound)` to `shuffleDeck`. Construct with
an empty hand before shuffling; the deck's back is the next card drawn.

Turn actions reserve AP when accepted. LIFO resolution returns a value for the
authoritative host; cancelling/fizzling does not refund AP. The host owns dead
actors, game-over decisions and reaction eligibility. Cards charge energy when
played; all players passing resolves one frame. These policies do not implement
the full rules of Magic, D&D, or any commercial game.

## Numeric and scheduling boundaries

BigNumber uses base 10^9 and at most 456 limbs (4104 decimal digits). Operations
return `nullopt` for underflow/division by zero/capacity exhaustion. There are no
floats or platform-dependent serialization. This deliberately does not replace
Fixed or the bounded meta wallet.

An offline law has at most 16 variables and means
`next[i] = constant[i] + sum(matrix[i][j] * current[j])` with nonnegative integers.
All variables read the same old tick. `advanceOffline` uses exponentiation and
must be equivalent to repeated ticks, including chained growth. Rejecting an
oversized intermediate or result preserves the original state. `now` is a caller-authorized tick,
not a clock sampled by the library. Caps/consumption cannot be encoded as an affine
law; use a separately verified event model for those rules.

Factory rules are sorted by node/link IDs. A node runs at most one recipe per tick;
insufficient inputs or full output means it does nothing and spends nothing.
Transport follows stable link IDs after recipes, reads source availability and
destination free capacity from that phase's snapshot, and never forwards arriving
material again in the same tick. Space freed by transport is available next tick.
This explicit conservative rule prevents edge order from creating multi-hop speed.
The host should use this only for custom networks, not mirror built-in conveyors.

The puzzle solver returns `BudgetExceeded`, never `Unsolvable`, when its state
budget prevents exhaustive search. Runtime boards are at most 64×64; solving is
limited to 256 cells and 16 boxes. Hints must obey the same legal move function.

## Validation

```sh
python3 tools/libraries/catalog.py check
python3 tests/libraries/run.py
python3 tests/libraries/native_consumer.py
python3 tests/libraries/strategy_native.py
```

The strategy-native check builds and executes an external game using the real
engine registration. It exercises commands, saves pending reactions/card frames,
loads into a fresh simulation, and compares 200 subsequent checksums. It repeats
after copying the actual aiversebuild source-export layout and deleting the
original game, checking that includes/linkage do not depend on original paths.

These are headless runtime checks. No visual UX, production multiplayer deployment,
or untested mobile/web/Windows behavior is implied by successful macOS checks.

Verification on 2026-11-12: macOS arm64 library suite 10/10, independent public-header
compilation, ASan/UBSan, native lifecycle/restore and relocated source export passed.
MinGW x86_64 compiled and linked the strategy test executable with C++20,
`-Wall -Wextra -Werror -pedantic`; Windows execution is not verified.

Engine regression: 7741 checks had one sandbox socket-listen failure; that test passed
separately outside the sandbox (27 checks). Editor core was enabled because the baseline
animation test requires it. All 81 RTS checksum cases match after normalizing checkout
CRLF line endings (26 templates plus triggerlab, three seeds); four FPS templates exit
zero. Headless snapshot/serialization/fresh restore/replay and deliberate-divergence
checks pass. Some sandbox/training templates intentionally finish without a winner.
