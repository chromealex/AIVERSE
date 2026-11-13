# Bunker Wars port — what the template does not do yet

Generated content: `tools/importers/bunkerwars/import.py --source <bunker-wars> --out templates/bunkerwars`
(re-run it instead of editing `game.json`, `rules.json`, `maps/`, `lang/`, `bw/` by hand). Paths below are relative to
the Bunker Wars repository (`Client/Assets/...`). `bw/*.json` hold every BW number the engine has no key for yet,
already converted: positions and distances in cells (BW units / 40, map origin = BW (0, 0) at the map centre,
y flipped), times in seconds.

What is exact today: garrison growth (IncreaseTime), capacity (MaxValue), building defense (ShieldCoefficient),
upgrade / conversion cost and time (GD Cost, BuildingTime), level drop on capture (downgrade), tower range / fire rate /
bullet speed, armory attack / defense table by the number owned (`playerMods.byCount`), unit speed, start garrisons,
owners and teams of all 139 map headers, hero energy (max 500, 2.5 / s). Everything else is listed here.

Note on the client's own data: `GameData.uiws` has `RaceN|TowerN|FirePerSecond/FireRange` and `House5|IncreaseTime`
values that differ from the building assets (e.g. Tower1 1.5 / 400 vs 1.35 / 360). The runtime reads the assets
(`BuildingParametersInfo` plain floats; no code reads those GD keys), so the importer uses the assets and prints the
mismatches.

## Battle core (sim; nobody assigned yet unless noted)

| gap | BW source | data already exported |
|---|---|---|
| Morale: points (catch neutral / enemy, lost, upgrade per building type, 10 points per unit lost in an attack), levels at 0/500/1000/2000/4000/8000 points, per level damage / shield / speed multipliers (not compounding), point decay every SubtractCooldown s. The template plays with morale level 0 (all multipliers 1) | `MW2Data/Gameplay/Battle/Modules/MoraleModule.cs`, `Player.cs` (CalcSpeedPercent, CalcShieldPercent, CalcAttackPercent, AddMoralePoints), `BaseModule.cs` AttackResult_INTERNAL | `bw/morale.json`, `bw/buildings.json` (moraleCatch*/moraleLost/moraleUpgradePoints) |
| Energy for a lost unit depends on morale: EnergyPerUnit x (6 - moraleIndex - 1); the template uses the morale-0 value 2.25. Also granted for units killed by towers / clashes / skills (any reason except "dead by defense"); the engine `gain.lost` only counts assaults | `Player.cs` OnUnitLost | `bw/heroes.json` energyPerUnit |
| Production slows with the player's army: rate x1 below 50 % of the cap, x0.75 from 50 %, x0.5 from 75 %, 0 at the cap; cap = sum of MaxValue of own houses (x productionLimitMultiplier) | `Player.cs` GetIncreaseRate / GetUnitsMaxCount, `HouseModule.cs` | `bw/battle.json` buildings |
| Neutral houses grow up to `maxValueNeutral` (engine: neutrals never grow) | `HouseModule.cs` PrepareUnitsProducedUnitsCount | `bw/maps/*.json` points[].values |
| Garrisons are floats in BW (fractional growth and losses, leftover attackers keep a fraction); the engine counts whole units with an accumulator | `BaseModule.cs` Attack | — |
| Soldiers shoot while walking (unitsAttack config 1: 30 deg forward sector, 350 units = 8.75 cells, not within 200 of a point, 0.5 s aim, 0.25 s, 0.2 s move delay, damage 22 +-3 vs 1 hp) | `Unit.cs` TryFire, `UnitAttack/UnitsQuadTree.cs` | `bw/battle.json` unitsAttack |
| Tower range is an ellipse: rangeY = 0.5 x rangeX (world.aspectRatio); the template uses a circle of rangeX | `TowerModule.cs` UpdateInfo | `bw/battle.json` world.aspectRatio |
| Tower fire rate is a rate (1.35 / s); the template writes cooldown = 1 / rate rounded to 6 digits | `TowerModule.cs` AdvanceTick | `bw/buildings.json` |
| Upgrade cost / time multipliers from hero passives, FastConvert; House5 cost 999 = not buildable | `BuildingInfo.cs` GetBuildingCost | — |
| Point values per difficulty (easy / normal / hard / expert); the template takes easy for multiplayer maps (BW `Any` -> index 0) and normal for campaign maps | `MapPointsModule.cs` GetCurrentValues | `bw/maps/*.json` points[].values, complexityUsed |

## Routes (agent-bw-routes) — engine side done, importer has to write these keys

Engine: docs/game-format.md § Routes, groups, multiscreen. Defaults are BW `BattleParametersInfo` at 40 units / cell.
Coordinates: cell = (x / 40 + w / 2, -y / 40 + h / 2), like the points. What `import.py` must write:

- `rules.json` `"lanes": {"routes": {"lines": 6}, "groups": true}` instead of the A* lanes (left / right / spacing).
  Only when `bw/battle.json` differs from the BW defaults: `routes.columnPadding` (columnsPadding / 40),
  `flyColumnPadding` (flyColumnsPadding / 40), `trim` (routeVisualizer.pathPointOffset / 40), `jitter`
  (units.positionRandomOffset / 40), `fade` (units.alphaFadeThreshold / 40), `groups.rowSpacing` (rowsOffset / 40),
  `groups.flyRowSpacing` (flyingRowsOffset / 40), `lines` (common.unitsInRow).
- map `"routes"`: one entry per `Routes.list[]` item:
  `{"from": "p<pointFromId>", "to": "p<pointToId>", "points": [...], "lines": [...], "blockers": [...], "towersCoverage": [...]}`
  - `points`: every `points[]` as `[x, z]`, or `{"at": [x, z], "offsets": [8 x offset.offsets[k] / 40], "zone": zoneFlags,
    "slowdown": slowdownCoefficient}` when any of those is non-zero (offset values keep their sign: the y flip and the
    engine's right vector cancel out);
  - `lines`: `rawSplineLines[k].rawNodes` converted like points (BW uses them instead of the control points when a line
    has >= 2 nodes: they are what BW units walk, so they are needed for 1:1);
  - `blockers`: ids of the destructible objects whose `closedRoutes` contain this pair (either direction), once they
    are map entities;
  - `towersCoverage`: `{"point": "p<pointIndex>", "distances": [d / 40 ...]}` as stored (leading zeros already dropped).
- map entity key `"screenGroups": [..]` from `points[].screenGroups` (routes across groups are dropped, BW rule).
- map `"screen": {"size": [68, 38], "bounds": [left, right, top, bottom] / 40}` from `MapHeader.gameplayRectOffset`
  (only maps with a gameplayRectOffset; center defaults to the map centre = BW offset 0). Map scroller maps can keep
  the environment `mapScroller.screen`: it drives the same screen API.
- Flying skills (`PointsFlyingUnits`): a structure status with `"flags": ["flying"]` (+ speed `mods`) on the source
  point, not on units in transit: the next send from it then flies along a flying route (whole garrison).

## Modes (agent-bw-modes)

Conquest only (`victory: noEntities`). Domination / King of the Hill / TimeMod per map (`modes`, `gameMode`,
`kingOfTheHillScore`, points[].domination / kingOfTheHill and their values), sudden death, win modes, battle modes:
`bw/maps/*.json`, `bw/modes.json`; types `domination` and `koh` exist in rules (DominationPointInfo / KOHPointInfo).
Source: `MapHeader.cs`, `ModsModule/Module.cs`, `SuddenDeathModule.cs`, `GameInfo/WinModes`, `GameInfo/BattleModes`.

## Environment (agent-bw-env)

Wind, toad, fly bomb, worms, outmap units, map auto skills / skill pairs, graveyard limits, Mouro, map scroller,
battle royal, weather / winter (BattleParameters winter, weather): `bw/maps/*.json` environment, `bw/battle.json`.
Not exported (too big, derived): `environment.grass` / `winterGrass` — precomputed grass cells x route distance
ranges (34 MB over all maps; grass slows and can burn units: `GrassModule`); read them from the map data asset.
Caves (`caveGroup`), graveyard / souls, campaign specials (hospital, storage, megatower, prison, snow bunker) are
rules types with their BW garrison numbers but none of their behaviour (`CaveModule.cs`, `GraveyardModule.cs`,
`UnitSoul`, `PointsSoulsCollector`). Destructible objects: `bw/destructible_objects.json` + per map.
Fog of war: maps with `fogOfWar` get `{"mode": "full"}`; BW fow radii are in `bw/battle.json` fow.

## Skills, heroes, loadouts (agent-bw-skills, agent-bw-meta)

`bw/skills.json`: 49 skill cards + 7 talents with names / descriptions (ru, en), allowed slot, rarity, per-level
GameData numbers (`SkillsCards|SkillN|<param>|lvlL`) and the level prefab holding the modificators
(`ME.TexturesGenerator/_DatabaseData/GameInfo/SkillsHeaders/SkillsData/**`). `bw/heroes.json`: 6 heroes (race,
energy numbers, talent, levels). No `playerAbilities` and no map `loadout` yet: a BW loadout = 4 equipped cards
(player inventory on the BW server, `HeroInfo.cs` slots) + the hero's talent, so the maps get loadouts once the skills
exist as player abilities. Passive trees / hero ranks / custom rules: `GameInfo/_Heroes/**`, `PassiveSkillsConfigs`.

## AI (agent-bw-ai)

Per map bot module (variants, trigger info), AI complexity per player, AI6 assets: `bw/maps/*.json` bot / players[].
aiComplexity; `ME.TexturesGenerator/_DatabaseData/AI/**`, `GameInfo/AI/**`. The template uses the engine bot.

## Presentation

Skins (`bw/skins.json`), emotions (`bw/emotions.json`), camera shakes (`bw/camera_shake.json`), race colours /
names (`bw/races.json`), player palettes, building tips (menu hints, `bw/buildings.json` tips), tutorial chapters per
map (`bw/maps/*.json` tutorial). Art (Spine, sprites, map backgrounds) is not imported.
