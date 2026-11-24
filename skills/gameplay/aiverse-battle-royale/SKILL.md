---
name: aiverse-battle-royale
description: Reuse AIVERSE battle-royale policies (phased safe zone with seeded nested circles and per-phase damage, bombardment / red zone, supply drops, air insertion with carrier path, skydive, parachute and landing, last-team-standing standings with knockdowns, revives, wipes, placements, kill feed, draws, pre-match lobby with countdown and bot fill) in any genre: FPS / third-person shooters, top-down arenas, RTS "shrinking map" modes or survival games.
---

# battle-royale

Use when a match is won by the last team standing in a shrinking play space, or when a game needs any one of the
pieces alone (a timed shrinking circle, random area bombardment, scheduled crate drops, a skydive insertion, a
placement table with knockdowns). The `engine/actor` host wires it from rules (`royale` at the top level or legacy `actor.royale`, docs/fps.md); the same block on
other hosts is debt G4/G6 in `docs/no-genre-split-audit.md`. The steps below are for native/host owners.

1. Read the catalog entry (`python3 tools/libraries/catalog.py search royale`) and the headers in
   `engine/mechanics/royale/`: `SafeZone.h`, `Bombardment.h`, `SupplyDrops.h`, `Insertion.h`,
   `Standings.h`, `Lobby.h`; `Data.h` reads the JSON, `Persistence.h` saves and hashes; `BattleRoyale.h` includes all.
2. Data first: copy `examples/libraries/mechanics/royale/royale.json` (bounds, zone phases, flight, drop, bombardment,
   supplyDrops, standings, lobby). Distances are metres (decimal literals, parsed as Fixed), times are ticks. Only
   `bounds` and `zone` are required; `readRoyale(json, "path")` throws `JsonError("royale.zone.phases[2].radius: ...")`.
3. Seed every policy once per match from the simulation seed (`beginSafeZone(seed)`, `beginBombardment(def, seed)`,
   `beginSupplyDrops(seed)`, `planInsertionFlight(def, bounds, seed, tick)`); each keeps its own PCG32 stream in its
   state, so other systems' random draws never shift the circle or the path. A default-constructed state is rejected.
4. Tick order per owner tick: `stepRoyaleLobby` until Started -> `addRoyaleCompetitor` for every seat, then
   `startRoyaleStandings` -> `stepSkydiver` per actor (owner passes the ground height under it; after Landed give the
   actor back to its motor) -> `stepSafeZone` -> `stepBombardment(def, state, safeZoneCircle(...), tick)` ->
   `stepSupplyDrops(def, state, safeZoneTarget(...), tick)` -> damage: for actors outside `safeZoneCircle` apply
   `safeZoneDamage`; for every `Shell` event apply its damage to actors in the circle the owner considers exposed ->
   report results to the standings: `reportDown` (health reached 0), `reportOut` (bleed-out, zone, disconnect),
   `reportOutTogether` (several on one tick: last teams together = draw), `reportRevive`, `reportDamage`.
5. Authority: the policies decide and report; they never write health, positions, loot or views. Validate input
   ownership before `SkydiveInput` (move components in [-1, 1]). Spawn loot for `SupplyDropEventKind::Landed` through
   the loot / inventory owner. A team is out when nobody stands; its knocked members are finished and credited to
   their knocker (`finishKnocked`). Same-tick outs share the better placement; `royaleRanking` gives the result table.
6. Determinism: Fixed / integer only, one step per object per strictly increasing tick (repeated or backward ticks
   and invalid input return `std::nullopt` and leave the state untouched). Do not range-for over `*stepX(...)`
   directly (the temporary optional dies before the loop in C++20): store the result first.
7. Persist each state with `royaleSave` / `royaleLoad` (record kinds 120..126, 127 reserved) in its own NativeState
   key or the module's save; fold `hashRoyale(hasher, state)` into the checksum; bind `royaleDataHash(json)` in the
   save envelope (definitions are not saved).
8. Check: `tests/libraries/royale_tests.cpp` (CTest `library_royale`; golden hashes pin the seeded circle and
   flight path on every compiler). Presentation reads `safeZoneCircle` / `safeZoneTarget` / `safeZoneTicksLeft`,
   `insertionPosition`, `supplyDropPosition` and the kill-feed events. Not implied: HUD, minimap, models, audio,
   loot tables, bot behaviour (zone-aware bots read the same circles), vehicles, network replication.

Build selection: `AIVERSE::lib_battle_royale` (depends on `aiverse_engine` only); native OBJECT modules call
`aiverse_native_use_libraries(target battle-royale)`.
