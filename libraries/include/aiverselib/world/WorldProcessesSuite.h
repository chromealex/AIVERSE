// Opt-in installation of the seven universal World Processes ECS systems.
// No per-genre branches or second authoritative simulation state.
#pragma once
#include "MorphEcs.h"
#include "CrowdEcs.h"
#include "CrowdRoutes.h"
#include "ImprintEcs.h"
#include "ImprintTrails.h"
#include "ImprintCrowdBridge.h"
#include "CircuitEcs.h"
#include "CircuitDevices.h"
#include "GranularEcs.h"
#include "EvolveEcs.h"
#include "SocietyEcs.h"
#include "engine/data/Json.h"
#include <stdexcept>
#include <string>
#include <utility>

namespace aiverse::lib::worldprocess {
struct SuiteFlags {
    bool morph=false,crowd=false,imprint=false,circuit=false;
    bool granular=false,evolve=false,society=false;
    bool any()const {
        return morph||crowd||imprint||circuit||granular||evolve||society;
    }
};
inline SuiteFlags suiteFlags(const Json& json) {
    if(!json.isObject())throw std::invalid_argument("World Processes suite config must be an object");
    SuiteFlags f;
    for(const auto& [key,value]:json.members()){
        if(value.type()!=Json::Type::Bool)
            throw std::invalid_argument("World Processes suite flag must be Boolean: "+key);
        if(key=="morph")f.morph=value.boolean();
        else if(key=="crowd")f.crowd=value.boolean();
        else if(key=="imprint")f.imprint=value.boolean();
        else if(key=="circuit")f.circuit=value.boolean();
        else if(key=="granular")f.granular=value.boolean();
        else if(key=="evolve")f.evolve=value.boolean();
        else if(key=="society")f.society=value.boolean();
        else throw std::invalid_argument("Unknown World Processes suite flag: "+key);
    }
    return f;
}
inline void installSuite(Simulation& sim,SuiteFlags flags) {
    // Order is stable across game/server/editor builds. Integrations can
    // place their own producer systems before this opt-in installation.
    if(flags.morph)morph::install(sim);
    if(flags.crowd){crowd::installRoutes(sim);crowd::install(sim);}
    if(flags.imprint){
        if(flags.crowd)installCrowdFootprints(sim);
        imprint::installTrails(sim);
        imprint::install(sim);
    }
    if(flags.circuit){
        circuit::registerDevicePersistence(sim);
        circuit::installSwitches(sim);
        circuit::install(sim);
        circuit::installDevices(sim);
    }
    if(flags.granular)granular::install(sim);
    if(flags.evolve)evolve::install(sim);
    if(flags.society)society::install(sim);
}
inline void installSuite(Simulation& sim,const Json& config){
    installSuite(sim,suiteFlags(config));
}
} // namespace aiverse::lib::worldprocess
