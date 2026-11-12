# Strategy libraries for native game modules

This is the first implementation batch from the 47-area genre audit. The first batch added six
bounded runtime packages, not six complete genres. `genre-implementation.json`
tracks the original acceptance criteria separately from package delivery.

| Package | Public include | Implemented contract | Remaining scope |
|---|---|---|---|
| turn-scheduler | `aiverselib/strategy/Turns.h` | Stable initiative order, owner/AP checks, nested LIFO reactions, round resets | Initiative changes, reaction eligibility, actual effects, tactical grid/UI and planning AI belong to integration |
| card-runtime | `aiverselib/strategy/Cards.h` | Deck/hand/board/discard/exile, instances, priority passing, response stack, five ordered effect kinds, private projections | Arbitrary trigger/replacement language, creature targeting, automatic triggers, deck builder and authoritative transport |
| grid-puzzles | `aiverselib/strategy/Puzzles.h` | Sokoban movement, sorted boxes, undo/redo, bounded shortest-path BFS; 0.2.0: dead cells, freeze deadlocks, A* with node budget, hints | Other puzzle families, move numbering and the puzzle bot are `puzzle-solvers` ([genre-packages.md](genre-packages.md#puzzle-solvers-puzzle-solvers-010)); generator, editor and hint UI |
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
registration/lifecycle example. Its `strategy_demo` module now uses all ten packages.
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

## Zone graph and rhythm timing extension

`ZoneGraph.h` models directed doors with permanent ability masks, visits and the latest
checkpoint. `proveZones` explores the full reachable (zone, abilities, checkpoint) state
space and checks reverse reachability from every final state. A softlock includes a
witness path; exhausted search returns BudgetExceeded, never a false safety guarantee.
Optional respawn edges are included only when the host guarantees respawn is available.
It does not model consumable keys, ability loss, physical doors or region streaming.

`Rhythm.h` converts pulse-based tempo maps to integer microseconds without segment-rounding
drift. Tap/hold judgement, calibration, combo and score are stateful and serializable.
Deadlines are resolved in timestamp/index order, independent of frame batching.
The host must validate and order audio-clock input before advancing judgement time;
advanceRhythm takes corrected timestamps, rhythmInput applies stored calibration once.
Early hold release misses, release in the final good window keeps the head grade,
and a sustained hold completes at its end. Repeated presses while held are rejected.
No actual audio-device bridge or rhythm UI is wired by this headless package.

Native example VERSION 2 adds commands 6 (unlock zone), 7 (respawn), 8 (enter final)
and 9 (start a synthetic-clock hold). It saves after unlocking and during the hold,
then verifies identical continuation into the final and completion of the hold.
Both packages have dedicated catalog skills. The immutable definitions in the example
are C++; a game can construct those records from its own validated configuration.

## Seeded run extension

`RunGraph.h` constructs 2..64 layers with up to 16 rooms per layer. Every room is
reachable and has a path to the final. Positive encounter costs obey each layer's
budget, reserving the minimum cost for unallocated rooms. The engine RNG supplies
weighted encounter/reward sampling; generated topology and offers are persisted.

The lifecycle is Encounter -> Reward -> Travel -> Encounter, ending in Completed
or Failed. Claims accept only saved offers and invoke a grant callback once. The host
must supply authoritative encounter outcomes and commit the grant plus RunState in
one transaction against its existing inventory. No asynchronous backend operation
belongs in that simulation callback. Sequence and phase checks reject repeat claims.
Geometry generation, adaptive directing, relic/card effects and route UI remain separate.

The native fixture VERSION 3 exercises commands 10/11/12 for encounter result, first
reward and first route. It restores at reward selection and completes a three-layer run,
including a duplicate claim request. Its local counter demonstrates atomic native state,
not a production inventory. Unit tests cover 200 seeds and per-layer budget invariants.

## Programmable puzzle extension

`PuzzleMachine.h` is a deterministic integer register machine for programming puzzles,
separate from the engine's Lua scripting. It has eight registers, bounded memory and IO,
14 instruction kinds, checked arithmetic, explicit waits/yield/faults and an instruction
budget. No host IO, clock, RNG or arbitrary native-code execution is exposed.

Save PC, registers, memory, queues, cursor, step count, halt/fault in NativeState. Each
program is immutable and versioned by the host. Input authorization and puzzle completion
checks remain host responsibilities. Native fixture VERSION 4 adds command 13 and advances
one instruction per tick, saving after Read and completing multiplication/output on restore.
A program editor, concurrent circuits and puzzle-specific scoring are not included.
