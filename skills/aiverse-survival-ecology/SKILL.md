---
name: aiverse-survival-ecology
description: Reuse AIVERSE survival/ecology policies (body temperature and wetness from environment, clothing and shelter, hunger/thirst/stamina, weather modifiers, population ecosystems with carrying capacity and predation, animal wander/graze/flee/hunt/sleep behavior from perceived targets, renewable resource nodes) when building native survival, life-sim, colony or open-world games.
---

# survival-ecology

Use when an authoritative owner needs exposure and deprivation for characters, weather that changes bodies, power
and growth, deer/wolf style ecosystems, animals reacting to what they perceive, and bushes/trees/fish that deplete
and regrow. Not a weather simulator, health/damage owner, inventory, navigation or grid-field kernel: the owner
supplies weather state, shelter, clothing and food actually eaten; fire/heat fields stay in `docs/fields.md` (G5).
Colony jobs, power graphs and the planner bot are `settlement-production`.

1. Read `docs/genre-packages.md` (survival-ecology section). Headers: `genre/Survival.h` (`stepSurvival`,
   `survivalWeatherEffects`, `survivalMood`), `genre/Ecology.h` (`stepEcology`), `genre/EcologyAnimals.h`
   (`stepEcologyAnimals`), `genre/EcologyResources.h` (`stepEcologyResources`), `genre/EcologyData.h`,
   `genre/SurvivalConfigured.h`, `genre/SurvivalPersistence.h`, umbrella `genre/SurvivalEcology.h`.
2. Data: `readEcologyDefinitions(json["ecology"])`, `readEcologyAnimals(json["animals"])`,
   `readEcologyResources(json["resources"])`; errors `ecology.species 1.diet: ...`, `resources[0].amount: ...`.
   Body constants: optional generated `survival` settings section -> `survivalConfig(settings)`.
3. Per tick: compute `survivalWeatherEffects(config.weather, weather)` once; feed `windPowerMilli`/`solarMilli`
   into power producers, `growthMilli` into ecology/renewal, `activityMilli` into animals. `stepSurvival` takes
   per-body shelter, heat, clothing, exertion, rest and eaten food/drink; bodies without input are outdoors.
4. Animals: build `genre::PerceivedTarget` records (from stealth-senses `stepSenses`, or the owner's own LOS)
   with `observer` = animal id, plus `EcologyTargetFacts{species, position}` (species 0 = humans). Priority:
   flee feared > sleep > hunt visible prey > graze > wander (RNG only from the passed `Random`). `Killed`,
   `Grazed`, `Starved` events go to the population owner (`EcologyInput::harvests`).
5. Ecology periods run on ticks divisible by `periodTicks`; harvests apply immediately; fractions are carried in
   milli accumulators; optional birth noise consumes the passed RNG.
6. Persist `SurvivalState` 106, `EcologyState` 107, `EcologyAnimalState` 108, `EcologyResourceState` 109 with
   `genre::save/load`; hash with `survivalHash`; bind `survivalConfigHash`, `ecologyDefsHash`,
   `ecologyAnimalDefsHash`. Save the RNG state with the simulation.
7. Examples: `examples/libraries/genre/production-survival` and `examples/native/libraries/ProductionSurvivalConsumer.cpp`;
   tests in `tests/libraries/genre_production_survival_tests.cpp`.

Build selection: `AIVERSE::lib_survival_ecology` (depends on stealth-senses, configuration and `aiverse_engine`);
native OBJECT modules call `aiverse_native_use_libraries(target survival-ecology)`.
