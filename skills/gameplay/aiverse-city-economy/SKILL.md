---
name: aiverse-city-economy
description: Add AIVERSE economy systems to any project - commerce (premises, businesses, bank loans), careers (rank ladders with perks) and a character arsenal (weapons, ammo, pickups, money for kills) - from rules data, Lua tables and the citygen helpers.
---

# City economy: commerce, careers, arsenal

Use when a project needs premises and businesses, rank ladders or a per-character arsenal. The systems are enabled by
their rules blocks, not by a game type; they act on directly controlled characters (rules `topDown`, a legacy block
name for direct control, not a camera). Current host: `engine/game` (GameCore, the one host). Hero control, raids and traffic are
`skills/gameplay/aiverse-special-modes`; the city map itself is `skills/world/aiverse-city-planner`; a persistent
multiplayer city is `docs/city-server.md`.

| System | Rules / map | Code | Lua | Tests | Add to a city project |
|---|---|---|---|---|---|
| Commerce (`docs/commerce.md`) | rules `"commerce"`, map `"premises"` | `engine/game/Commerce.cpp` | table `commerce` | `aiverse_tests commerce` | `tools/citygen/add_commerce.py` |
| Careers (`docs/careers.md`) | rules `"careers"` | `engine/game/Career.cpp` | table `career` | `aiverse_tests career` | `tools/citygen/add_careers.py` |
| Arsenal (`docs/arsenal.md`) | rules weapons / pickups of the hero | `engine/game/Arsenal.cpp` | - | `aiverse_tests arsenal_` | `tools/citygen/add_arsenal.py` (run by `tools/citygen/make_city.py`) |

1. Prefer the citygen helper for an existing city project; it is safe to run again.
2. Otherwise add the rules block by the doc, then the map data, then Lua glue for UI and missions
   (`skills/workflow/aiverse-lua-gameplay`), HUD by `skills/ui/aiverse-game-screens`.
3. Check: the tests above, a headless run with `--verify-restore-at` (state is saved and hashed), screenshots of
   the HUD.

Not done yet (from the docs): commerce - residents with wallets and jobs, district demand, renting out, taxes;
careers - perks are names only, no UI joining; arsenal - thrown weapons, flamethrower, drive-by, weapon shops,
wanted level, car explosions.

Workflow recipe over existing engine code and tools; no runtime library.
