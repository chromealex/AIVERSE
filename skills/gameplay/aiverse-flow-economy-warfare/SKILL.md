---
name: aiverse-flow-economy-warfare
description: Add AIVERSE flow-economy warfare systems to any project's rules - radar, sonar, jammers, stealth and underwater units, radar targeting, repair / reclaim / assist / patrol builders, self-destruct, stockpiled weapons and interceptors, wind / tidal / geothermal income, bomber attack runs and air repair pads, and importing TA data.
---

# Flow-economy wars (Total Annihilation style)

Use when a game needs radar contacts, reclaim, nukes and anti-nukes, wind income or aircraft passes on top of the
flow economy (`flow` resources, `costModel: "progressive"`). Data only: no game code in the engine. Current host: legacy
`engine/game`.

Authority: `docs/game-format.md` § Radar, sonar and jammers and § Field operations; engine code
`engine/game/Sensors.cpp`, `engine/game/FieldOps.cpp`; sample `~/.aiverse/packages/templates/metaltide`.

1. Rules: `radar.targeting`, `repair`, `reclaim`, `selfDestruct`, `ambient` (map `ambient` overrides it). Types:
   `radar` / `sonar` / `jammer` / `stealth` / `underwater` (+ weapon `underwater`), builders `assist` + `patrolWork`,
   `stockpile` (+ `abilities` it feeds, ability `autocast` + `smart` so bots fire it), `intercept` with the tags
   of the shooters it stops, `ambientIncome`, `attackRun`, `repairPad`, `seekRepair`.
2. Keep a small fixed `income` on wind / tidal generators: the bot counts only fixed income as an income building.
3. Importing a TA install: `python3 tools/importers/ta/import.py --source <TA folder> --out <game folder>` (archives
   through `tools/unpackers/hpi`); the game folder stays outside the engine repository.
4. Check: `rules_lint`, `scenario_run` with the command names `repair`, `reclaim`, `self_destruct`, a bot match
   (`match_run`), and for engine changes `aiverse_tests sensors_ fieldops_`, CTest `ta_importer`,
   `tests/checksums/check.sh`.

Workflow recipe over existing engine code; no runtime library.
