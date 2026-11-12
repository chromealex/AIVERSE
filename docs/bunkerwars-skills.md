# Bunker Wars skills

How every Bunker Wars (Mushroom Wars 2 codebase) energy skill maps onto the engine's ability primitives, and the
converter that turns the client's skill data into `rules.json` player abilities. Owner: agent-bw-skills.
Primitives: [Abilities and effects](game-format.md#abilities-and-effects), [skill objects](game-format.md#skill-objects-objects-spawned-from-units-goals-magnets-souls).

## Converter

```sh
python3 tools/importers/bunkerwars/skills/convert.py --bw ~/Projects/bunker-wars --out skills.json --report coverage.md
python3 tools/importers/bunkerwars/skills/test_convert.py      # self-test on a synthetic client (no BW data)
```

It reads the user's own client checkout (read-only, nothing of it is committed): C# classes under
`MW2Data/Gameplay/Database` (script guids, interfaces, interface-property backing fields), the GameData table
(`GameDataSettings.asset`: `GDFloat` / `GDInt` keys such as `hero2|skill3|cooldown`) and the skill assets:

| asset | what |
|---|---|
| `SkillsHeaders/SkillN.asset`, `_CampaignSkills/*.asset`, `_Heroes/*/HeroN-Talent.asset` | a card / talent: `levels` (1..10) -> level prefabs |
| `SkillsData/SkillN/SkillN_LevelK.prefab` | `SkillsUpgradeInfo.mods`: upgrade components applied in order; `skillDescription` -> `_Desc` |
| `SkillN_Desc.prefab` | `cost` (hero skill energy) and `skillResource` -> the base skill prefab |
| `_Gameplay/SkillsData/_SkillN_Config_*.prefab` | `SkillData`: `cooldown` (GD key), `initialCooldown`, `items` = elements |
| `_Gameplay/MapsEnvironment/**` | map / campaign skills (moving fire, trains, campaign scripts) without headers |
| `Heroes_DELETE/Skills_DELETE/**` (`--legacy`) | the removed hero skills (magnet, worm, jumping heads, petrify, souls, ...) |

Output: `{"playerAbilities": [...], "bwSkills": {...}}` to merge into a game's `rules.json`. One ability per card
(`bw_skill7`), talent (`bw_hero6_talent`), campaign skill (`bw_campaignskill9001`), map skill (`bw_map_*`) and legacy
skill (`bw_legacy_*`); level 1 is the ability, levels 2.. are full copies in `levels` (loadout `level` picks one).
Live data today: 119 abilities, 535 ability levels; with `--legacy` 193.

### Units and names the output expects from the rest of the BW port

| what | default | where it comes from |
|---|---|---|
| distances / speeds | BW pixels / `--px-per-cell` (50) | the map importer must use the same scale (BW Y is screen Y: ellipses with the world aspect ratio become circles) |
| skill cost | `cost.skillEnergy` = card `cost` | `--energy-resource`; hero energy (per unit sent / passive) is a resource of the BW rules |
| morale | resource `morale`, `--morale-star-points` per star | BW morale levels (stars) |
| structure kinds | type tags `house`, `tower`, `smithy`, `special1..5`, `domination`, `kingOfTheHill`, `cave`, `graveyard` | `PointInfo.Type` bits |
| cast buildings | `transform` to `bw_<kind><level>` (`bw_tower2`) | building types of the BW data importer |
| spawned objects | `bw_skeleton`, `bw_barrier` (blocksLanes), `bw_flying_tower` | BW data importer; flying tower: agent-bw-routes |
| zones | map areas `bw_zone_<ZoneType>` | environment importer (agent-bw-env) |

## The BW skill model -> abilities

A `SkillData` has `items` (elements). Each element has activation / deactivation conditions, a filter (who) and
modificators (what). The element that spends energy is the cast.

| BW | engine |
|---|---|
| `SkillModificatorConditionMapClick` (activation) | `target: point` (circle of the filter's `inRange`); a points filter with `oneNearest` -> `target: structure`, `shape: target` |
| `SkillModificatorConditionInstant` | `target: none`; range-limited filters act on the whole map (BW measures from the screen / hero) |
| `SkillModificatorConditionTimeout` (deactivation) | `duration` of the statuses / lifetime of the areas the modificators create |
| `SkillModificatorConditionTimeout` (activation of a second element) | the element's effects wrapped in an `area` with `delay` |
| `SkillModificatorConditionTimeoutFromDuration` / `ConditionalTwoStage` | duration (first + second stage); two-stage speed: status `onExpire` -> second status |
| `SkillPointsFilter` | `kind: structure`, `radius` = `inRange`, `affects` = `ownerFilter`, `tags` = `typeFilter`, `level` = `levelFrom..To`, `maxTargets` = `maxAmount`, `sort` (`sortByDistance` / `sortByLevel`) |
| `SkillRandomPointsFilter` | `kind: structure`, `shape: map`, `sort: random`, `maxTargets` |
| `SkillUnitsFilter` | `kind: unit`, `inTransit`, `radius`, `ring` (`ringWidth`), `maxTargets` (`maxCount`); `rangeVelocity` (growing wave) -> start range |
| `SkillPlayersFilter` | player effects with `affects` = `playerFilter` |
| `SkillZoneFilter` | `shape: map`, `area: bw_zone_<type>` |
| modificator `executeFilter` / `ignoreSkillFilter` / `SkillFilterRangeCopier` / `VelocityToRangeCopier` | not converted yet: the element filter is used (see gaps) |
| `chance` + `chanceValue` | `cast` with `chance` |
| `additionalActivationDelay` (SkillActivationDelay) | `area` with `delay` around the effects |
| `SkillVariantFilters` (`AlwaysTrue`, `UnitKills`) | talent variants: not converted (meta progression) |

`PlayersModule.PlayersFilter`: All -> `all`, Me -> `self` (objects: `own`), AlliesAndMe -> `own`, Allies -> `allies`,
Enemies -> `enemies`, EnemiesAndNeutral / AllExceptAllies -> `enemies` + `neutral`, Neutral / AllExceptMe -> approximated.
`MathValues.FloatValue` signs: Add -> stat `add`, Multiply / Set -> stat `mul`.

### Upgrades (level prefabs)

The converter replays `SkillDataModificatorBase.Apply` per class. Interface upgrades find the modificators that
implement the interface and write the backing field the class maps the property to (read from the C# source, so
BW quirks stay: `UnitsRage` maps `effectForce` to its speed and `unitsEffectSpeed` to its attack).

| upgrade | writes |
|---|---|
| `SkillCooldown`, `SkillCooldownInitial` | ability `cooldown` / `initialCooldown` (Add / Set / Multiply) |
| `SkillTimeout`, `SkillEffectDuration` | deactivation timeouts; `IModifiableEffectDuration*` fields of modificators and conditions |
| `SkillRange`, `SkillRangeTornado`, `SkillRangeVelocity`, `SkillUnitsCountInFilter`, `SkillPointsFilterAmount/Level`, `SkillPointsTypeFilter`, `SkillOwnerFilter` | element filters |
| `SkillEffectForce`, `SkillUnitsSpeed`, `SkillUnitsSpeedInArea`, `SkillUnitsSpeedSecondStage`, `SkillUnitsCount`, `SkillChance`, `SkillEffectRange`, `SkillEffectRangeMultiplier`, `SkillEffectProportion`, `SkillItemsCount`, `SkillItemLevel`, `SkillItemTargetLevel`, `SkillDamage`, `SkillAppearDuration`, `SkillWaveFilter` | the interface property's backing field (FloatValue `.value`, filter `startRange` -> `inRange`, GD values) |
| `SkillModificatorChance`, `SkillActivationDelay` | modificator chance / delay |
| `SkillDefences`, `SkillUnitsAmounts`, `SkillRageMods`, `SkillSoulsSpeed`, ... (list / flag replacements) | kept on the modificator for its mapping |
| `SkillSfx`, `SkillSkinSound`, `SkillSkinVisualEffect*`, `SkillCameraShakePattern`, `Skill*VisualEffect` | presentation only (audio / particles agents) |

### Skill objects (Database/Objects)

| BW | engine |
|---|---|
| `RollingStoneInfo` (UnitsPetrify spline walker) | `area` `motion: lane` from each unit (`origins` + `consume`), `onArrive` `garrisonKill` at the lane end; sprite sheet = view of the area |
| `JumpingHeadInfo` (UnitsJumpingHeads line walker) | `area` `goal: groupTarget` from each unit, `onArrive` `garrisonKill` |
| `UnitMineInfo` (UnitsMines standing object) | contact `area` from each unit, `maxTriggers: 1`, `lifetime: [t - f, t + f]`, `onExpire` range kill |
| `Magnet` (PointsMagnet, PointsWorm) | `pull` (start velocity, acceleration, min / max velocity) from a structure status tick |
| `SplineWalker` (FreezeWave, Teleport dig) | `area` `motion: lane` |
| `LineWalker` | `area` `goal` |
| `StandingObject` | static `area` with `lifetime` / `onExpire` |
| souls (SoulsCollector, GoHome souls, SpiritedAway) | `onDeath` areas + `projectile` `start: point` `goal: target` |
| `CustomUnitSkinInfo` | view only (unit skin swap while a status is on) |

## Modificators

Fidelity: **exact** = checked against the BW code; **mapped** = converted from the serialized fields, not yet
checked line by line; **approx** = same intent, the note says what differs; **ref** = another agent's area;
**none** = presentation / bookkeeping; **missing** = no primitive yet; **unused** = mapped, no current data uses it.
Regenerate: `convert.py --legacy --report coverage.md`.

| modificator | fidelity | skills (live + legacy) | notes |
|---|---|---|---|
| HeroSpecial1 | unused | 0 | mapped, not used by the converted data |
| PlayersAllMorale | approx | 1 | the previous morale is not restored when it ends |
| PlayersAverageMorale | exact | 1 + 1 legacy |  |
| PlayersCooldownChange | unused | 0 | mapped, not used by the converted data |
| PlayersDespell | mapped | 0 + 1 legacy |  |
| PlayersFreezeMana | mapped | 0 + 1 legacy |  |
| PlayersFreezeMorale | exact | 1 |  |
| PlayersIncreaseMorale | approx | 1 | moraleCooldown (decay timer shift) not converted |
| PlayersLowerCaughtDefense | unused | 0 | mapped, not used by the converted data |
| PlayersNotifyAttacks | unused | 0 | mapped, not used by the converted data |
| PlayersOwnerResetSkillStat | none | 2 | statistics / HUD only |
| PlayersShowAmountCaught | unused | 0 | mapped, not used by the converted data |
| PlayersSilence | exact | 1 |  |
| PlayersStealSkill | exact | 1 + 1 legacy |  |
| PlayersTemporaryIncreaseMorale | unused | 0 | mapped, not used by the converted data |
| PointsAmulet | approx | 4 + 1 legacy | reverseDistance is measured straight (BW: along the route) |
| PointsAverageUnits | approx | 1 | per-player amount coefficients after averaging not applied |
| PointsBarrier | approx | 0 + 1 legacy | wall across the lanes at the click: a "bw_barrier" unit with blocksLanes and hp = unitsCount |
| PointsBomb | exact | 1 + 1 legacy |  |
| PointsCastBuilding | approx | 1 + 2 legacy | cast keeps the target level only (BW castToMaxInsteadMin / forceCastLevel / maxCastLevel); cast to the same kind at targetCastLevel as a +1 level for the duration |
| PointsChargeTower | approx | 0 + 1 legacy | tower weapon range / damage up for the duration |
| PointsChickenCoop | exact | 0 + 1 legacy |  |
| PointsCloseRows | approx | 1 | BW closes rows of the next group sent from the structure; here: own groups in transit form up and speed up |
| PointsDeadLock | approx | 0 + 2 legacy | DeadLock as closing the groups of the point |
| PointsDefector | mapped | 0 + 1 legacy |  |
| PointsDefector2 | approx | 1 | production of the structure walks to the caster: redirect (sendUnitsPerSecond for towers/smithies ignored) |
| PointsDefenseCalculation | none | 0 + 1 legacy | defense recalculation bookkeeping |
| PointsDefenseCoefficient | exact | 3 |  |
| PointsDisable | exact | 1 |  |
| PointsDisableForPlayer | unused | 0 | mapped, not used by the converted data |
| PointsEndGameOnCatch | approx | 0 + 1 legacy | BW defeats the enemies of the local player; here the enemies of the skill owner |
| PointsEnhanceWeapon | approx | 1 | extra smithy levels as the armory attack factor of that many levels without armories (BW adds them to the owned ones) |
| PointsEvilSouls | approx | 0 + 1 legacy | souls variants converted as a souls collector |
| PointsFastCapture | exact | 1 |  |
| PointsFastConvert | approx | 1 | upgrade / conversion cost and time multipliers; chanceToUpgrade of neighbours not converted |
| PointsFlyingUnits | approx | 0 + 2 legacy | ignoredByTowers / ignoredBySkills not converted |
| PointsFreeze | unused | 0 | mapped, not used by the converted data |
| PointsFreezeWave | mapped | 0 + 1 legacy |  |
| PointsGhostArmy | unused | 0 | mapped, not used by the converted data |
| PointsGiveUnits | unused | 0 | mapped, not used by the converted data |
| PointsHauntedHouse | unused | 0 | mapped, not used by the converted data |
| PointsHypnosis | exact | 1 |  |
| PointsIllusionUnits | mapped | 0 + 1 legacy |  |
| PointsIncreaseLevel | exact | 1 |  |
| PointsIncreaseProduction | mapped | 0 + 1 legacy |  |
| PointsInvulnerability | exact | 2 |  |
| PointsKillContinuous | mapped | 0 + 1 legacy |  |
| PointsKillLeavingUnits | exact | 1 |  |
| PointsKillUnitsPercent | unused | 0 | mapped, not used by the converted data |
| PointsLanding | exact | 1 |  |
| PointsMagnet | exact | 0 + 1 legacy |  |
| PointsMakeStonedUnits | unused | 0 | mapped, not used by the converted data |
| PointsManyBombs | exact | 2 |  |
| PointsMapWind | mapped | 0 + 1 legacy |  |
| PointsMoraleThief | approx | 1 | the caster gets the stars together with the loss (BW: starFlyDuration s later, after the flight) |
| PointsPanic | approx | 1 + 1 legacy | group attackCoefficient not converted |
| PointsPlague | approx | 1 | BW lets a point be infected again only waveTimeout s after the last carrier entered |
| PointsPossession | unused | 0 | mapped, not used by the converted data |
| PointsProductionLimit | approx | 0 + 3 legacy | tutorial production cap: production stops for the duration |
| PointsProductionSpeedCoefficient | exact | 5 + 2 legacy |  |
| PointsReaver | approx | 0 + 1 legacy | tower weapon range / damage up for the duration |
| PointsRecoverUnits | approx | 1 | refill toward a share of the maximum as a percent of the garrison |
| PointsRecoverUnitsVisual | none | 1 | presentation of PointsRecoverUnits |
| PointsRedirectProduction | exact | 1 |  |
| PointsReplenishUnits | unused | 0 | mapped, not used by the converted data |
| PointsResurrection | exact | 0 + 1 legacy |  |
| PointsRevenge | mapped | 0 + 1 legacy |  |
| PointsSabotage | approx | 2 | BW sends unitsCount neutral attackers spread over the structures (ignoring morale / smithies / shields): direct garrison loss |
| PointsShareUnits | mapped | 0 + 1 legacy |  |
| PointsShortWay | approx | 0 + 1 legacy | portal at the click; the exit (BW: next to the chosen structure) is not resolved: units stay |
| PointsShowAmount | unused | 0 | mapped, not used by the converted data |
| PointsSkeletonArmy | approx | 1 + 1 legacy | army as soldiers of the caster next to the point (BW skeleton skins, appear animation) |
| PointsSoulsCollector | mapped | 0 + 1 legacy |  |
| PointsSpeedArea | mapped | 0 + 1 legacy |  |
| PointsSpiritedAway | unused | 0 | mapped, not used by the converted data |
| PointsSpy | exact | 1 |  |
| PointsStoneUnits | unused | 0 | mapped, not used by the converted data |
| PointsStopTower | unused | 0 | mapped, not used by the converted data |
| PointsSuppressDefense | exact | 1 |  |
| PointsSwap | approx | 1 | skills of the swapped points with deactivateOnSwap are not cancelled; lastCatchTick not swapped |
| PointsTeleport | exact | 1 |  |
| PointsTutorialLimitMoveFrom | unused | 0 | mapped, not used by the converted data |
| PointsTutorialWaitCaught | unused | 0 | mapped, not used by the converted data |
| PointsTutorialWaitConvertFinished | unused | 0 | mapped, not used by the converted data |
| PointsTutorialWaitConvertStarted | unused | 0 | mapped, not used by the converted data |
| PointsTutorialWaitTowerFire | unused | 0 | mapped, not used by the converted data |
| PointsTutorialWaitUpgradeFinished | unused | 0 | mapped, not used by the converted data |
| PointsTutorialWaitUpgradeStarted | unused | 0 | mapped, not used by the converted data |
| PointsUnitsKill | unused | 0 | mapped, not used by the converted data |
| PointsUpgrade | unused | 0 | mapped, not used by the converted data |
| PointsVictim | mapped | 0 + 1 legacy |  |
| PointsWarChains | exact | 1 |  |
| PointsWorm | approx | 0 + 1 legacy | worm: pulls walkers into the structure, then eats a share of its garrison |
| PositionBlindTowers | exact | 1 |  |
| PositionFlyingTower | ref | 1 | flying tower type "ufo" (agent-bw-routes); its defense aura additionalDefenseCoefficient as a defense status |
| PositionMines | mapped | 0 + 1 legacy |  |
| PositionPlaceholder | unused | 0 | mapped, not used by the converted data |
| UnitsAddSmithyLevel | mapped | 0 + 1 legacy |  |
| UnitsAttackCoefficient | mapped | 0 + 1 legacy |  |
| UnitsBetrayalGarden | mapped | 0 + 1 legacy |  |
| UnitsBetrayalGarden2 | exact | 1 |  |
| UnitsBlast | mapped | 0 + 2 legacy |  |
| UnitsBlizzardRain | exact | 2 |  |
| UnitsChainLightning | mapped | 0 + 6 legacy |  |
| UnitsCharge | approx | 0 + 1 legacy | charging units: speed burst |
| UnitsClone | mapped | 0 + 1 legacy |  |
| UnitsDeposit | mapped | 0 + 1 legacy |  |
| UnitsDespell2 | mapped | 0 + 1 legacy |  |
| UnitsDisarm | mapped | 0 + 1 legacy |  |
| UnitsExploring | exact | 1 |  |
| UnitsFreeze | exact | 14 + 3 legacy |  |
| UnitsGoHome | mapped | 0 + 1 legacy |  |
| UnitsGravity | mapped | 0 + 1 legacy |  |
| UnitsInvisibility | exact | 1 |  |
| UnitsJumpingHeads | exact | 0 + 1 legacy |  |
| UnitsKill | exact | 29 + 3 legacy |  |
| UnitsKillAura | exact | 1 |  |
| UnitsKillTrain | approx | 9 | train runs from the click along its direction killing on contact (environment object: agent-bw-env) |
| UnitsMines | exact | 1 |  |
| UnitsPetrify | exact | 0 + 1 legacy |  |
| UnitsRage | exact | 1 |  |
| UnitsReverse | exact | 4 |  |
| UnitsSacrifice | approx | 1 | BW towers also skip their usual morale transfer for these units |
| UnitsSandStorm | mapped | 0 + 1 legacy |  |
| UnitsSlowdown | mapped | 0 + 1 legacy |  |
| UnitsSlowdown2 | exact | 1 + 1 legacy |  |
| UnitsSpeedCoefficient | exact | 1 + 1 legacy |  |
| UnitsSpeedInAreaCoefficient | mapped | 0 + 2 legacy |  |
| UnitsThrowBack | mapped | 0 + 1 legacy |  |
| UnitsTornado | exact | 2 + 1 legacy |  |
| UnitsWormReverse | unused | 0 | mapped, not used by the converted data |

## Skill notes

- skill8: upgrade SkillSkinSound: presentation only (not converted)
- campaignskill9001: upgrade SkillCameraShakePattern: presentation only (not converted)
- campaignskill9002: upgrade SkillCameraShakePattern: presentation only (not converted)
- campaignskill9003: upgrade SkillCameraShakePattern: presentation only (not converted)
- campaignskill9005: upgrade SkillCameraShakePattern: presentation only (not converted)
- hero1_talent: affectsOnlyFirstPlayerPoints: structures of the first matched player only (not converted)
- hero2_talent: affectsOnlyFirstPlayerPoints: structures of the first matched player only (not converted)

## Gaps for 1:1

Every modificator used by the live game is **exact** or **approx** (stage 2); "mapped" rows are legacy
(Heroes_DELETE) skills only. Remaining approx rows of the live game and what they lack:

- PlayersAllMorale: the saved morale is not restored when it ends. PlayersIncreaseMorale: `moraleCooldown`
  (decay timer shift) not converted.
- PointsAmulet: reverseDistance is measured straight (BW: along the route).
- PointsAverageUnits, PointsCastBuilding (castToMaxInsteadMin / forceCastLevel / maxCastLevel), PointsCloseRows,
  PointsDefector2 (tower / smithy sendUnitsPerSecond), PointsEnhanceWeapon (extra smithy levels on top of owned
  armories), PointsFastConvert (chanceToUpgrade of neighbours), PointsRecoverUnits, PointsSabotage (neutral attackers
  as direct loss), PointsSkeletonArmy (skins / appear animation): see the notes in the table.
- PointsMoraleThief: the caster gets the stars together with the loss (BW: starFlyDuration s later).
- PointsPanic: the group attackCoefficient and chanceToNeutral are not converted.
- PointsPlague: re-infection after `waveTimeout` is not modelled.
- PointsSwap: skills with `deactivateOnSwap` on the swapped points are not cancelled; lastCatchTick not swapped.
- UnitsSacrifice: towers still transfer morale for sacrificed units (BW skips it).
- UnitsKillTrain: environment object (agent-bw-env); UnitsAttack (units on routes fight) needs a weapon API for
  route walkers from agent-bw-routes and is converted as a range bonus.
- Points filter `affectsOnlyFirstPlayerPoints` (hero1 / hero2 talents) is not converted.
- BW float arithmetic is reproduced in Q47.16; per-modificator RNG (C5Random seeded by tick) becomes the sim RNG.
- AI use of skills (`AISkillAssistant`): agent-bw-ai; flying tower: agent-bw-routes; environment skills: agent-bw-env.
