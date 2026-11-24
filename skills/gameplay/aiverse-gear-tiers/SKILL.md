---
name: aiverse-gear-tiers
description: Reuse AIVERSE gear-tiers for tiered protective equipment in any genre - helmets, vests, shields, backpacks or rings that absorb a share of damage to the zones they cover, wear down, break, add carry capacity, swap on pickup and compare for bots and auto-pickup.
---

# gear-tiers

Use when a project has armor-like pieces with levels and durability (equipment slots of any kind). The legacy
`engine/actor` host wires it from rules (`gear` at the top level or `actor.gear`, docs/fps.md § Gear); the same block on other hosts is debt G4/G6
in `docs/no-genre-split-audit.md`. The steps below are for native / host owners.

1. Read `engine/systems/Gear.h` (namespace `aiverse::lib::systems`) and the catalog entry.
2. Data: `readGearCatalog(json["gear"], "gear")` - pieces with `id`, `slot` (your slot numbering), `tier`,
   `durability`, `reduction` and `wear` (milli: 550 = 55 %, 1000 = one durability point per absorbed point),
   `zones` (bit mask of your hit zones, e.g. 1 head, 2 body, 4 legs) and `capacity`. Errors name the path
   (`gear[2].reduction: must be in 0..1000`). Example: `~/.aiverse/packages/templates/fps_survival/rules/gear.json`.
3. One `GearLoadout` per actor in the owner's state. On a hit: `absorbHit(catalog, loadout, zoneBit, damage)`
   returns the damage to apply, the absorbed points, the wear and the slots that broke (removed from the loadout);
   apply the damage through the owner's health path and show the broken pieces.
4. Pickup: `equipGear(catalog, loadout, piece)` returns the replaced piece (`def == 0` when the slot was empty);
   drop it as loot with its durability (the FPS module swaps it into the picked loot entry). `betterGear` decides
   automatic pickups and bot choices (higher tier, then more remaining share). `gearCapacity` adds carry room.
5. Death: drop every worn piece as loot with its durability and clear the loadout.
6. Determinism and saves: integer math only; persist the loadout with `io(Archive&, GearLoadout&)` and fold
   `gearHash` into the checksum; validate loaded loadouts with `valid(catalog, loadout)`.
7. Check: `tests/libraries/gear_tests.cpp` (CTest `library_gear`: zones, durability caps, breaking, swaps, ordering,
   capacity, JSON errors, round trip) and `gear` in `tests/fps/tests.cpp` for the FPS wiring. Not implied: item
   visuals, inventory UI, loot tables, repair, set bonuses.

Build selection: `AIVERSE::lib_gear_tiers` (depends on `aiverse_engine`); native OBJECT modules call
`aiverse_native_use_libraries(target gear-tiers)`.
