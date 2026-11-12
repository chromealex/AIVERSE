---
name: rts-card-runtime
description: Build native AIVERSE card games with ordered zones, priority passing, response stacks and private player views. Use for the bounded card runtime, not arbitrary commercial CCG rules.
---

# Card runtime

Read `libraries/include/aiverselib/strategy/Cards.h` and the authority section of
`docs/strategy-libraries.md`. The runtime supports damage, guard, heal, energy and
draw effects targeting a player; arbitrary triggers/replacements and creature
targets are not implemented.

- `makeCards` assigns stable instance IDs. The back of a deck is drawn first.
  To shuffle, construct with hand size zero, use `shuffleDeck` with the simulation
  RNG callback `[0,bound)`, then implement the game's deal phase explicitly.
- Validate custom actions through Commands. `playCard` pays energy, removes a
  hand card and opens a priority response. Only instant cards can respond. Every
  seat passing resolves one frame in LIFO order. Effect order is author-controlled;
  overflow rejects that resolution atomically rather than silently saturating.
- Use `cardView(state, player)` for the owner's hand/public zones and viewer `-1`
  for spectators. Never send full CardState to opponents. RTS lockstep replicates
  simulation knowledge and is not suitable for confidential hands merely because
  the UI uses this projection. A server/view transport is a separate integration.
- Link `AIVERSE::strategy` after `aiverse_native_module`. Save CardState through
  `strategy/Persistence.h` in NativeState, including pending frames, priority,
  pass count and command watermark. Bind immutable rules to module/config version
  and codec revision. Do not keep mutable state on the module object.
- Follow `examples/native/strategy`. Run `strategy_libraries` and
  `python3 tests/libraries/strategy_native.py`. Check nested response order,
  opponent/spectator projections, rejected repeated orders and a fresh restore
  while the stack is nonempty. Production networking requires packet inspection.

Deck building, elimination/winner rules, UI, custom effects and actual transport
remain game integration work. Use the existing meta collection/wallet owner.
