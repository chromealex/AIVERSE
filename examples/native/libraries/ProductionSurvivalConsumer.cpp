// settlement-production + survival-ecology native consumer: validated settings sections, colony/power/ecology JSON,
// two authoritative ticks in the owner order (power -> planner commands -> colony -> bodies -> ecology), versioned
// records (kinds 104..110) in NativeState and definition hashes for the save envelope. The full 20-day example is
// examples/libraries/genre/production-survival.
#include <aiverselib/genre/Colony.h>
#include <aiverselib/genre/SurvivalEcology.h>
#include <aiverselib/bots/ColonyPlannerData.h>
#include "modules/rts/RtsNative.h"
#include <string>
bool libraryProductionSurvivalConsumerExample(aiverse::game::NativeState& state,const aiverse::lib::gameplay::GameplaySettings& settings){
    using namespace aiverse::lib::genre;namespace bots=aiverse::lib::bots;
    if(!colonySettingsErrors(settings).empty()||!survivalSettingsErrors(settings).empty())return false;
    const auto survival=survivalConfig(settings);
    ColonyDefs defs;PowerGridState grid;EcologyDefs eco;bots::ColonyPlannerConfig planner;
    try{
        defs=readColonyDefinitions(aiverse::Json::parse(R"({"restNeed":2,
            "needs":[{"id":1,"decayPerTick":20,"lowAt":40000,"criticalAt":10000},{"id":2,"decayPerTick":30,"lowAt":30000,"criticalAt":5000}],
            "restores":[{"activity":"sleeping","need":2,"perTick":100}],"meals":[{"resource":3,"need":1,"amount":50000,"ticks":10}],
            "workTypes":[{"id":1},{"id":2}],"recipes":[{"id":1,"workstationType":1,"workType":1,"work":30,"inputs":[[1,2]],"outputs":[[3,1]]}],
            "alerts":[{"id":1,"workType":2,"workPerSeverity":20}]})"),colonyConfig(settings));
        grid=readPowerGrid(aiverse::Json::parse(R"({"restoreDelayTicks":30,"nodes":[{"id":1,"kind":"producer","output":50},{"id":10,"kind":"consumer","demand":30,"priority":5}],"links":[[1,10]]})"));
        eco=readEcologyDefinitions(aiverse::Json::parse(R"({"periodTicks":1,"species":[{"id":1,"growthMilli":200}],"regions":[{"id":1,"capacity":[[1,50]]}]})"));
        planner=bots::readColonyPlanner(aiverse::Json::parse(R"({"intervalTicks":1,"rules":[{"id":1,"action":"bill","ref":1,"value":5}]})"),defs);
    }catch(const aiverse::JsonError&){return false;}
    ColonyState colony;SurvivalState bodies;EcologyState ecology;bots::ColonyPlannerState plan;aiverse::Random rng(7);
    if(!colonyAddStockpile(colony,1,0,{{1,10}})||!colonyAddWorkstation(colony,10,1,1,true)||!colonyAddColonist(defs,colony,1,colonySchedule(-1,-1,-1,-1))
        ||!survivalAddBody(survival,bodies,1)||!ecologySetPopulation(eco,ecology,1,1,10))return false;
    for(int64_t tick=0;tick<2;++tick){
        auto power=stepPower(grid,tick,{});if(!power)return false;
        auto commands=bots::planColony(planner,defs,colony,{},plan,tick);if(!commands)return false;
        ColonyInput in;in.commands=commands->commands;in.unpowered=power->unpowered;in.moodModifiers[1]=survivalMood(bodies.bodies.at(1));
        if(!stepColony(defs,colony,tick,in))return false;
        SurvivalInput body;body.bodies[1].sheltered=true;
        if(!stepSurvival(survival,bodies,tick,body)||!stepEcology(eco,ecology,tick,{},rng))return false;
    }
    if(colony.workstations.at(10).recipe!=1||!colony.colonists.at(1).job)return false;
    state.values["genre.colony.defs"]=int64_t(colonyDefsHash(defs)&0x7fffffffffffffffull);
    state.values["genre.survival.config"]=int64_t(survivalConfigHash(survival)&0x7fffffffffffffffull);
    state.values["genre.colony_hash"]=int64_t((colonyHash(colony)^powerHash(grid)^survivalHash(bodies)^survivalHash(ecology))&0x7fffffffffffffffull);
    return storeBytes(state,"genre.colony",save(colony))&&storeBytes(state,"genre.colony.power",save(grid))&&storeBytes(state,"genre.survival",save(bodies))
        &&storeBytes(state,"genre.ecology",save(ecology))&&storeBytes(state,"genre.colony.planner",save(plan));
}
